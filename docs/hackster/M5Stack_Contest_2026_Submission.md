# Pocket Audio Deck: Web Radio and MP3 Player for M5StickS3

## Hackster project fields

**Title:** Pocket Audio Deck: Web Radio and MP3 Player for M5StickS3

**Elevator pitch:** An M5StickS3 pocket player with a custom headphone board, microSD MP3, Web Radio, FFT visuals, and a 3D-printed enclosure.

**Difficulty:** Intermediate

**Estimated build time:** 2 days after the PCB and enclosure parts are ready

**Repository:** https://github.com/norippy-i/PocketAudioDeck-WebRadio

**Tags:** M5Stack, M5StickS3, ESP32-S3, Web Radio, MP3 Player, I2S, PCM5102A, microSD, PlatformIO, Arduino, 3D Printing

## Story

### Why I built it

I wanted a small standalone audio player that felt more like a dedicated MP3 player than a smartphone app. It should start quickly, work with physical controls, show useful information at a glance, and play through wired headphones. I also wanted it to switch between two sources: live Internet radio and MP3 files stored on a microSD card.

The M5StickS3 was a good starting point because it combines an ESP32-S3, Wi-Fi, PSRAM, a color display, and physical buttons in a very compact body. The challenge was turning it into a complete pocket audio product instead of leaving it as a development board with wires attached.

The result is Pocket Audio Deck: a custom expansion board, a two-part 3D-printed enclosure, and Arduino firmware that turns the M5StickS3 into a Web Radio and microSD MP3 player.

> **PHOTO 1 - REQUIRED:** Finished Pocket Audio Deck, display on, photographed at a slight angle. Use this as the Hackster cover image.

### From the first prototype to a dedicated audio board

The first prototype played network audio through the M5StickS3's built-in audio hardware. That was useful for proving the streaming and UI concepts, but a pocket player also needs wired headphone output, removable storage, and controls that can be operated without navigating menus.

I designed a dedicated Pocket Audio Deck board in KiCad. The board connects to the M5StickS3 expansion pins and adds:

- A PCM5102A stereo I2S DAC
- A TPA6132A2 stereo headphone amplifier
- A 3.5 mm headphone jack
- A microSD card socket
- Two previous/next switches
- A left/right/push control for volume and mute

The M5StickS3 remains the controller. It handles Wi-Fi, network protocols, MP3 decoding, metadata, FFT processing, the display, button logic, persistent settings, and mode switching. The custom board handles audio conversion, headphone drive, storage, and ergonomic controls.

> **PHOTO 2 - REQUIRED:** Top and bottom views of the assembled custom PCB before it is installed in the enclosure.

### Hardware architecture

The audio path is fully digital until it reaches the custom board:

1. The M5StickS3 receives an HTTP MP3 stream or reads an MP3 file from microSD.
2. ESP32-audioI2S decodes the MP3 data.
3. PCM samples are sent over I2S to the PCM5102A.
4. The DAC's left and right outputs feed the TPA6132A2 headphone amplifier.
5. The amplified stereo signal is available at the 3.5 mm headphone jack.

The main connections are:

| Function | M5StickS3 GPIO |
|---|---:|
| I2S BCLK | GPIO1 |
| I2S LRCK | GPIO2 |
| I2S DATA | GPIO3 |
| microSD SCLK | GPIO4 |
| microSD MOSI | GPIO5 |
| microSD MISO | GPIO6 |
| microSD CS | GPIO7 |
| Previous/next ADC input | GPIO8 |
| Volume down | GPIO43 |
| Volume up | GPIO44 |
| Mute push | GPIO0 |

The complete schematic is included in the GitHub repository. The PCB uses separate digital and analog supply domains around the DAC and headphone amplifier. R18 is a 0-ohm link in the current design; it can be replaced by a ferrite bead around 600 ohms at 100 MHz if additional supply-noise suppression is needed.

![Pocket Audio Deck schematic](assets/schematic.png)

### Enclosure

The enclosure consists of an upper and base part, both provided as STEP files in the repository. It keeps the display and controls accessible while protecting the custom board and microSD card.

