# Pocket Audio Deck Web Radio

[日本語・書き込み手順](README.ja.md) | [Photo manual (Japanese)](https://rinoproducts.official.jp/support/pocket-audio-deck/)

Open PlatformIO/Arduino firmware for the M5StickS3 and Pocket Audio Deck board.
It plays public HTTP MP3 web-radio streams through the board's PCM5102A DAC and
also plays MP3 files from microSD.

This public edition contains no region-locked service authentication, private
endpoints, regional station data, or related playback code.

## Start Here: Flash with PlatformIO

**Open this project in VS Code and select PlatformIO's Upload task to build and flash over USB.**
Board settings and library dependencies are already configured. Normally, no source edits or embedded Wi-Fi credentials are needed.

**Download → Open in VS Code → Connect USB → Upload → Set up Wi-Fi on the device**

### 1. Prepare

- M5StickS3 (not included with Pocket Audio Deck) and the Pocket Audio Deck board
- A **USB data cable**, not a charging-only cable
- An Internet-connected computer
- [Visual Studio Code](https://code.visualstudio.com/) with the [PlatformIO IDE extension](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)
- [Git](https://git-scm.com/downloads), required to fetch build dependencies even if you download this project as a ZIP

Install PlatformIO IDE from VS Code's Extensions view and let its initial setup finish.
There is no need to install Arduino IDE or PlatformIO Core separately.
See the [official PlatformIO installation guide](https://docs.platformio.org/en/stable/integration/ide/vscode.html#installation).

### 2. Open This Project

1. Select **Code → Download ZIP** on this GitHub page and extract it, or clone the repository.
2. In VS Code, use **File → Open Folder** to open the folder containing **`platformio.ini`**, not just `src`.
3. Wait for PlatformIO to load, then open **PROJECT TASKS → m5sticks3 → General** from the PlatformIO sidebar.

Do not create a new project or select another board. The `esp32-s3-devkitc1-n8r8` board identifier in `platformio.ini` is intentional for this M5StickS3 project; keep it unchanged.

### 3. Connect and Upload

1. Connect M5StickS3 to the computer by USB. Close any other serial monitor using the device.
2. Select **PROJECT TASKS → m5sticks3 → General → Upload**.
3. Keep USB connected until the terminal reports **`SUCCESS`**. The first run takes longer while tools and libraries download.
4. After the device restarts and displays the Pocket Audio Deck welcome screen, continue with first-time setup.

| Task | Result |
| --- | --- |
| Build | Compiles the firmware. **Does not flash the device.** |
| Upload | Builds as needed and flashes over USB. This is the usual choice. |
| Monitor | Shows device logs at 115200 baud for troubleshooting. |

Uploading replaces any other firmware installed on the M5StickS3.
**A successful Build is not a successful Upload.** Check `SUCCESS` in the Upload task's output.

### 4. Start Listening

**Web radio:** In radio mode, hold the blue KEY1 button for about 2 seconds and follow the QR-code Wi-Fi setup. See [Wi-Fi Setup](#wi-fi-setup).
`WiFi not configured` before setup does not mean flashing failed.

**MP3:** Insert a microSD card containing MP3 files and hold KEY2 to switch to MP3 mode. Local playback does not need Wi-Fi.

Audio comes from the **Pocket Audio Deck headphone jack**, not the M5StickS3 built-in speaker. Start with a low volume.
See the [photo manual (Japanese)](https://rinoproducts.official.jp/support/pocket-audio-deck/) for button locations and everyday operation.

### Upload Troubleshooting

| Symptom | What to check |
| --- | --- |
| PROJECT TASKS is missing | Open the folder containing `platformio.ini` and wait for PlatformIO setup to finish. |
| No USB port is found | Try a data-capable cable and a direct USB connection. Disconnect other USB serial devices. |
| Upload stays at `Connecting...` | Try manual download mode below. |
| Port is busy | Close PlatformIO Monitor and other serial-monitor applications before uploading. |
| Download fails / Git is missing | Check Internet access and install Git. Restart VS Code after installing Git. |
| `xtensa-esp32s3-elf-g++: command not found` or `get_metavar` errors | These are host build-environment errors before flashing. Keep the supplied `platformio.ini` and report your OS and the log including the first error in [Issues](https://github.com/norippy-i/PocketAudioDeck-WebRadio/issues). |
| Upload succeeds but the app does not start | Briefly press the power/reset button, then inspect startup logs with Monitor if needed. |

**Manual download mode:** With USB connected, hold the side **power/reset button** until the internal green LED flashes. This is not the blue KEY1 or a track-selection button.
See [M5Stack's button location and download-mode instructions](https://docs.m5stack.com/en/core/StickS3#download-mode).
Then select **PROJECT TASKS → m5sticks3-manual → General → Upload**.
Reselect the USB port if its name changes when entering download mode.

## Hardware

- M5StickS3
- Pocket Audio Deck external PCM5102A I2S DAC
- microSD card slot
- Previous/next media switches
- Volume up/down and push-to-mute control
- Headphone output

## Hardware Files

- [Schematic (PDF)](hardware/schematic/M5_PocketAudioDeck.pdf)
- [Upper enclosure model (STEP)](hardware/3d/PocketAudioDeck_Upper.step)
- [Base enclosure model (STEP)](hardware/3d/PocketAudioDeck_Base.step)

## Assembly

Fasten the enclosure on both the headphone-jack side and the M5StickS3 USB
cable connector side with M2 x 6 mm self-tapping screws.

## Features

- Four SomaFM Non-SSL 128kbps MP3 stations
- ICY stream-title display with horizontal scrolling
- PCM-driven FFT spectrum display
- microSD MP3 playback with ID3-first title/artist display and filename fallback
- Repeat-one, repeat-all, and shuffle modes
- Progress/spectrum view with automatic scrolling for long metadata
- Seamless MP3 track changes at the selected volume; fade-in is used only when mute is released
- Persistent station, mode, volume, EQ, MP3 view, repeat mode, and current track
- Display-off and one-touch wake behavior in both radio and MP3 modes
- QR-code Wi-Fi setup and captive portal
- Automatic SD card insertion/removal handling with hidden system folders excluded

## Included Stations

- Groove Salad
- Drone Zone
- Indie Pop Rocks!
- Space Station Soma

The URLs are taken from SomaFM's official Non-SSL MP3 playlists. SomaFM is a
listener-supported service; its availability and terms apply. Stream URLs may
change, so verify them against [SomaFM's official listen page](https://somafm.com/listen/).

## Wi-Fi Setup

No Wi-Fi credentials are stored in this repository.

1. Start in radio mode.
2. Hold KEY1 for about 2 seconds to show the Wi-Fi setup QR code.
3. Scan it and join `PocketAudioDeck-Setup`.
4. Enter the target Wi-Fi SSID and password in the captive portal.
5. Hold KEY1 for about 2 seconds again to leave setup without changing credentials.

Credentials are stored in ESP32 NVS.

## Controls

- SW1: next station or MP3 track
- SW2: previous station or MP3 track
- Volume control left/right: volume down/up
- Volume control push: toggle mute
- KEY1 click in MP3 mode: play/pause
- KEY1 click in Web Radio mode: turn the display off
- KEY1 hold for 1 second in MP3 mode: switch and save the progress/spectrum view
- KEY1 hold for 4 seconds in MP3 mode: restore the previous view and turn the display off
- KEY1 hold for 2 seconds in Web Radio mode: enter/leave Wi-Fi setup
- KEY1 press while the display is off: wake the display without triggering another action
- KEY2 click in MP3 mode: change repeat mode
- KEY2 hold: switch radio/MP3 mode
- KEY1 + KEY2: cycle EQ preset

## Command-Line Alternative

The VS Code steps above are sufficient for normal use. For command-line operation, open **PlatformIO Core CLI** in VS Code and run from the folder containing `platformio.ini`:

```sh
pio run -e m5sticks3
pio run -e m5sticks3 -t upload
```

If automatic port detection fails, find the device with `pio device list` and specify its port. Replace these example port names:

```sh
pio device list
# macOS
pio run -e m5sticks3 -t upload --upload-port /dev/cu.usbmodemXXXX
# Windows
pio run -e m5sticks3 -t upload --upload-port COM5
```

Use `-e m5sticks3-manual` after manually entering download mode.
Use `pio device monitor -b 115200` to inspect device logs.

On macOS/Linux, the existing helper is also available when PlatformIO is on your PATH:

```sh
./scripts/pio-local.sh run -e m5sticks3
./scripts/pio-local.sh run -e m5sticks3 -t upload
```

The project targets 8 MB flash with PSRAM. The pre-build script applies small
ESP32-audioI2S hooks for PCM visualization, output muting, and SD hot-plug
handling.

## Adding Stations

Add entries to `kStations` in `src/main.cpp`:

```cpp
{"Station Name", "http://example.com/live-128-mp3"},
```

Prefer direct HTTP MP3 stream URLs. Playlist pages, HTTPS-only streams, AAC,
HLS, and services requiring authentication may need additional implementation.

## License

Firmware source in this repository is released under the GNU General Public
License v3.0. It builds against and applies local patches to
[ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S), which is also
licensed under GPL-3.0. See [LICENSE](LICENSE) for the full terms. Radio content,
station names, trademarks, and other third-party libraries remain the property
of their respective owners and are not covered by this license.
