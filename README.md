# RNET Bridge

An ESP32 + MAX3232 bridge for Russound RNET controllers (target: CAA6.6). It plugs into the controller's RS-232
port and exposes it over a WebSocket/JSON API. A companion Home Assistant custom integration (`rnet_bridge`)
turns each zone into a media player, with tone and turn-on-volume controls.

> **Status: early development.** Nothing here is usable yet. See [docs/SPEC.md](docs/SPEC.md) §9 for the
> milestone plan. Installation instructions (including HACS) will be added before the first release.

## Repository layout

| Path | Contents |
|---|---|
| `firmware/` | ESP32 firmware (PlatformIO, Arduino). `lib/rnet/` is the host-testable protocol library |
| `docs/SPEC.md` | Project specification |
| `docs/reference/` | Russound's RNET protocol document, with an annotated Markdown transcription |

## Firmware development

```bash
cd firmware
pio test -e native      # protocol unit tests on the host
pio run -e esp32        # build the ESP32 firmware
```

## License

[MIT](LICENSE). Russound and RNET are trademarks of Russound/FMP, Inc. This project is not affiliated with or
endorsed by Russound.