Fasten the enclosure at the headphone-jack side and the M5StickS3 USB connector side with M2 x 6 mm self-tapping screws.

> **PHOTO 3 - REQUIRED:** Exploded view showing the M5StickS3, custom PCB, upper enclosure, base enclosure, and screws.

## Firmware

The firmware uses PlatformIO with the Arduino framework, M5Unified, and schreibfaul1's ESP32-audioI2S library. It targets the M5StickS3's 8 MB flash and PSRAM.

### Web Radio mode

The public firmware includes four listener-supported SomaFM stations using direct HTTP MP3 streams:

- Groove Salad
- Drone Zone
- Indie Pop Rocks!
- Space Station Soma

The player reads ICY stream metadata and displays the current track title. Long titles remain on one line and scroll only when they do not fit on the screen.

No Wi-Fi credentials are compiled into the public repository. Holding KEY1 in Radio mode starts a temporary access point and displays a QR code. A phone can scan the code, join `PocketAudioDeck-Setup`, and open the captive portal to enter the target SSID and password. The credentials are stored in ESP32 NVS.

### MP3 mode

MP3 mode scans the microSD card and starts playback when a card is available. It supports:

- ID3 title and artist metadata
- Track progress and remaining time
- Play/pause
- Previous and next track
- Repeat one
- Repeat all
- Shuffle
- Automatic card insertion and removal detection

Wi-Fi is explicitly disabled in MP3 mode. This reduces unnecessary RF activity and helps keep the player focused on local playback.

### Display and interaction

The interface is inspired by compact MP3 players rather than a general-purpose touchscreen UI. The upper area shows the station or artist and the current program or track title. The lower 64-pixel area displays a 16-band FFT spectrum analyzer.

The spectrum is calculated from decoded PCM data, not from random animation. A short history buffer delays the visualization by approximately 180 ms so that the on-screen motion aligns more naturally with the audio heard through the headphone output. The grayscale palette keeps the display readable without overpowering the metadata.

During tuning or buffering, the spectrum area is reused for a large centered status message. Volume changes appear as a temporary full-width overlay, avoiding a permanent volume label that would consume metadata space.

The firmware stores the last station, operating mode, volume, EQ preset, and MP3 playback preferences in NVS. After a restart, it returns to the previous Radio or MP3 mode. Mute is intentionally not persisted.

> **PHOTO 4 - REQUIRED:** Radio mode showing a station, current title, and active spectrum analyzer.

> **PHOTO 5 - RECOMMENDED:** MP3 mode showing ID3 information, progress, remaining time, and repeat/shuffle icon.

## Controls

| Control | Radio mode | MP3 mode |
|---|---|---|
| SW1 | Next station | Next track |
| SW2 | Previous station | Previous track |
| Left/right control | Volume down/up | Volume down/up |
| Control push | Toggle mute | Toggle mute |
| KEY1 click | - | Play/pause |
| KEY1 hold | Wi-Fi setup portal | Progress/spectrum view |
| KEY2 click | - | Change repeat mode |
| KEY2 hold | Switch to MP3 mode | Switch to Radio mode |
| KEY1 + KEY2 | Change EQ preset | Change EQ preset |

## Bill of materials

| Quantity | Component | Notes |
|---:|---|---|
| 1 | M5StickS3 | Main controller, display, Wi-Fi, ESP32-S3, and PSRAM |
| 1 | Pocket Audio Deck custom PCB | Fabricated from the KiCad design |
| 1 | PCM5102APWR | Stereo I2S DAC |
| 1 | TPA6132A2RTER | Stereo headphone amplifier |
| 1 | HC-PJ-320D-3P-S | 3.5 mm headphone jack |
| 1 | TF PUSH microSD socket | SPI storage |
| 2 | K2-1812SA-D3SW-04 | Previous/next switches |
| 1 | WS-001 control switch | Left/right volume and push mute |
| 1 | PZ254R-12-16P connector | M5StickS3 expansion connection |
| 1 | microSD card | FAT-formatted; stores MP3 files |
| 1 set | Resistors and capacitors | Values are listed in the schematic |
| 1 | 3D-printed upper enclosure | STEP file included |
| 1 | 3D-printed base enclosure | STEP file included |
| 2 | M2 x 6 mm self-tapping screws | Headphone-jack side and USB-connector side |
| 1 | Wired headphones | 3.5 mm stereo plug |

