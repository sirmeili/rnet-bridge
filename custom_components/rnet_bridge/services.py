"""The rnet_bridge.send_raw debug service (SPEC §5.5)."""

from __future__ import annotations

import logging

from homeassistant.config_entries import ConfigEntryState
from homeassistant.core import HomeAssistant, ServiceCall, callback
from homeassistant.exceptions import ServiceValidationError
from homeassistant.helpers import config_validation as cv

from .compat import vol
from .const import DOMAIN

_LOGGER = logging.getLogger(__name__)

SERVICE_SEND_RAW = "send_raw"
ATTR_CONFIG_ENTRY_ID = "config_entry_id"
ATTR_HEX = "hex"
ATTR_FIX_CHECKSUM = "fix_checksum"

SEND_RAW_SCHEMA = vol.Schema(
    {
        vol.Required(ATTR_CONFIG_ENTRY_ID): cv.string,
        vol.Required(ATTR_HEX): cv.string,
        vol.Optional(ATTR_FIX_CHECKSUM, default=False): cv.boolean,
    }
)


@callback
def async_setup_services(hass: HomeAssistant) -> None:
    """Register the services (called once from async_setup)."""

    async def send_raw(call: ServiceCall) -> None:
        entry_id: str = call.data[ATTR_CONFIG_ENTRY_ID]
        entry = hass.config_entries.async_get_entry(entry_id)
        if entry is None or entry.domain != DOMAIN:
            raise ServiceValidationError(
                translation_domain=DOMAIN,
                translation_key="entry_not_found",
                translation_placeholders={"entry_id": entry_id},
            )
        if entry.state is not ConfigEntryState.LOADED:
            raise ServiceValidationError(
                translation_domain=DOMAIN,
                translation_key="entry_not_loaded",
                translation_placeholders={"title": entry.title},
            )
        hex_bytes: str = call.data[ATTR_HEX]
        fix: bool = call.data[ATTR_FIX_CHECKSUM]
        await entry.runtime_data.async_send_raw(hex_bytes, fix)
        _LOGGER.info(
            "send_raw to %s: %s%s: ok",
            entry.title,
            hex_bytes,
            " (checksum appended)" if fix else "",
        )

    hass.services.async_register(DOMAIN, SERVICE_SEND_RAW, send_raw, SEND_RAW_SCHEMA)
