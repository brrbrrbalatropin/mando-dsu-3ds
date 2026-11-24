# Mando DSU — use a New 3DS as a Wi-Fi controller for Dolphin

*[Leer en español](README.es.md)*

A small homebrew app that turns a **New Nintendo 3DS** into a wireless gamepad for
[Dolphin](https://dolphin-emu.org/) (or any emulator with a DSU client, such as Cemu or Citra forks).

The 3DS acts as a **DSU server** (the "cemuhook" protocol, version 1001) on UDP port **26760**.
The emulator connects to the IP address shown on the 3DS top screen. Only button and stick
state is sent — no video — so latency is just the Wi-Fi round trip, and no drivers are needed
on the PC.

## Features
- All New 3DS inputs: A/B/X/Y, L/R, **ZL/ZR**, START/SELECT, D-Pad, **Circle Pad and C-Stick**,
  plus the touch screen.
- ~60 packets per second (one per frame), sent right after reading the input.
- Battery level and charging state reported to the client.
- Top screen shows the IP, connection status and live input, handy for checking mappings.
- Protocol code (`source/dsu.c`) is independent of libctru, so it can be reused or tested on a PC.
- `prueba_dsu.py`: a standard-library Python DSU client that validates every packet (magic,
  length, CRC32) and compares it byte by byte against a reference encoder.

## Requirements
- A New 3DS / New 2DS XL with custom firmware (e.g. Luma3DS) and the Homebrew Launcher.
  The original 3DS works too, but has no C-Stick or ZL/ZR.
- The 3DS and the PC on the same local network.
- To build: [devkitPro](https://devkitpro.org/) with devkitARM and libctru.

## Build
From the devkitPro MSYS2 shell (on Windows) or any shell with `DEVKITARM` set:

```sh
make
```

Copy `mando-dsu.3dsx` to `sdmc:/3ds/` on the SD card and launch it from the Homebrew Launcher.

## Usage with Dolphin
1. Open the app on the 3DS (Wi-Fi on). Note the IP shown on the top screen.
2. In Dolphin: **Controllers → Alternate Input Sources → DSU Client**, enable it and add a server
   with that IP and port `26760`.
3. **Controllers → Port 1 → Standard Controller → Configure**, choose the device
   `DSUClient/0/<server name>` and map the buttons (click a field, press the 3DS button).

To quit the app press **START + SELECT together** (START alone is forwarded to the emulator).

Alternatively, edit Dolphin's config files directly (with Dolphin closed). In
`Config/DSUClient.ini`:

```ini
[Server]
Enabled = True
Entries = 3DS:<3ds-ip>:26760;
```

And a GameCube controller mapping in `Config/GCPadNew.ini`:

```ini
[GCPad1]
Device = DSUClient/0/3DS
Buttons/A = `Circle`
Buttons/B = `Cross`
Buttons/X = `Triangle`
Buttons/Y = `Square`
Buttons/Z = `R2` | `L2`
Buttons/Start = `Options`
Main Stick/Up = `Left Y+`
Main Stick/Down = `Left Y-`
Main Stick/Left = `Left X-`
Main Stick/Right = `Left X+`
C-Stick/Up = `Right Y+`
C-Stick/Down = `Right Y-`
C-Stick/Left = `Right X-`
C-Stick/Right = `Right X+`
Triggers/L = `L1`
Triggers/R = `R1`
D-Pad/Up = `Pad N`
D-Pad/Down = `Pad S`
D-Pad/Left = `Pad W`
D-Pad/Right = `Pad E`
```

Dolphin names DSU inputs after a DualShock 4, so the 3DS buttons appear under PlayStation names.

## Button mapping

| 3DS | DSU field | Dolphin input name |
|---|---|---|
| A | A (right face button) | `Circle` |
| B | B (bottom) | `Cross` |
| X | X (top) | `Triangle` |
| Y | Y (left) | `Square` |
| L / R | L1 / R1 | `L1` / `R1` |
| ZL / ZR | L2 / R2 (0 or 255) | `L2` / `R2` |
| START / SELECT | Options / Share | `Options` / `Share` |
| D-Pad | D-Pad | `Pad N/S/E/W` |
| Circle Pad | Left stick | `Left X±`, `Left Y±` |
| C-Stick | Right stick | `Right X±`, `Right Y±` |
| Touch screen | Touch 1 (pixels, 320×240) | `Touch …` |

The HOME button is not sent (the 3DS system uses it). Motion sensors are not enabled: the
accelerometer reports a constant 1 g "at rest" and the gyro reports zero.

## Testing without an emulator

```sh
python prueba_dsu.py <3ds-ip> [seconds]   # live test against the console
python prueba_dsu.py --autoprueba         # self-test of the reference encoder, no console needed
```

A good run reports about 60 data packets per second, 0 differences from the reference and
`RESULTADO: OK`.

## Files
- `source/dsu.c`, `source/dsu.h` — the protocol: CRC32, packet building and request validation.
- `source/main.c` — 3DS side: input reading, UDP socket, top-screen status.
- `prueba_dsu.py` — Python test client.

Code comments and on-screen text are in Spanish.

## Protocol reference
[v1993/cemuhook-protocol](https://github.com/v1993/cemuhook-protocol) — the community
documentation of the DSU protocol. Dolphin's client implementation lives in
`Source/Core/InputCommon/ControllerInterface/DualShockUDPClient/`.

## License
[MIT](LICENSE). This project is not affiliated with or endorsed by Nintendo or the Dolphin team.
