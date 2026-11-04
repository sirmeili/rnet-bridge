# RNET Bridge

Control a Russound RNET multi-zone controller (built and tested on the **CAA6.6**) from Home Assistant, through a
small ESP32 bridge plugged into the controller's RS-232 port.

- **Bridge firmware**: an Olimex ESP32-POE-ISO with an Olimex MOD-RS232. It speaks RNET to the controller, keeps
  a live copy of every zone, and offers it over a WebSocket/JSON API on your network (Ethernet/PoE, with Wi-Fi
  fallback).
- **Home Assistant integration** (`rnet_bridge`): finds the bridge automatically and turns each zone into a media
  player, with bass, treble, balance, loudness and turn-on volume controls.

[![Open your Home Assistant instance and open this repository in HACS.](https://my.home-assistant.io/badges/hacs_repository.svg)](https://my.home-assistant.io/redirect/hacs_repository/?owner=sirmeili&repository=rnet-bridge&category=integration)

## What you get in Home Assistant

For each zone you enable:

| Entity | What it does |
|---|---|
| Media player (named after the zone) | Power, volume (one step = 2 on the keypad), mute, source selection |
| Bass, Treble, Balance | −10 … +10 sliders |
| Turn-on volume | 0–100 %, as the keypad shows it. Changing it doesn't change what's playing now |
| Loudness | On/off switch |

On the controller device: **All zones off**, **Resync**, an **RNET link** sensor, and diagnostic counters
(disabled by default). **All zones on** is there too but disabled by default, because the CAA6.6 ignores it; enable
it if your controller supports it.

Zones and sources are named and enabled in Home Assistant; nothing needs reflashing. Changes made at a keypad show
up within about 3 seconds. Mute is emulated by the bridge (RNET has no mute): it remembers the volume and sets 0.

## Hardware

| Part | Notes |
|---|---|
| Olimex ESP32-POE-ISO (WROOM variant) | Powered by PoE or USB |
| Olimex MOD-RS232 | Plugs into the board's UEXT connector with the ribbon cable |
| DB9 male-to-male **null-modem** cable | From the MOD-RS232 to the controller's RS-232 port |

Only one device can use the controller's RS-232 port: unplug anything else (for example a serial-to-network
adapter) and remove Home Assistant's built-in `russound_rnet` integration. A 3D-printable case is in
[`hardware/enclosure/`](hardware/enclosure/). Details and verified wiring: [docs/SPEC.md](docs/SPEC.md) §2.

## Install

### 1. Flash the bridge

Download `firmware-factory.bin` from the [latest release](https://github.com/sirmeili/rnet-bridge/releases/latest)
and flash it over USB (first time only):

```bash
pip install esptool
esptool.py --chip esp32 --port COM4 write_flash 0x0 firmware-factory.bin   # your serial port here
```

Later updates can go over the network with the release's `firmware.bin`. Run this from the folder that holds
the file; it prints `{"ok":true}` and the bridge restarts by itself:

```bash
curl -F "firmware=@firmware.bin" http://rnet-bridge-xxxx.local/api/ota
```

On Windows PowerShell, type `curl.exe`, not `curl`: plain `curl` is PowerShell's `Invoke-WebRequest`, which rejects
`-F`.

Always use the **firmware and integration from the same release**. Their API version must match, and Home
Assistant shows a repair notice if it doesn't.

### 2. Connect it

Plug the bridge into Ethernet and the controller. It gets an address by DHCP and announces itself as
`rnet-bridge-xxxx.local`. Browse to that address for a status page.

**Wi-Fi instead of (or as a fallback to) Ethernet:** with no Ethernet, the bridge opens a setup network named
`RNET-Bridge-XXXX`. Join it from a phone, choose **Configure WiFi**, and enter your network. To reopen this portal
while on Ethernet, press the reset button **twice within 3 seconds**. The portal is also where you can set an
optional API token and OTA password.

### 3. Add the integration

1. In HACS, add this repository as a **custom repository** of type **Integration** (or use the button above), then
   download **Russound RNET Bridge** and restart Home Assistant.
2. Home Assistant should discover the bridge (**Settings → Devices & services**). If it doesn't, add
   **Russound RNET Bridge** and enter the bridge's address.
3. Choose which zones and sources to use, and name them. You can change this later under **Configure**.

## Known limitations

- **Changing a zone's turn-on volume while it's playing (CAA6.6).** The controller itself sets the zone's current
  volume to the new turn-on volume. The bridge puts the volume straight back about 50 ms later, so small changes
  go unnoticed, but a big jump (say from a playing level of 70 % to a turn-on volume of 0 % or 100 %) can be heard
  as a short blip. This was tested with both RNET command forms for turn-on volume: the Set Data (`0x00`) and the
  Plus/Minus Event (`0x05`). The controller behaved the same with both.

  **Have a different RNET controller?** If yours changes the turn-on volume *without* touching the volume that's
  playing, with either command form, please [open an issue](https://github.com/sirmeili/rnet-bridge/issues) with
  your model. We can make a test build with an option to turn the volume restore off for your controller, or to
  use the Event form.
- Only one device can use the controller's RS-232 port.
- **All zones on** does nothing on the CAA6.6, so that button is disabled by default.

## Troubleshooting

- **Entities unavailable**: the bridge is offline, or the **RNET link** sensor is off (check the cable, and that
  nothing else is on the RS-232 port).
- **Download diagnostics** from the integration's menu: it includes the bridge's link counters, with the API
  token redacted.
- **Developer tool**: the `rnet_bridge.send_raw` action sends raw RNET bytes through the bridge, exactly as
  given. It skips the bridge's safeguards, including the volume restore after a turn-on volume change.

## Development

| Path | Contents |
|---|---|
| `firmware/` | ESP32 firmware (PlatformIO, Arduino). `lib/` holds the host-testable protocol and network logic |
| `custom_components/rnet_bridge/` | Home Assistant integration |
| `tools/` | `fake_bridge.py` (simulated bridge), `rnet_cli.py` (terminal client) |
| `tests/` | Integration tests (pytest) |
| `docs/SPEC.md` | Project specification |
| `docs/protocol.md` | Network API contract between the firmware and the integration |
| `docs/rnet-notes.md` | What was verified on real hardware |

```bash
cd firmware && pio test -e native && pio run -e esp32     # firmware tests and build
pip install -r requirements_test.txt && pytest tests/       # integration tests
python tools/fake_bridge.py --port 8080                     # develop without hardware
```

## License

[MIT](LICENSE). Russound and RNET are trademarks of Russound/FMP, Inc. This project is not affiliated with or
endorsed by Russound.
