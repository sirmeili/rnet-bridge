"""Config flow: zeroconf or manual host, then zones and sources (SPEC §5.3)."""

from __future__ import annotations

from collections.abc import Mapping
import logging
from typing import Any

from homeassistant.config_entries import (
    ConfigEntry,
    ConfigFlow,
    ConfigFlowResult,
    OptionsFlow,
)
from homeassistant.const import CONF_HOST, CONF_PORT, CONF_TOKEN
from homeassistant.core import callback
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.selector import (
    BooleanSelector,
    NumberSelector,
    NumberSelectorConfig,
    NumberSelectorMode,
    TextSelector,
    TextSelectorConfig,
    TextSelectorType,
)
from homeassistant.helpers.service_info.zeroconf import ZeroconfServiceInfo

from .api import (
    BridgeInfo,
    RnetBridgeAuthError,
    RnetBridgeClient,
    RnetBridgeConnectionError,
    RnetBridgeProtocolError,
)
from .compat import vol
from .const import (
    CONF_ENABLED,
    CONF_SOURCES,
    CONF_ZONES,
    DEFAULT_PORT,
    DOMAIN,
    NUM_SOURCES,
    NUM_ZONES,
    SUPPORTED_PROTO,
)

_LOGGER = logging.getLogger(__name__)

_TOKEN_SELECTOR = TextSelector(TextSelectorConfig(type=TextSelectorType.PASSWORD))
_PORT_SELECTOR = NumberSelector(
    NumberSelectorConfig(min=1, max=65535, mode=NumberSelectorMode.BOX)
)


def _enabled_key(n: int) -> str:
    return f"{n}_{CONF_ENABLED}"


def _name_key(n: int) -> str:
    return f"{n}_name"


def _items_schema(count: int) -> vol.Schema:
    """Enabled toggle and name for items 1..count (zones or sources)."""
    fields: dict[Any, Any] = {}
    for n in range(1, count + 1):
        fields[vol.Required(_enabled_key(n), default=True)] = BooleanSelector()
        fields[vol.Optional(_name_key(n))] = TextSelector()  # blank -> "Zone N"
    return vol.Schema(fields)


def _items_defaults(count: int, label: str) -> dict[str, Any]:
    defaults: dict[str, Any] = {}
    for n in range(1, count + 1):
        defaults[_enabled_key(n)] = True
        defaults[_name_key(n)] = f"{label} {n}"
    return defaults


def _items_current(items: dict[str, dict[str, Any]], count: int) -> dict[str, Any]:
    """Stored options -> form values, to pre-fill the options flow."""
    values: dict[str, Any] = {}
    for n in range(1, count + 1):
        item = items.get(str(n), {})
        values[_enabled_key(n)] = bool(item.get(CONF_ENABLED, True))
        values[_name_key(n)] = item.get("name", "")
    return values


def _items_from_input(
    user_input: dict[str, Any], count: int, label: str
) -> dict[str, dict[str, Any]]:
    """Form input -> {"1": {"enabled": bool, "name": str}, ...} for entry.options."""
    items: dict[str, dict[str, Any]] = {}
    for n in range(1, count + 1):
        name = str(user_input.get(_name_key(n), "")).strip() or f"{label} {n}"
        items[str(n)] = {
            CONF_ENABLED: bool(user_input.get(_enabled_key(n), True)),
            "name": name,
        }
    return items