## Build instructions

### 1. Build the custom board

Fabricate and assemble the PCB using the supplied schematic. Pay particular attention to the analog power decoupling around the PCM5102A and TPA6132A2, and keep the I2S and analog output paths short and clean.

Before connecting headphones, inspect for shorts between 3.3 V, 3.3 VA, and ground. Verify the orientation of the DAC, amplifier, microSD socket, and board connector.

### 2. Print and assemble the enclosure

Print the upper and base models from the STEP files. Install the M5StickS3 and Pocket Audio Deck board, align the headphone jack and USB opening, and fasten both ends using M2 x 6 mm self-tapping screws.

### 3. Prepare the microSD card

Format a microSD card as FAT and copy MP3 files to it. The player scans the card and uses ID3 tags when they are available. If a file has no title tag, the filename is used.

### 4. Build the firmware

Clone the public repository and run:

```sh
./scripts/pio-local.sh run
```

Upload while the M5StickS3 is connected by USB:

```sh
./scripts/pio-local.sh run -t upload --upload-port /dev/cu.usbmodemXXXX
```

The pre-build script applies small hooks to ESP32-audioI2S so the firmware can receive decoded PCM samples for the spectrum analyzer and safely handle SD-card removal.

### 5. Configure Wi-Fi

1. Start in Radio mode.
2. Hold KEY1 until the QR code appears.
3. Scan the QR code with a phone.
4. Enter the target Wi-Fi SSID and password in the captive portal.
5. Save the settings and return to Radio mode.

### 6. Test both modes

Confirm that Radio mode connects, displays `PLAYING`, shows ICY metadata, and produces audio. Insert a microSD card, hold KEY2 to enter MP3 mode, and verify metadata, play/pause, next/previous, volume, mute, repeat, shuffle, and card hot-plug behavior.

## Development lessons

This project grew through repeated real-device testing. Several details only became clear after using it as a physical player:

- A dedicated headphone DAC and amplifier made more sense than relying on the internal speaker for the final product.
- A real FFT must use decoded PCM samples; visually plausible random bars were not enough.
- The spectrum display felt early until its history buffer was aligned with the actual audio output path.
- Updating only small sprite regions eliminated distracting display flicker.
- Wi-Fi should be disabled in MP3 mode to reduce unnecessary activity.
- SD-card removal needs explicit file-handle cleanup before unmounting.
- USB and Wi-Fi noise can couple into analog audio, so power-domain layout and optional ferrite filtering matter.
- Persistent station, mode, volume, and EQ settings make the device feel like a finished player rather than a demo.

## What makes it special

Pocket Audio Deck is not just a Web Radio sketch. It combines an M5Stack controller, a purpose-built audio PCB, physical controls, removable storage, a responsive low-flicker interface, real PCM-based visualization, persistent settings, and a printable enclosure into one reproducible pocket device.

The public firmware intentionally uses direct HTTP MP3 radio streams that other makers can test without regional authentication. The source code, schematic PDF, and enclosure STEP files are all available in the GitHub repository.

## Result

The finished device boots into the last-used mode, remembers the last station and listening volume, plays live Web Radio or local MP3 files, and can be controlled without taking out a phone. It captures the direct, tactile feeling of a dedicated MP3 player while using the M5StickS3 as a modern networked audio controller.

> **VIDEO - STRONGLY RECOMMENDED:** A 45-90 second horizontal demo showing boot, Web Radio metadata and spectrum, station switching, volume/mute, MP3 mode, play/pause, and SD-card insertion/removal.

## Resources

- Public firmware and hardware files: https://github.com/norippy-i/PocketAudioDeck-WebRadio
- M5Stack Global Innovation Contest 2026: https://m5stack.com/global-innovation-contest-2026
- SomaFM listen page: https://somafm.com/listen/
