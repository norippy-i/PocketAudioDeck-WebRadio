# Pocket Audio Deck Web Radio

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

## Features

- Four SomaFM Non-SSL 128kbps MP3 stations
- ICY stream-title display with horizontal scrolling
- PCM-driven FFT spectrum display
- microSD MP3 playback with metadata and progress display
- Repeat-one, repeat-all, and shuffle modes
- Persistent station, mode, volume, and EQ settings
- QR-code Wi-Fi setup and captive portal
- Automatic SD card insertion/removal handling

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
2. Hold KEY1 to show the Wi-Fi setup QR code.
3. Scan it and join `PocketAudioDeck-Setup`.
4. Enter the target Wi-Fi SSID and password in the captive portal.
5. Hold KEY1 again to leave setup without changing credentials.

Credentials are stored in ESP32 NVS.

## Controls

- SW1/SW2: previous/next station or MP3 track
- Volume control left/right: volume down/up
- Volume control push: toggle mute
- KEY1 click in MP3 mode: play/pause
- KEY1 hold in MP3 mode: progress/spectrum view
- KEY1 hold in radio mode: enter/leave Wi-Fi setup
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

Firmware source in this repository is released under the MIT License. Radio
content, station names, trademarks, and third-party libraries remain the
property of their respective owners and are not covered by this license.