class RnetBridgeConfigFlow(ConfigFlow, domain=DOMAIN):
    """Add an RNET Bridge."""

    VERSION = 1

    @staticmethod
    @callback
    def async_get_options_flow(config_entry: ConfigEntry) -> RnetBridgeOptionsFlow:
        """Zones and sources can be changed at any time (SPEC §5.3)."""
        return RnetBridgeOptionsFlow()

    def __init__(self) -> None:
        """Start a new flow."""
        self._host: str | None = None
        self._port: int = DEFAULT_PORT
        self._token: str | None = None
        self._info: BridgeInfo | None = None
        self._zones: dict[str, dict[str, Any]] = {}

    async def _async_try_connect(self) -> dict[str, str]:
        """Connect with the current host/port/token; return form errors."""
        assert self._host is not None
        client = RnetBridgeClient(
            async_get_clientsession(self.hass), self._host, self._port, self._token
        )
        try:
            await client.connect()
        except RnetBridgeConnectionError:
            return {"base": "cannot_connect"}
        except RnetBridgeAuthError:
            return {"base": "invalid_auth"}
        except RnetBridgeProtocolError as err:
            if err.proto is not None:
                return {"base": "unsupported_proto"}
            return {"base": "cannot_connect"}
        except Exception:
            _LOGGER.exception("Unexpected error connecting to the RNET Bridge")
            return {"base": "unknown"}
        finally:
            await client.close()
        self._info = client.info
        return {}

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        """Manual entry: host, port and an optional token."""
        errors: dict[str, str] = {}
        if user_input is not None:
            self._host = str(user_input[CONF_HOST]).strip()
            self._port = int(user_input.get(CONF_PORT, DEFAULT_PORT))
            self._token = user_input.get(CONF_TOKEN) or None
            errors = await self._async_try_connect()
            if not errors:
                assert self._info is not None
                await self.async_set_unique_id(self._info.bridge_id)
                self._abort_if_unique_id_configured(
                    updates={CONF_HOST: self._host, CONF_PORT: self._port}
                )
                return await self.async_step_zones()

        schema = vol.Schema(
            {
                vol.Required(CONF_HOST, default=self._host or ""): TextSelector(),
                vol.Optional(CONF_PORT, default=self._port): _PORT_SELECTOR,
                vol.Optional(CONF_TOKEN): _TOKEN_SELECTOR,
            }
        )
        return self.async_show_form(step_id="user", data_schema=schema, errors=errors)

    async def async_step_zeroconf(
        self, discovery_info: ZeroconfServiceInfo
    ) -> ConfigFlowResult:
        """Discovered `_rnetbridge._tcp`. The unique id is the TXT `id`."""
        bridge_id = discovery_info.properties.get("id")
        if not bridge_id:
            return self.async_abort(reason="invalid_discovery")
        self._host = discovery_info.host
        self._port = discovery_info.port or DEFAULT_PORT
        await self.async_set_unique_id(bridge_id)
        # Rediscovery with a new address (e.g. Ethernet <-> Wi-Fi) updates the entry.
        self._abort_if_unique_id_configured(
            updates={CONF_HOST: self._host, CONF_PORT: self._port}
        )
        proto = discovery_info.properties.get("proto")
        if proto is not None and proto != str(SUPPORTED_PROTO):
            return self.async_abort(reason="unsupported_proto")
        self.context["title_placeholders"] = {"name": f"RNET Bridge {bridge_id[-4:]}"}
        return await self.async_step_zeroconf_confirm()

    async def async_step_zeroconf_confirm(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        """Confirm a discovered bridge; ask for a token if it needs one."""
        errors: dict[str, str] = {}
        if user_input is not None:
            self._token = user_input.get(CONF_TOKEN) or None
            errors = await self._async_try_connect()
            if not errors:
                return await self.async_step_zones()

        assert self.unique_id is not None
        return self.async_show_form(
            step_id="zeroconf_confirm",
            data_schema=vol.Schema({vol.Optional(CONF_TOKEN): _TOKEN_SELECTOR}),
            description_placeholders={"host": self._host or "", "id": self.unique_id},
            errors=errors,
        )

    async def async_step_reauth(
        self, entry_data: Mapping[str, Any]
    ) -> ConfigFlowResult:
        """The bridge rejected the stored token (or now requires one)."""
        self._host = entry_data[CONF_HOST]
        self._port = entry_data.get(CONF_PORT, DEFAULT_PORT)
        return await self.async_step_reauth_confirm()

    async def async_step_reauth_confirm(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        """Ask for the API token set in the bridge's setup portal."""
        errors: dict[str, str] = {}
        if user_input is not None:
            self._token = user_input.get(CONF_TOKEN) or None
            errors = await self._async_try_connect()
            if not errors:
                assert self._info is not None
                await self.async_set_unique_id(self._info.bridge_id)
                self._abort_if_unique_id_mismatch(reason="wrong_bridge")
                entry = self._get_reauth_entry()
                data = {**entry.data}
                if self._token:
                    data[CONF_TOKEN] = self._token
                else:
                    data.pop(CONF_TOKEN, None)
                return self.async_update_reload_and_abort(entry, data=data)

        return self.async_show_form(
            step_id="reauth_confirm",
            data_schema=vol.Schema({vol.Optional(CONF_TOKEN): _TOKEN_SELECTOR}),
            description_placeholders={"host": self._host or ""},
            errors=errors,
        )

    async def async_step_zones(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        """Enable and name zones 1-6 (default: all enabled, "Zone N")."""
        if user_input is not None:
            self._zones = _items_from_input(user_input, NUM_ZONES, "Zone")
            return await self.async_step_sources()
        schema = self.add_suggested_values_to_schema(
            _items_schema(NUM_ZONES), _items_defaults(NUM_ZONES, "Zone")
        )
        return self.async_show_form(step_id="zones", data_schema=schema)

    async def async_step_sources(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        """Enable and name sources 1-6, then create the entry."""
        if user_input is None:
            schema = self.add_suggested_values_to_schema(
                _items_schema(NUM_SOURCES), _items_defaults(NUM_SOURCES, "Source")
            )
            return self.async_show_form(step_id="sources", data_schema=schema)

        assert self._host is not None
        assert self.unique_id is not None
        data: dict[str, Any] = {CONF_HOST: self._host, CONF_PORT: self._port}
        if self._token:
            data[CONF_TOKEN] = self._token
        return self.async_create_entry(
            title=f"RNET Bridge {self.unique_id[-4:]}",
            data=data,
            options={
                CONF_ZONES: self._zones,
                CONF_SOURCES: _items_from_input(user_input, NUM_SOURCES, "Source"),
            },
        )


class RnetBridgeOptionsFlow(OptionsFlow):
    """Re-run the zones and sources steps. Saving reloads the entry, which adds
    entities for newly enabled zones and removes devices of disabled ones."""

    def __init__(self) -> None:
        """Start with no changes."""
        self._zones: dict[str, dict[str, Any]] = {}

    async def async_step_init(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        """Zones first."""
        return await self.async_step_zones()

    async def async_step_zones(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        """Enable and rename zones."""
        if user_input is not None:
            self._zones = _items_from_input(user_input, NUM_ZONES, "Zone")
            return await self.async_step_sources()
        schema = self.add_suggested_values_to_schema(
            _items_schema(NUM_ZONES),
            _items_current(self.config_entry.options[CONF_ZONES], NUM_ZONES),
        )
        return self.async_show_form(step_id="zones", data_schema=schema)

    async def async_step_sources(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        """Enable and rename sources, then save."""
        if user_input is None:
            schema = self.add_suggested_values_to_schema(
                _items_schema(NUM_SOURCES),
                _items_current(self.config_entry.options[CONF_SOURCES], NUM_SOURCES),
            )
            return self.async_show_form(step_id="sources", data_schema=schema)
        return self.async_create_entry(
            data={
                CONF_ZONES: self._zones,
                CONF_SOURCES: _items_from_input(user_input, NUM_SOURCES, "Source"),
            }
        )
