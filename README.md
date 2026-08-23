# Pocket Audio Deck Web Radio

[日本語](README.ja.md)

Open PlatformIO/Arduino firmware for the M5StickS3 and Pocket Audio Deck board.
It plays public HTTP MP3 web-radio streams through the board's PCM5102A DAC and
also plays MP3 files from microSD.

This public edition contains no region-locked service authentication, private
endpoints, regional station data, or related playback code.

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

## Build

```sh
./scripts/pio-local.sh run
```

Upload using the M5StickS3 serial port:

```sh
./scripts/pio-local.sh run -t upload --upload-port /dev/cu.usbmodemXXXX
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
