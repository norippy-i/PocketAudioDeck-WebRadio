#include <Arduino.h>
#include <Audio.h>
#include <DNSServer.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_dsp.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <vector>

#include "welcome_logo.h"

// Leave empty in source. Configure Wi-Fi from the on-device captive portal.
static const char* WIFI_SSID = "";
static const char* WIFI_PASSWORD = "";
static const char* WIFI_SETUP_AP_SSID = "PocketAudioDeck-Setup";
static const char* WIFI_SETUP_AP_PASSWORD = "pocketaudio";

// M5 PocketAudioDeck: PCM5102A I2S and microSD SPI pins.
static constexpr uint8_t I2S_BCLK = 1;
static constexpr uint8_t I2S_LRCK = 2;
static constexpr uint8_t I2S_DOUT = 3;
static constexpr uint8_t SD_SCLK = 4;
static constexpr uint8_t SD_MOSI = 5;
static constexpr uint8_t SD_MISO = 6;
static constexpr uint8_t SD_CS = 7;
static constexpr uint8_t MEDIA_KEY_ADC = 8;
static constexpr uint8_t VOLUME_UP_PIN = 44;
static constexpr uint8_t VOLUME_DOWN_PIN = 43;
static constexpr uint8_t VOLUME_MUTE_PIN = 0;
static constexpr bool kVerboseHttpLog = false;
static constexpr bool kVerboseAudioLog = false;
static constexpr bool kVerbosePcmLog = false;
static constexpr uint32_t kWelcomeDurationMs = 3000;
static constexpr uint32_t kSdMountRetryIntervalMs = 1000;
static constexpr uint32_t kSdPresenceCheckIntervalMs = 750;
static constexpr uint32_t kUsbPowerPollIntervalMs = 1000;
static constexpr int16_t kUsbPresentVoltageMv = 4200;
static constexpr int16_t kUsbAbsentVoltageMv = 3800;

static Audio audio(I2S_NUM_0);
static Preferences preferences;
static SPIClass sdSpi(FSPI);
static DNSServer wifiConfigDns;
static WebServer wifiConfigServer(80);

struct WebRadioStation {
  const char* name;
  const char* url;
};

struct Mp3TrackMetadata {
  const char* id;
  const char* title;
  const char* artist;
};

static const WebRadioStation kStations[] = {
    {"Groove Salad", "http://ice2.somafm.com/groovesalad-128-mp3"},
    {"Drone Zone", "http://ice2.somafm.com/dronezone-128-mp3"},
    {"Indie Pop Rocks!", "http://ice2.somafm.com/indiepop-128-mp3"},
    {"Space Station Soma", "http://ice2.somafm.com/spacestation-128-mp3"},
};
static constexpr size_t kStationCount = sizeof(kStations) / sizeof(kStations[0]);

static const Mp3TrackMetadata kMp3TrackMetadata[] = {
    {"14", "シャイニングスター", "魔王魂"},
    {"32", "ときめき☆ラビリンス", "魔王魂"},
    {"46", "夜明けのHighway", "魔王魂"},
    {"47", "GAIA", "魔王魂"},
    {"49", "Piece Maker", "魔王魂"},
};
static constexpr size_t kMp3TrackMetadataCount =
    sizeof(kMp3TrackMetadata) / sizeof(kMp3TrackMetadata[0]);

enum class AppMode : uint8_t {
  Radio,
  Mp3,
};

enum class Mp3LoopMode : uint8_t {
  One,
  All,
  Shuffle,
};

enum class Mp3LowerView : uint8_t {
  Progress,
  Spectrum,
};

enum class EqPreset : uint8_t {
  Flat,
  Bass,
  Vocal,
  Bright,
  Night,
  Count,
};

struct EqPresetSetting {
  const char* name;
  float lowGain;
  float midGain;
  float highGain;
};

static constexpr EqPresetSetting kEqPresets[] = {
    {"FLAT", 0.0f, 0.0f, 0.0f},
    {"BASS", 4.0f, 1.0f, -1.0f},
    {"VOCAL", -2.0f, 4.0f, 1.0f},
    {"BRIGHT", -2.0f, 0.0f, 4.0f},
    {"NIGHT", 1.0f, 2.0f, -4.0f},
};
static_assert(sizeof(kEqPresets) / sizeof(kEqPresets[0]) ==
                  static_cast<size_t>(EqPreset::Count),
              "EQ preset table mismatch");

enum class MediaKey : uint8_t {
  None,
  Previous,
  Next,
};

struct DebouncedInput {
  bool candidate = false;
  bool stable = false;
  bool pressed = false;
  bool released = false;
  uint32_t changedAtMs = 0;
};

static AppMode appMode = AppMode::Radio;
static int pendingAppMode = -1;
static String wifiSsid;
static String wifiPassword;
static bool wifiConnectionFailed = false;
static bool wifiConfigPortalRequested = false;
static bool wifiConfigPortalActive = false;
static bool wifiConfigPortalExitRequested = false;
static bool wifiCredentialsSubmitted = false;
static bool wifiConfigRoutesReady = false;
static Mp3LoopMode mp3LoopMode = Mp3LoopMode::All;
static Mp3LowerView mp3LowerView = Mp3LowerView::Progress;
static constexpr uint8_t kDefaultVolume = 16;
static constexpr uint8_t kMaximumVolume = 21;
static constexpr uint32_t kVolumeSaveDelayMs = 750;
static uint8_t volumeLevel = kDefaultVolume;
static uint8_t lastSavedVolume = kDefaultVolume;
static bool volumeSavePending = false;
static uint32_t volumeChangedAtMs = 0;
static volatile bool audioMuted = false;
static bool audioOutputReady = false;
static volatile bool audioOutputMuted = true;
static bool usbPowerPresent = false;
static bool usbPowerKnown = false;
static bool usbIdleStandby = false;
static uint32_t lastUsbPowerPollMs = 0;
static EqPreset eqPreset = EqPreset::Flat;
static bool keyChordActive = false;
static size_t stationIndex = 0;
static String currentStatus;
static bool streamReadyShown = false;
static bool tuningBusy = false;
static bool tuneAbortRequested = false;
static int pendingTuneIndex = -1;
static volatile uint32_t pcmBlocks = 0;
static volatile uint32_t pcmSamples = 0;
static volatile uint32_t pcmPeak = 0;
static volatile bool playbackSamplesSeen = false;
static uint32_t lastPcmReportMs = 0;
static String programTitle = "Waiting for stream title";
static bool uiNeedsFullRedraw = true;
static uint32_t lastUiUpdateMs = 0;
static uint32_t titleScrollStartMs = 0;
static uint32_t volumeOverlayUntilMs = 0;
static uint32_t eqOverlayUntilMs = 0;
static String lastDrawnProgramTitle;
static String lastDrawnSpectrumStatus;
static bool spectrumStatusDrawn = false;
static AppMode spectrumStatusMode = AppMode::Radio;
static std::vector<String> mp3Tracks;
static size_t mp3TrackIndex = 0;
static bool sdMounted = false;
static bool sdBusStarted = false;
static uint32_t lastSdMountAttemptMs = 0;
static uint32_t lastSdPresenceCheckMs = 0;
static bool mp3Paused = false;
static bool mp3UsesKnownMetadata = false;
static volatile bool mp3EofPending = false;
static uint32_t mp3DisplayDuration = 0;
static uint32_t mp3DurationCandidate = 0;
static uint32_t mp3DurationCandidateSinceMs = 0;
static String mp3Title;
static String mp3Artist;
static String lastDrawnMp3Title;
static String lastDrawnMp3Artist;
static uint32_t mp3TitleScrollStartMs = 0;
static uint32_t mp3ArtistScrollStartMs = 0;
static char pendingMp3Title[384] = {};
static char pendingMp3Artist[384] = {};
static volatile uint8_t pendingMp3MetadataMask = 0;
static portMUX_TYPE mp3MetadataMux = portMUX_INITIALIZER_UNLOCKED;
static constexpr uint8_t kMp3MetadataTitle = 1U << 0;
static constexpr uint8_t kMp3MetadataArtist = 1U << 1;
static MediaKey mediaKeyCandidate = MediaKey::None;
static MediaKey mediaKeyStable = MediaKey::None;
static uint32_t mediaKeyChangedAtMs = 0;
static DebouncedInput volumeUpInput;
static DebouncedInput volumeDownInput;
static DebouncedInput volumeMuteInput;
static uint32_t lastVolumeRepeatMs = 0;
static constexpr int32_t kSpectrumAreaHeight = 64;
static constexpr size_t kSpectrumBands = 16;
static constexpr size_t kFftSize = 256;
static constexpr size_t kFftHistorySize = 16384;
static constexpr uint32_t kSpectrumDelayMs = 180;
static constexpr uint16_t kSpectrumBarColor = 0x9CD3;
static constexpr uint16_t kSpectrumBarCapColor = 0xC618;
static float fftRing[kFftHistorySize] = {};
static float fftWork[kFftSize * 2] = {};
static float fftWindow[kFftSize] = {};
static volatile uint16_t fftWriteIndex = 0;
static volatile uint32_t fftSampleCount = 0;
static volatile uint32_t lastRawPcmMs = 0;
static bool fftReady = false;
static bool fftInputGainReady = false;
static float fftInputGain = 1.0f;
static volatile float fftInputPeak = 0.0f;
static float spectrumLevels[kSpectrumBands] = {};
static float spectrumNoiseFloor[kSpectrumBands] = {};
static bool spectrumNoiseReady = false;
static M5Canvas topCanvas(&M5.Display);
static M5Canvas spectrumCanvas(&M5.Display);
static portMUX_TYPE fftMux = portMUX_INITIALIZER_UNLOCKED;

static const WebRadioStation& currentStation() {
  return kStations[stationIndex];
}

static const char* currentStationName() {
  return currentStation().name;
}

static const char* activeStreamUrl() {
  return currentStation().url;
}

static void renderUi();
static void showStatus(const char* status);
static void applyVolume();
static void adjustVolume(int delta);
static void toggleMute();
static void applyEqPreset(bool save);
static void cycleEqPreset();
static void updateUsbPowerState(bool force = false);
static void updateAudioOutputGate(bool force = false);
static void disableWiFiForMp3();
static void handlePeripheralControls();
static void handleM5Keys();
static void handleSdHotplug();
static bool playMp3Track(size_t index);
static void switchAppMode(AppMode mode);
static void saveCurrentStation();
static bool tuneStation(size_t index);
static bool connectWiFi();
static void startWifiConfigPortal();
static void serviceWifiConfigPortal();

static void resetStationUiState() {
  programTitle = "Waiting for stream title";
  lastDrawnProgramTitle = "";
  uiNeedsFullRedraw = true;
  streamReadyShown = false;
}

static void selectStationImmediately(size_t index) {
  if (index >= kStationCount) {
    return;
  }
  stationIndex = index;
  saveCurrentStation();
  resetStationUiState();
  audio.stopSong();
  Serial.printf("[Station] selected %u/%u %s\n", static_cast<unsigned>(stationIndex + 1),
                static_cast<unsigned>(kStationCount), currentStationName());
  showStatus("Station selected");
}

static void requestTuneStation(size_t index) {
  if (index >= kStationCount) {
    return;
  }
  selectStationImmediately(index);
  if (tuningBusy) {
    pendingTuneIndex = static_cast<int>(index);
    tuneAbortRequested = true;
    return;
  }
  tuneStation(index);
}

static void requestNextStation() {
  requestTuneStation((stationIndex + 1) % kStationCount);
}

static void requestPreviousStation() {
  requestTuneStation((stationIndex + kStationCount - 1) % kStationCount);
}

static bool pollUserControlsDuringTuning() {
  M5.update();
  handlePeripheralControls();
  handleM5Keys();

  if (pendingAppMode >= 0) {
    tuneAbortRequested = true;
    audio.stopSong();
  }
  if (wifiConfigPortalRequested) {
    tuneAbortRequested = true;
    audio.stopSong();
  }

  renderUi();
  return tuneAbortRequested;
}

static void initSpectrumFft() {
  for (size_t i = 0; i < kFftSize; ++i) {
    fftWindow[i] =
        0.5f * (1.0f - cosf((2.0f * PI * static_cast<float>(i)) / static_cast<float>(kFftSize - 1)));
  }
  const esp_err_t result = dsps_fft2r_init_fc32(nullptr, kFftSize);
  fftReady = result == ESP_OK || result == ESP_ERR_DSP_REINITIALIZED;
  Serial.printf("[FFT] init %s\n", fftReady ? "OK" : "FAIL");
}

static void showStatus(const char* status) {
  currentStatus = status;
  if (strcmp(status, "Error") == 0 || strcmp(status, "No stream URL") == 0) {
    spectrumStatusDrawn = false;
    lastDrawnSpectrumStatus = "";
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setFont(&fonts::Font4);
    M5.Display.drawCentreString("ERROR", M5.Display.width() / 2, M5.Display.height() / 2 - 18);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.drawCentreString(status, M5.Display.width() / 2, M5.Display.height() / 2 + 10);
  } else {
    uiNeedsFullRedraw = true;
    renderUi();
  }
  Serial.printf("[Status] %s\n", status);
  updateAudioOutputGate();
}

static int32_t infoAreaHeight() {
  const int32_t h = M5.Display.height();
  return h > kSpectrumAreaHeight ? h - kSpectrumAreaHeight : h / 2;
}

static int32_t spectrumAreaHeight() {
  const int32_t h = M5.Display.height();
  return h - infoAreaHeight();
}

static void showWelcomeScreen() {
  M5Canvas canvas(&M5.Display);
  canvas.setColorDepth(8);
  if (!canvas.createSprite(M5.Display.width(), M5.Display.height())) {
    M5.Display.fillScreen(TFT_BLACK);
    return;
  }

  const int32_t w = canvas.width();
  const int32_t h = canvas.height();
  canvas.fillSprite(TFT_BLACK);
  canvas.drawRect(5, 5, w - 10, h - 10, kSpectrumBarColor);
  canvas.setTextColor(kSpectrumBarCapColor, TFT_BLACK);
  canvas.setTextSize(1);
  canvas.setFont(&fonts::Font0);
  canvas.drawCentreString("WELCOME", w / 2, 12);
  canvas.drawXBitmap((w - welcome_logo_width) / 2, 32, welcome_logo_bits,
                     welcome_logo_width, welcome_logo_height, TFT_WHITE);

  static constexpr uint8_t kWelcomeBars[] = {5, 11, 18, 27, 17, 9, 22, 31,
                                              24, 13, 7,  16, 26, 19, 10, 4};
  const int32_t graphBottom = h - 13;
  for (size_t i = 0; i < sizeof(kWelcomeBars); ++i) {
    const int32_t cellX0 = 9 + (static_cast<int32_t>(i) * (w - 18)) /
                                   static_cast<int32_t>(sizeof(kWelcomeBars));
    const int32_t cellX1 = 9 + (static_cast<int32_t>(i + 1) * (w - 18)) /
                                   static_cast<int32_t>(sizeof(kWelcomeBars));
    const int32_t barW = std::max<int32_t>(1, cellX1 - cellX0 - 2);
    canvas.fillRect(cellX0 + 1, graphBottom - kWelcomeBars[i], barW, kWelcomeBars[i],
                    kSpectrumBarColor);
    canvas.drawFastHLine(cellX0 + 1, graphBottom - kWelcomeBars[i], barW,
                         kSpectrumBarCapColor);
  }
  canvas.pushSprite(0, 0);
  canvas.deleteSprite();
}

static void waitForWelcomeScreen(uint32_t startedAtMs) {
  const uint32_t elapsed = millis() - startedAtMs;
  if (elapsed < kWelcomeDurationMs) {
    delay(kWelcomeDurationMs - elapsed);
  }
}

static const char* uiStatusLabel() {
  if (appMode == AppMode::Mp3) {
    if (!sdMounted) {
      return "NO SD CARD";
    }
    if (mp3Tracks.empty()) {
      return "NO MP3 FILES";
    }
    if (mp3Paused) {
      return "PAUSED";
    }
    return "";
  }
  if (wifiConfigPortalActive) {
    return "WI-FI SETUP";
  }
  if (wifiConnectionFailed) {
    return "NO WI-FI";
  }
  if (WiFi.status() != WL_CONNECTED) {
    return "TUNING...";
  }
  if (currentStatus == "Stream opened") {
    return "BUFFERING...";
  }
  if (currentStatus == "Playing") {
    return "";
  }
  return "TUNING...";
}

static void loadSavedStation() {
  preferences.begin("pocket-audio", true);
  const uint8_t saved = preferences.getUChar("station", 0);
  const uint8_t savedVolume = preferences.getUChar("volume", kDefaultVolume);
  const uint8_t savedMode =
      preferences.getUChar("mode", static_cast<uint8_t>(AppMode::Radio));
  const uint8_t savedEq =
      preferences.getUChar("eq", static_cast<uint8_t>(EqPreset::Flat));
  wifiSsid = preferences.getString("wifi_ssid", WIFI_SSID);
  wifiPassword = preferences.getString("wifi_pass", WIFI_PASSWORD);
  preferences.end();
  if (saved < kStationCount) {
    stationIndex = saved;
  }
  if (savedMode <= static_cast<uint8_t>(AppMode::Mp3)) {
    appMode = static_cast<AppMode>(savedMode);
  }
  if (savedEq < static_cast<uint8_t>(EqPreset::Count)) {
    eqPreset = static_cast<EqPreset>(savedEq);
  }
  volumeLevel = savedVolume <= kMaximumVolume ? savedVolume : kDefaultVolume;
  lastSavedVolume = volumeLevel;
  audioMuted = false;
  Serial.printf("[Station] boot station %u/%u %s\n", static_cast<unsigned>(stationIndex + 1),
                static_cast<unsigned>(kStationCount), currentStationName());
  Serial.printf("[Audio] boot volume=%u/%u mute=0\n", volumeLevel, kMaximumVolume);
  Serial.printf("[EQ] boot preset=%s\n", kEqPresets[static_cast<uint8_t>(eqPreset)].name);
  Serial.printf("[Mode] boot %s\n", appMode == AppMode::Mp3 ? "MP3" : "RADIO");
  Serial.printf("[WiFi] configured SSID '%s'\n", wifiSsid.c_str());
}

static void saveCurrentStation() {
  preferences.begin("pocket-audio", false);
  preferences.putUChar("station", static_cast<uint8_t>(stationIndex));
  preferences.end();
  Serial.printf("[Station] saved %u %s\n", static_cast<unsigned>(stationIndex),
                currentStationName());
}

static void saveAppMode() {
  preferences.begin("pocket-audio", false);
  preferences.putUChar("mode", static_cast<uint8_t>(appMode));
  preferences.end();
  Serial.printf("[Mode] saved %s\n", appMode == AppMode::Mp3 ? "MP3" : "RADIO");
}

static void maybeSaveVolume() {
  if (!volumeSavePending || millis() - volumeChangedAtMs < kVolumeSaveDelayMs) {
    return;
  }
  volumeSavePending = false;
  if (volumeLevel == lastSavedVolume) {
    return;
  }
  preferences.begin("pocket-audio", false);
  preferences.putUChar("volume", volumeLevel);
  preferences.end();
  lastSavedVolume = volumeLevel;
  Serial.printf("[Audio] saved volume=%u/%u\n", volumeLevel, kMaximumVolume);
}

static void drawTextClipped(M5Canvas& canvas, const String& text, int32_t x, int32_t y, int32_t w,
                            int32_t h, const lgfx::IFont* font, uint16_t color = TFT_WHITE) {
  canvas.setClipRect(x, y, w, h);
  canvas.setFont(font);
  canvas.setTextSize(1);
  canvas.setTextColor(color, TFT_BLACK);
  canvas.fillRect(x, y, w, h, TFT_BLACK);
  canvas.setCursor(x, y);
  canvas.print(text);
  canvas.clearClipRect();
}

static void drawScrollingText(M5Canvas& canvas, const String& text, int32_t x, int32_t y,
                              int32_t w, int32_t h, const lgfx::IFont* font, String* lastText,
                              uint32_t* scrollStartMs) {
  canvas.setClipRect(x, y, w, h);
  canvas.fillRect(x, y, w, h, TFT_BLACK);
  canvas.setFont(font);
  canvas.setTextSize(1);
  canvas.setTextWrap(false, false);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);

  if (*lastText != text) {
    *lastText = text;
    *scrollStartMs = millis();
  }

  const int32_t textW = canvas.textWidth(text);
  int32_t drawX = x;
  if (textW > w) {
    static constexpr uint32_t kHoldMs = 3000;
    static constexpr uint32_t kEndHoldMs = 200;
    static constexpr uint32_t kMsPerPixel = 28;
    const int32_t scrollDistance = textW;
    const uint32_t scrollMs = static_cast<uint32_t>(scrollDistance) * kMsPerPixel;
    const uint32_t cycleMs = kHoldMs + scrollMs + kEndHoldMs;
    const uint32_t phase = millis() - *scrollStartMs;
    const uint32_t cyclePhase = cycleMs > 0 ? phase % cycleMs : 0;
    int32_t offset = 0;
    if (cyclePhase >= kHoldMs) {
      const uint32_t scrollPhase = cyclePhase - kHoldMs;
      if (scrollPhase >= scrollMs) {
        offset = scrollDistance;
      } else {
        offset = static_cast<int32_t>(scrollPhase / kMsPerPixel);
      }
    }
    drawX = x - offset;
  }

  canvas.setCursor(drawX, y);
  canvas.print(text);
  canvas.clearClipRect();
}

static void drawVolumePopup(M5Canvas& canvas) {
  char label[9];
  if (audioMuted) {
    snprintf(label, sizeof(label), "MUTE");
  } else {
    snprintf(label, sizeof(label), "VOL.%02u", volumeLevel);
  }

  canvas.setFont(&fonts::Font4);
  canvas.setTextSize(1);
  const int32_t textW = canvas.textWidth(label);
  const int32_t boxW = canvas.width();
  const int32_t boxH = 34;
  const int32_t x = 0;
  int32_t y = (infoAreaHeight() - boxH) / 2;
  if (y < 2) {
    y = 2;
  }

  canvas.fillRect(x, y, boxW, boxH, TFT_WHITE);
  canvas.drawRect(x, y, boxW, boxH, TFT_BLACK);
  canvas.setTextColor(TFT_BLACK, TFT_WHITE);
  canvas.setCursor(x + (boxW - textW) / 2, y + 7);
  canvas.print(label);
}

static void drawEqPopup(M5Canvas& canvas) {
  char label[16];
  snprintf(label, sizeof(label), "EQ: %s",
           kEqPresets[static_cast<uint8_t>(eqPreset)].name);

  canvas.setFont(&fonts::Font4);
  canvas.setTextSize(1);
  const int32_t textW = canvas.textWidth(label);
  const int32_t boxW = canvas.width();
  const int32_t boxH = 34;
  const int32_t x = 0;
  int32_t y = (infoAreaHeight() - boxH) / 2;
  if (y < 2) {
    y = 2;
  }

  canvas.fillRect(x, y, boxW, boxH, TFT_WHITE);
  canvas.drawRect(x, y, boxW, boxH, TFT_BLACK);
  canvas.setTextColor(TFT_BLACK, TFT_WHITE);
  canvas.setCursor(x + (boxW - textW) / 2, y + 7);
  canvas.print(label);
}

static void drawLoopModeIcon(M5Canvas& canvas, int32_t x, int32_t y, uint16_t color) {
  if (mp3LoopMode == Mp3LoopMode::Shuffle) {
    canvas.drawLine(x + 2, y + 3, x + 7, y + 3, color);
    canvas.drawLine(x + 7, y + 3, x + 16, y + 13, color);
    canvas.drawLine(x + 2, y + 13, x + 7, y + 13, color);
    canvas.drawLine(x + 7, y + 13, x + 16, y + 3, color);
    canvas.fillTriangle(x + 15, y, x + 21, y + 3, x + 15, y + 6, color);
    canvas.fillTriangle(x + 15, y + 10, x + 21, y + 13, x + 15, y + 16, color);
    return;
  }

  canvas.drawLine(x + 4, y + 3, x + 18, y + 3, color);
  canvas.drawLine(x + 18, y + 3, x + 18, y + 7, color);
  canvas.fillTriangle(x + 15, y, x + 21, y + 3, x + 15, y + 6, color);
  canvas.drawLine(x + 4, y + 13, x + 18, y + 13, color);
  canvas.drawLine(x + 4, y + 9, x + 4, y + 13, color);
  canvas.fillTriangle(x + 7, y + 10, x + 1, y + 13, x + 7, y + 16, color);
  if (mp3LoopMode == Mp3LoopMode::One) {
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(color, TFT_BLACK);
    canvas.setCursor(x + 10, y + 6);
    canvas.print('1');
  }
}

static void drawRadioPlayerInfo() {
  const int32_t w = M5.Display.width();
  const int32_t topH = infoAreaHeight();

  topCanvas.fillSprite(TFT_BLACK);
  topCanvas.setTextColor(TFT_WHITE, TFT_BLACK);
  topCanvas.setTextSize(1);

  if (wifiConfigPortalActive) {
    topCanvas.setFont(&fonts::Font2);
    topCanvas.setCursor(2, 2);
    topCanvas.print("AP: ");
    topCanvas.print(WIFI_SETUP_AP_SSID);
    topCanvas.setCursor(2, 22);
    topCanvas.print("PASS: ");
    topCanvas.print(WIFI_SETUP_AP_PASSWORD);
    topCanvas.setCursor(2, 42);
    topCanvas.print("OPEN: ");
    topCanvas.print(WiFi.softAPIP());
    topCanvas.pushSprite(0, 0);
    return;
  }

  if (wifiConnectionFailed) {
    topCanvas.setFont(&fonts::Font4);
    topCanvas.setCursor(2, 4);
    topCanvas.print("WI-FI NOT FOUND");
    topCanvas.setFont(&fonts::Font2);
    drawTextClipped(topCanvas, wifiSsid, 2, 35, w - 4, 18, &fonts::Font2,
                    kSpectrumBarCapColor);
    topCanvas.setCursor(2, 56);
    topCanvas.print("HOLD KEY1: SETUP");
    topCanvas.pushSprite(0, 0);
    return;
  }

  topCanvas.setFont(&fonts::lgfxJapanGothicP_20);
  topCanvas.setCursor(2, 6);
  topCanvas.print(currentStationName());

  drawScrollingText(topCanvas, programTitle, 2, 36, w - 4, topH - 37,
                    &fonts::lgfxJapanGothicP_16, &lastDrawnProgramTitle,
                    &titleScrollStartMs);

  if (millis() < volumeOverlayUntilMs) {
    drawVolumePopup(topCanvas);
  } else if (millis() < eqOverlayUntilMs) {
    drawEqPopup(topCanvas);
  }

  topCanvas.pushSprite(0, 0);
}

static void drawMp3PlayerInfo() {
  const int32_t w = M5.Display.width();
  const int32_t topH = infoAreaHeight();
  const String title = mp3Title.isEmpty() ? "曲名を取得中" : mp3Title;
  const String artist = mp3Artist.isEmpty() ? "アーティスト情報なし" : mp3Artist;

  topCanvas.fillSprite(TFT_BLACK);
  drawScrollingText(topCanvas, title, 2, 4, w - 4, 26, &fonts::lgfxJapanGothicP_20,
                    &lastDrawnMp3Title, &mp3TitleScrollStartMs);
  drawScrollingText(topCanvas, artist, 2, 38, w - 32, topH - 39,
                    &fonts::lgfxJapanGothicP_16, &lastDrawnMp3Artist,
                    &mp3ArtistScrollStartMs);
  drawLoopModeIcon(topCanvas, w - 27, 42, kSpectrumBarCapColor);

  if (millis() < volumeOverlayUntilMs) {
    drawVolumePopup(topCanvas);
  } else if (millis() < eqOverlayUntilMs) {
    drawEqPopup(topCanvas);
  }
  topCanvas.pushSprite(0, 0);
}

static void drawPlayerInfo() {
  if (appMode == AppMode::Mp3) {
    drawMp3PlayerInfo();
  } else {
    drawRadioPlayerInfo();
  }
}

static bool updateFftSpectrum(int32_t maxHeight) {
  if (millis() - lastRawPcmMs > 250) {
    for (size_t i = 0; i < kSpectrumBands; ++i) {
      spectrumLevels[i] *= 0.80f;
    }
    return false;
  }

  uint32_t sampleRate = audio.getSampleRate();
  if (sampleRate == 0) {
    sampleRate = 48000;
  }
  uint32_t delaySamples =
      (sampleRate * kSpectrumDelayMs + 500U) / 1000U;
  const uint32_t maxDelaySamples = kFftHistorySize - kFftSize;
  if (delaySamples > maxDelaySamples) {
    delaySamples = maxDelaySamples;
  }

  if (!fftReady || fftSampleCount < delaySamples + kFftSize || maxHeight <= 0) {
    for (size_t i = 0; i < kSpectrumBands; ++i) {
      spectrumLevels[i] *= 0.82f;
    }
    return false;
  }

  float mean = 0.0f;
  uint16_t writeIndex;
  portENTER_CRITICAL(&fftMux);
  writeIndex = fftWriteIndex;
  const uint16_t windowStart =
      (writeIndex - delaySamples - kFftSize) & (kFftHistorySize - 1);
  for (size_t i = 0; i < kFftSize; ++i) {
    const float sample = fftRing[(windowStart + i) & (kFftHistorySize - 1)];
    fftWork[i * 2] = sample;
    mean += sample;
  }
  portEXIT_CRITICAL(&fftMux);
  mean /= static_cast<float>(kFftSize);

  for (size_t i = 0; i < kFftSize; ++i) {
    fftWork[i * 2] = (fftWork[i * 2] - mean) * fftWindow[i];
    fftWork[i * 2 + 1] = 0.0f;
  }

  dsps_fft2r_fc32(fftWork, kFftSize);
  dsps_bit_rev_fc32(fftWork, kFftSize);
  dsps_cplx2reC_fc32(fftWork, kFftSize);

  float rms[kSpectrumBands] = {};
  float weightedRms[kSpectrumBands] = {};
  float framePeak = 0.0f;
  const float norm = 2.0f / static_cast<float>(kFftSize);
  static constexpr float kMinBin = 2.0f;
  static constexpr float kMaxBin = 124.0f;
  static constexpr float kLowRolloffBin = 5.5f;
  const float logMinBin = logf(kMinBin);
  const float logBinSpan = logf(kMaxBin) - logMinBin;
  uint8_t previousEnd = static_cast<uint8_t>(kMinBin);

  for (size_t band = 0; band < kSpectrumBands; ++band) {
    const float edge0 = expf(logMinBin + logBinSpan * static_cast<float>(band) /
                                             static_cast<float>(kSpectrumBands));
    const float edge1 = expf(logMinBin + logBinSpan * static_cast<float>(band + 1) /
                                             static_cast<float>(kSpectrumBands));
    uint8_t startBin = static_cast<uint8_t>(floorf(edge0));
    uint8_t endBin = static_cast<uint8_t>(ceilf(edge1));
    if (startBin < previousEnd) {
      startBin = previousEnd;
    }
    if (endBin <= startBin) {
      endBin = startBin + 1;
    }
    if (endBin > static_cast<uint8_t>(kMaxBin)) {
      endBin = static_cast<uint8_t>(kMaxBin);
    }
    previousEnd = endBin;

    float power = 0.0f;
    uint8_t count = 0;
    for (uint8_t bin = startBin; bin < endBin; ++bin) {
      const float re = fftWork[bin * 2];
      const float im = fftWork[bin * 2 + 1];
      const float mag = sqrtf(re * re + im * im) * norm;
      power += mag * mag;
      ++count;
    }
    rms[band] = count ? sqrtf(power / static_cast<float>(count)) : 0.0f;
    if (rms[band] > framePeak) {
      framePeak = rms[band];
    }

    const float centerBin = sqrtf(static_cast<float>(startBin) * static_cast<float>(endBin));
    const float lowRoll =
        (centerBin * centerBin) / (centerBin * centerBin + kLowRolloffBin * kLowRolloffBin);
    const float tilt = powf(centerBin / 24.0f, 0.18f);
    weightedRms[band] = rms[band] * lowRoll * tilt;
  }

  if (framePeak < 0.000001f || fftInputPeak < 0.002f) {
    for (size_t i = 0; i < kSpectrumBands; ++i) {
      spectrumLevels[i] *= 0.80f;
    }
    return false;
  }

  float activeRms[kSpectrumBands] = {};
  if (!spectrumNoiseReady) {
    for (size_t band = 0; band < kSpectrumBands; ++band) {
      spectrumNoiseFloor[band] = weightedRms[band] * 0.85f;
    }
    spectrumNoiseReady = true;
  }

  for (size_t band = 0; band < kSpectrumBands; ++band) {
    const float value = weightedRms[band];
    float floor = spectrumNoiseFloor[band];
    const float followRate = value < floor ? 0.16f : 0.006f;
    floor += (value - floor) * followRate;
    spectrumNoiseFloor[band] = floor;

    const float residual = value - floor * 1.18f;
    activeRms[band] = residual > 0.0f ? residual : 0.0f;
  }

  float top1 = 0.000001f;
  float top2 = 0.000001f;
  float top3 = 0.000001f;
  for (size_t band = 0; band < kSpectrumBands; ++band) {
    const float value = activeRms[band];
    if (value > top1) {
      top3 = top2;
      top2 = top1;
      top1 = value;
    } else if (value > top2) {
      top3 = top2;
      top2 = value;
    } else if (value > top3) {
      top3 = value;
    }
  }
  const float reference = (top1 + top2 + top3) / 3.0f;

  for (size_t band = 0; band < kSpectrumBands; ++band) {
    const float relative = activeRms[band] / reference;
    float db = 20.0f * log10f(relative + 0.000001f);
    if (db < -38.0f) {
      db = -38.0f;
    } else if (db > 3.0f) {
      db = 3.0f;
    }
    float normalized = (db + 38.0f) / 41.0f;
    normalized = powf(normalized, 0.82f);
    const float target = normalized * static_cast<float>(maxHeight);
    const float rate = target > spectrumLevels[band] ? 0.62f : 0.22f;
    spectrumLevels[band] += (target - spectrumLevels[band]) * rate;
  }
  return true;
}

static void drawSpectrum() {
  const int32_t w = M5.Display.width();
  const int32_t topH = infoAreaHeight();
  const int32_t y0 = topH;
  const int32_t graphH = spectrumAreaHeight();
  const char* status = uiStatusLabel();

  if (status[0] != '\0') {
    if (spectrumStatusDrawn && spectrumStatusMode == appMode &&
        lastDrawnSpectrumStatus == status) {
      return;
    }

    spectrumCanvas.fillSprite(TFT_BLACK);
    spectrumCanvas.setFont(&fonts::Font4);
    spectrumCanvas.setTextColor(TFT_WHITE, TFT_BLACK);
    spectrumCanvas.drawCentreString(status, w / 2, (graphH - 26) / 2);
    spectrumCanvas.pushSprite(0, y0);
    lastDrawnSpectrumStatus = status;
    spectrumStatusMode = appMode;
    spectrumStatusDrawn = true;
    return;
  }

  spectrumStatusDrawn = false;
  lastDrawnSpectrumStatus = "";
  spectrumCanvas.fillSprite(TFT_BLACK);
  updateFftSpectrum(graphH - 3);

  for (size_t i = 0; i < kSpectrumBands; ++i) {
    const int32_t barH = static_cast<int32_t>(spectrumLevels[i] + 0.5f);
    const int32_t cellX0 = (static_cast<int32_t>(i) * w) / static_cast<int32_t>(kSpectrumBands);
    const int32_t cellX1 =
        (static_cast<int32_t>(i + 1) * w) / static_cast<int32_t>(kSpectrumBands);
    const int32_t x = cellX0 + 1;
    int32_t bandW = cellX1 - cellX0 - 2;
    if (bandW < 1) {
      bandW = 1;
    }
    const int32_t y = graphH - 2 - barH;
    spectrumCanvas.fillRect(x, y, bandW, barH, kSpectrumBarColor);
    if (barH > 5) {
      spectrumCanvas.fillRect(x, y + 1, bandW, 1, TFT_BLACK);
    }
    if (barH > 0) {
      spectrumCanvas.fillRect(x, y, bandW, 1, kSpectrumBarCapColor);
    }
  }

  spectrumCanvas.pushSprite(0, y0);
}

static void formatPlaybackTime(uint32_t seconds, char* out, size_t outSize) {
  const uint32_t minutes = seconds / 60;
  snprintf(out, outSize, "%02u:%02u", static_cast<unsigned>(minutes),
           static_cast<unsigned>(seconds % 60));
}

static uint32_t mp3PlaybackSeconds() {
  const uint32_t sampleRate = audio.getSampleRate();
  const uint32_t samples = pcmSamples;
  if (sampleRate >= 8000 && sampleRate <= 192000) {
    return samples / sampleRate;
  }
  return audio.getAudioCurrentTime();
}

static uint32_t stableMp3Duration(uint32_t current) {
  if (mp3DisplayDuration > 0) {
    return mp3DisplayDuration;
  }

  const uint32_t estimate = audio.getAudioFileDuration();
  if (current < 2 || estimate <= current) {
    return 0;
  }

  const uint32_t tolerance = std::max<uint32_t>(2, estimate / 100);
  const uint32_t difference = estimate > mp3DurationCandidate
                                  ? estimate - mp3DurationCandidate
                                  : mp3DurationCandidate - estimate;
  if (mp3DurationCandidate == 0 || difference > tolerance) {
    mp3DurationCandidate = estimate;
    mp3DurationCandidateSinceMs = millis();
    return 0;
  }

  mp3DurationCandidate =
      static_cast<uint32_t>((static_cast<uint64_t>(mp3DurationCandidate) * 3 + estimate) / 4);
  if (millis() - mp3DurationCandidateSinceMs >= 1000) {
    mp3DisplayDuration = std::max(mp3DurationCandidate, current + 1);
  }
  return mp3DisplayDuration;
}

static void drawMp3Progress() {
  const int32_t w = M5.Display.width();
  const int32_t y0 = infoAreaHeight();
  const int32_t h = spectrumAreaHeight();
  const uint32_t current = mp3PlaybackSeconds();
  const uint32_t duration = stableMp3Duration(current);
  const uint32_t remaining = duration > current ? duration - current : 0;
  const int32_t barX = 6;
  const int32_t barY = 25;
  const int32_t barW = w - 12;
  const int32_t barH = 8;

  spectrumStatusDrawn = false;
  lastDrawnSpectrumStatus = "";
  spectrumCanvas.fillSprite(TFT_BLACK);
  const char* status = uiStatusLabel();
  if (status[0] != '\0' && (!sdMounted || mp3Tracks.empty())) {
    spectrumCanvas.setFont(&fonts::Font4);
    spectrumCanvas.setTextColor(TFT_WHITE, TFT_BLACK);
    spectrumCanvas.drawCentreString(status, w / 2, (h - 26) / 2);
    spectrumCanvas.pushSprite(0, y0);
    return;
  }

  if (mp3Paused) {
    spectrumCanvas.fillRect(6, 4, 4, 14, kSpectrumBarCapColor);
    spectrumCanvas.fillRect(14, 4, 4, 14, kSpectrumBarCapColor);
  } else {
    spectrumCanvas.fillTriangle(7, 3, 7, 19, 20, 11, kSpectrumBarCapColor);
  }

  char trackLabel[16];
  snprintf(trackLabel, sizeof(trackLabel), "%u/%u", static_cast<unsigned>(mp3TrackIndex + 1),
           static_cast<unsigned>(mp3Tracks.size()));
  spectrumCanvas.setFont(&fonts::Font2);
  spectrumCanvas.setTextColor(kSpectrumBarCapColor, TFT_BLACK);
  spectrumCanvas.setCursor(28, 3);
  spectrumCanvas.print(trackLabel);

  spectrumCanvas.drawRect(barX, barY, barW, barH, 0x7BEF);
  if (duration > 0) {
    int32_t fillW =
        static_cast<int32_t>((static_cast<uint64_t>(barW - 2) * current) / duration);
    if (fillW > barW - 2) {
      fillW = barW - 2;
    }
    spectrumCanvas.fillRect(barX + 1, barY + 1, fillW, barH - 2, kSpectrumBarColor);
  }

  char currentText[12];
  char remainingText[13];
  formatPlaybackTime(current, currentText, sizeof(currentText));
  if (duration > 0) {
    char timeValue[11];
    formatPlaybackTime(remaining, timeValue, sizeof(timeValue));
    snprintf(remainingText, sizeof(remainingText), "-%s", timeValue);
  } else {
    snprintf(remainingText, sizeof(remainingText), "--:--");
  }
  spectrumCanvas.setTextColor(TFT_WHITE, TFT_BLACK);
  spectrumCanvas.setCursor(6, 41);
  spectrumCanvas.print(currentText);
  const int32_t remainingW = spectrumCanvas.textWidth(remainingText);
  spectrumCanvas.setCursor(w - 6 - remainingW, 41);
  spectrumCanvas.print(remainingText);
  spectrumCanvas.pushSprite(0, y0);
}

static void renderUi() {
  const uint32_t now = millis();
  if (!uiNeedsFullRedraw && now - lastUiUpdateMs < 80) {
    return;
  }
  lastUiUpdateMs = now;

  if (uiNeedsFullRedraw) {
    uiNeedsFullRedraw = false;
  }
  drawPlayerInfo();
  if (appMode == AppMode::Mp3 && mp3LowerView == Mp3LowerView::Progress) {
    drawMp3Progress();
  } else {
    drawSpectrum();
  }
}

static bool playbackOutputActive() {
  if (!playbackSamplesSeen) {
    return false;
  }
  if (appMode == AppMode::Mp3) {
    return sdMounted && !mp3Paused && currentStatus == "Playing";
  }
  return currentStatus == "Playing" || currentStatus == "Stream opened";
}

static void updateAudioOutputGate(bool force) {
  const bool standby = usbPowerPresent && !playbackOutputActive();
  const bool muteOutput = audioMuted || standby;
  const bool changed = standby != usbIdleStandby || muteOutput != audioOutputMuted;
  usbIdleStandby = standby;
  audioOutputMuted = muteOutput;

  if (!audioOutputReady || (!force && !changed)) {
    return;
  }

  audio.setMute(muteOutput);
  const char* reason = audioMuted ? "user" : (standby ? "usb-idle" : "playback");
  Serial.printf("[Audio] output=%s reason=%s\n", muteOutput ? "MUTED" : "ACTIVE", reason);
}

static void updateUsbPowerState(bool force) {
  const uint32_t now = millis();
  if (!force && now - lastUsbPowerPollMs < kUsbPowerPollIntervalMs) {
    return;
  }
  lastUsbPowerPollMs = now;

  const int16_t vbusMv = M5.Power.getVBUSVoltage();
  if (vbusMv < 0) {
    if (force) {
      Serial.println("[Power] VBUS measurement unsupported");
    }
    return;
  }

  bool present;
  if (!usbPowerKnown) {
    present = vbusMv >= kUsbPresentVoltageMv;
  } else if (usbPowerPresent) {
    present = vbusMv > kUsbAbsentVoltageMv;
  } else {
    present = vbusMv >= kUsbPresentVoltageMv;
  }

  const bool changed = !usbPowerKnown || present != usbPowerPresent;
  usbPowerKnown = true;
  usbPowerPresent = present;
  if (force || changed) {
    Serial.printf("[Power] VBUS=%dmV usb=%u\n", vbusMv, present ? 1 : 0);
  }
  updateAudioOutputGate();
}

static void disableWiFiForMp3() {
  bool stopped = true;
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    stopped = WiFi.disconnect(true, false, 1000);
    if (WiFi.getMode() != WIFI_MODE_NULL) {
      stopped = WiFi.mode(WIFI_OFF) && stopped;
    }
  }
  Serial.printf("[WiFi] MP3 radio %s mode=%d\n", stopped ? "OFF" : "OFF failed",
                static_cast<int>(WiFi.getMode()));
}

static void applyVolume() {
  audio.setVolume(volumeLevel);
  Serial.printf("[Audio] volume=%u/%u mute=%u\n", volumeLevel, kMaximumVolume,
                audioMuted ? 1 : 0);
  volumeOverlayUntilMs = millis() + 2000;
  eqOverlayUntilMs = 0;
  volumeChangedAtMs = millis();
  volumeSavePending = true;
}

static void adjustVolume(int delta) {
  int next = static_cast<int>(volumeLevel) + delta;
  if (next < 0) {
    next = 0;
  } else if (next > kMaximumVolume) {
    next = kMaximumVolume;
  }
  if (next == volumeLevel) {
    return;
  }
  volumeLevel = static_cast<uint8_t>(next);
  applyVolume();
}

static void toggleMute() {
  audioMuted = !audioMuted;
  updateAudioOutputGate();
  Serial.printf("[Audio] mute=%u\n", audioMuted ? 1 : 0);
  volumeOverlayUntilMs = millis() + 2000;
  eqOverlayUntilMs = 0;
}

static void applyEqPreset(bool save) {
  const EqPresetSetting& setting = kEqPresets[static_cast<uint8_t>(eqPreset)];
  audio.setTone(setting.lowGain, setting.midGain, setting.highGain);
  Serial.printf("[EQ] preset=%s low=%.1f mid=%.1f high=%.1f\n", setting.name,
                setting.lowGain, setting.midGain, setting.highGain);

  if (save) {
    preferences.begin("pocket-audio", false);
    preferences.putUChar("eq", static_cast<uint8_t>(eqPreset));
    preferences.end();
  }
}

static void cycleEqPreset() {
  const uint8_t next =
      (static_cast<uint8_t>(eqPreset) + 1) % static_cast<uint8_t>(EqPreset::Count);
  eqPreset = static_cast<EqPreset>(next);
  applyEqPreset(true);
  volumeOverlayUntilMs = 0;
  eqOverlayUntilMs = millis() + 2000;
}

static void resetAudioVisualization() {
  playbackSamplesSeen = false;
  updateAudioOutputGate();
  fftInputGainReady = false;
  fftInputGain = 1.0f;
  fftInputPeak = 0.0f;
  pcmBlocks = 0;
  pcmSamples = 0;
  pcmPeak = 0;
  lastRawPcmMs = 0;
  portENTER_CRITICAL(&fftMux);
  fftWriteIndex = 0;
  fftSampleCount = 0;
  for (size_t i = 0; i < kFftHistorySize; ++i) {
    fftRing[i] = 0.0f;
  }
  portEXIT_CRITICAL(&fftMux);
  for (size_t i = 0; i < kSpectrumBands; ++i) {
    spectrumLevels[i] = 0;
    spectrumNoiseFloor[i] = 0;
  }
  spectrumNoiseReady = false;
}

static bool isMp3Path(String path) {
  path.toLowerCase();
  return path.endsWith(".mp3") && !path.startsWith("/._") && path.indexOf("/._") < 0;
}

static String mp3FileTitle(const String& path) {
  int start = path.lastIndexOf('/');
  String name = start >= 0 ? path.substring(start + 1) : path;
  const int extension = name.lastIndexOf('.');
  if (extension > 0) {
    name.remove(extension);
  }

  if (name.startsWith("maou_")) {
    const int idEnd = name.indexOf('_', 5);
    if (idEnd > 5 && idEnd + 1 < static_cast<int>(name.length())) {
      name = name.substring(idEnd + 1);
    }
  }
  name.replace('_', ' ');
  bool startOfWord = true;
  for (size_t i = 0; i < name.length(); ++i) {
    char c = name[i];
    if (c >= 'a' && c <= 'z' && startOfWord) {
      name.setCharAt(i, static_cast<char>(c - ('a' - 'A')));
    }
    startOfWord = c == ' ';
  }
  return name;
}

static const Mp3TrackMetadata* findMp3TrackMetadata(const String& path) {
  int start = path.lastIndexOf('/');
  String name = start >= 0 ? path.substring(start + 1) : path;
  if (!name.startsWith("maou_")) {
    return nullptr;
  }
  const int idEnd = name.indexOf('_', 5);
  if (idEnd <= 5) {
    return nullptr;
  }
  const String id = name.substring(5, idEnd);
  for (size_t i = 0; i < kMp3TrackMetadataCount; ++i) {
    if (id == kMp3TrackMetadata[i].id) {
      return &kMp3TrackMetadata[i];
    }
  }
  return nullptr;
}

static void scanMp3Directory(const char* path, uint8_t depth) {
  File directory = SD.open(path);
  if (!directory || !directory.isDirectory()) {
    return;
  }

  File entry = directory.openNextFile();
  while (entry) {
    const String entryPath = entry.path();
    if (entry.isDirectory()) {
      if (depth > 0 && !entryPath.endsWith("/System Volume Information")) {
        scanMp3Directory(entryPath.c_str(), depth - 1);
      }
    } else if (isMp3Path(entryPath)) {
      mp3Tracks.push_back(entryPath);
    }
    entry.close();
    entry = directory.openNextFile();
  }
  directory.close();
}

static bool mountAndScanSd(bool reportFailure = true) {
  if (!sdMounted) {
    if (!sdBusStarted) {
      pinMode(SD_CS, OUTPUT);
      digitalWrite(SD_CS, HIGH);
      sdSpi.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
      sdBusStarted = true;
    }
    sdMounted = SD.begin(SD_CS, sdSpi, 20000000, "/sd", 8);
    lastSdMountAttemptMs = millis();
    if (!sdMounted) {
      if (reportFailure) {
        Serial.println("[SD] mount failed; waiting for card");
      }
      currentStatus = "SD error";
      if (reportFailure) {
        uiNeedsFullRedraw = true;
      }
      return false;
    }
    lastSdPresenceCheckMs = millis();
    Serial.printf("[SD] mounted size=%llu MB\n",
                  static_cast<unsigned long long>(SD.cardSize() / (1024ULL * 1024ULL)));
  }

  mp3Tracks.clear();
  scanMp3Directory("/", 5);
  std::sort(mp3Tracks.begin(), mp3Tracks.end(),
            [](const String& lhs, const String& rhs) {
              return strcasecmp(lhs.c_str(), rhs.c_str()) < 0;
            });
  Serial.printf("[MP3] found %u files\n", static_cast<unsigned>(mp3Tracks.size()));
  for (size_t i = 0; i < mp3Tracks.size(); ++i) {
    Serial.printf("[MP3] %3u %s\n", static_cast<unsigned>(i + 1), mp3Tracks[i].c_str());
  }
  if (mp3Tracks.empty()) {
    currentStatus = "No MP3 files";
    uiNeedsFullRedraw = true;
    return false;
  }
  if (mp3TrackIndex >= mp3Tracks.size()) {
    mp3TrackIndex = 0;
  }
  return true;
}

static bool isSdCardReadable() {
  if (!sdMounted) {
    return false;
  }

  if (!mp3Tracks.empty() && mp3TrackIndex < mp3Tracks.size()) {
    File probe = SD.open(mp3Tracks[mp3TrackIndex], FILE_READ);
    if (!probe) {
      return false;
    }
    const int value = probe.read();
    probe.close();
    return value >= 0;
  }

  File root = SD.open("/");
  const bool readable = root && root.isDirectory();
  root.close();
  return readable;
}

static void handleSdCardRemoved() {
  Serial.println("[SD] card removed");
  audio.stopSong();
  mp3EofPending = false;
  resetAudioVisualization();
  SD.end();
  sdMounted = false;
  mp3Tracks.clear();
  mp3Paused = false;
  mp3DisplayDuration = 0;
  mp3DurationCandidate = 0;
  mp3DurationCandidateSinceMs = 0;
  mp3Title = "";
  mp3Artist = "";
  lastDrawnMp3Title = "";
  lastDrawnMp3Artist = "";
  portENTER_CRITICAL(&mp3MetadataMux);
  pendingMp3MetadataMask = 0;
  pendingMp3Title[0] = '\0';
  pendingMp3Artist[0] = '\0';
  portEXIT_CRITICAL(&mp3MetadataMux);
  currentStatus = "SD error";
  lastSdMountAttemptMs = millis();
  uiNeedsFullRedraw = true;
}

static void handleSdHotplug() {
  if (appMode != AppMode::Mp3) {
    return;
  }

  const uint32_t now = millis();
  if (sdMounted) {
    if (now - lastSdPresenceCheckMs < kSdPresenceCheckIntervalMs) {
      return;
    }
    lastSdPresenceCheckMs = now;
    if (!isSdCardReadable()) {
      handleSdCardRemoved();
    }
    return;
  }

  if (now - lastSdMountAttemptMs < kSdMountRetryIntervalMs) {
    return;
  }
  if (!mountAndScanSd(false)) {
    return;
  }

  Serial.println("[SD] card detected; starting MP3 playback");
  if (!playMp3Track(mp3TrackIndex)) {
    currentStatus = "MP3 error";
    uiNeedsFullRedraw = true;
  }
}

static bool playMp3Track(size_t index) {
  if (!sdMounted || mp3Tracks.empty() || index >= mp3Tracks.size()) {
    return false;
  }

  audio.stopSong();
  resetAudioVisualization();
  mp3TrackIndex = index;
  mp3Paused = false;
  mp3EofPending = false;
  mp3DisplayDuration = 0;
  mp3DurationCandidate = 0;
  mp3DurationCandidateSinceMs = 0;
  const Mp3TrackMetadata* metadata = findMp3TrackMetadata(mp3Tracks[index]);
  mp3UsesKnownMetadata = metadata != nullptr;
  mp3Title = metadata ? metadata->title : mp3FileTitle(mp3Tracks[index]);
  mp3Artist = metadata ? metadata->artist : "";
  lastDrawnMp3Title = "";
  lastDrawnMp3Artist = "";
  memset(pendingMp3Title, 0, sizeof(pendingMp3Title));
  memset(pendingMp3Artist, 0, sizeof(pendingMp3Artist));
  pendingMp3MetadataMask = 0;
  currentStatus = "MP3 loading";
  uiNeedsFullRedraw = true;
  renderUi();

  audio.setVolume(volumeLevel);
  updateAudioOutputGate(true);
  Serial.printf("[MP3] play %u/%u %s\n", static_cast<unsigned>(index + 1),
                static_cast<unsigned>(mp3Tracks.size()), mp3Tracks[index].c_str());
  if (!audio.connecttoFS(SD, mp3Tracks[index].c_str())) {
    Serial.println("[MP3] connecttoFS failed");
    currentStatus = "MP3 error";
    uiNeedsFullRedraw = true;
    return false;
  }
  currentStatus = "Playing";
  return true;
}

static void playNextMp3(bool manual) {
  if (mp3Tracks.empty()) {
    return;
  }
  size_t next = mp3TrackIndex;
  if (!manual && mp3LoopMode == Mp3LoopMode::One) {
    next = mp3TrackIndex;
  } else if (!manual && mp3LoopMode == Mp3LoopMode::Shuffle && mp3Tracks.size() > 1) {
    do {
      next = static_cast<size_t>(esp_random() % mp3Tracks.size());
    } while (next == mp3TrackIndex);
  } else {
    next = (mp3TrackIndex + 1) % mp3Tracks.size();
  }
  playMp3Track(next);
}

static void playPreviousMp3() {
  if (mp3Tracks.empty()) {
    return;
  }
  const size_t previous = (mp3TrackIndex + mp3Tracks.size() - 1) % mp3Tracks.size();
  playMp3Track(previous);
}

static void toggleMp3Pause() {
  if (mp3Tracks.empty()) {
    return;
  }
  if (audio.pauseResume()) {
    mp3Paused = !mp3Paused;
    currentStatus = mp3Paused ? "Paused" : "Playing";
    updateAudioOutputGate();
    Serial.printf("[MP3] %s\n", mp3Paused ? "paused" : "resumed");
    uiNeedsFullRedraw = true;
  }
}

static void cycleMp3LoopMode() {
  mp3LoopMode = static_cast<Mp3LoopMode>(
      (static_cast<uint8_t>(mp3LoopMode) + 1) % 3);
  static const char* names[] = {"one", "all", "shuffle"};
  Serial.printf("[MP3] loop=%s\n", names[static_cast<uint8_t>(mp3LoopMode)]);
}

static void processMp3AudioEvents() {
  if (appMode != AppMode::Mp3) {
    return;
  }

  if (pendingMp3MetadataMask != 0) {
    char title[sizeof(pendingMp3Title)];
    char artist[sizeof(pendingMp3Artist)];
    uint8_t metadataMask = 0;
    portENTER_CRITICAL(&mp3MetadataMux);
    strlcpy(title, pendingMp3Title, sizeof(title));
    strlcpy(artist, pendingMp3Artist, sizeof(artist));
    metadataMask = pendingMp3MetadataMask;
    pendingMp3MetadataMask = 0;
    portEXIT_CRITICAL(&mp3MetadataMux);
    if (!mp3UsesKnownMetadata && (metadataMask & kMp3MetadataTitle) != 0) {
      String value(title);
      value.trim();
      if (!value.isEmpty()) {
        mp3Title = value;
        Serial.printf("[MP3] ID3 title=%s\n", mp3Title.c_str());
      }
    }
    if (!mp3UsesKnownMetadata && (metadataMask & kMp3MetadataArtist) != 0) {
      String value(artist);
      value.trim();
      if (!value.isEmpty()) {
        mp3Artist = value;
        Serial.printf("[MP3] ID3 artist=%s\n", mp3Artist.c_str());
      }
    }
  }

  if (mp3EofPending) {
    mp3EofPending = false;
    Serial.println("[MP3] eof");
    playNextMp3(false);
  }
}

static void switchAppMode(AppMode mode) {
  pendingAppMode = -1;
  if (appMode == mode) {
    return;
  }

  audio.stopSong();
  resetAudioVisualization();
  appMode = mode;
  saveAppMode();
  uiNeedsFullRedraw = true;
  if (mode == AppMode::Mp3) {
    disableWiFiForMp3();
    Serial.println("[Mode] MP3");
    currentStatus = "MP3 loading";
    renderUi();
    if (sdMounted && !isSdCardReadable()) {
      handleSdCardRemoved();
    }
    if (mountAndScanSd()) {
      playMp3Track(mp3TrackIndex);
    }
    return;
  }

  Serial.println("[Mode] RADIO");
  currentStatus = "Station selected";
  renderUi();
  if (WiFi.status() != WL_CONNECTED && !connectWiFi()) {
    return;
  }
  tuneStation(stationIndex);
}

static void updateDebouncedInput(DebouncedInput* input, bool raw, uint32_t now) {
  input->pressed = false;
  input->released = false;
  if (raw != input->candidate) {
    input->candidate = raw;
    input->changedAtMs = now;
  }
  if (input->stable != input->candidate && now - input->changedAtMs >= 25) {
    input->stable = input->candidate;
    input->pressed = input->stable;
    input->released = !input->stable;
  }
}

static MediaKey readMediaKey() {
  const int value = analogRead(MEDIA_KEY_ADC);
  if (value < 700) {
    return MediaKey::Next;
  }
  if (value < 3000) {
    return MediaKey::Previous;
  }
  return MediaKey::None;
}

static void handleMediaKey(uint32_t now) {
  const MediaKey raw = readMediaKey();
  if (raw != mediaKeyCandidate) {
    mediaKeyCandidate = raw;
    mediaKeyChangedAtMs = now;
  }
  if (mediaKeyStable == mediaKeyCandidate || now - mediaKeyChangedAtMs < 30) {
    return;
  }
  mediaKeyStable = mediaKeyCandidate;
  if (mediaKeyStable == MediaKey::None) {
    return;
  }

  Serial.printf("[Input] %s adc=%d\n",
                mediaKeyStable == MediaKey::Next ? "SW1 next" : "SW2 previous",
                analogRead(MEDIA_KEY_ADC));
  if (appMode == AppMode::Radio) {
    if (mediaKeyStable == MediaKey::Previous) {
      requestPreviousStation();
    } else {
      requestNextStation();
    }
  } else if (mediaKeyStable == MediaKey::Previous) {
    playPreviousMp3();
  } else {
    playNextMp3(true);
  }
}

static void handlePeripheralControls() {
  const uint32_t now = millis();
  handleMediaKey(now);
  updateDebouncedInput(&volumeUpInput, digitalRead(VOLUME_UP_PIN) == LOW, now);
  updateDebouncedInput(&volumeDownInput, digitalRead(VOLUME_DOWN_PIN) == LOW, now);
  updateDebouncedInput(&volumeMuteInput, digitalRead(VOLUME_MUTE_PIN) == LOW, now);

  if (volumeUpInput.pressed) {
    Serial.println("[Input] volume up");
    adjustVolume(1);
    lastVolumeRepeatMs = now;
  } else if (volumeDownInput.pressed) {
    Serial.println("[Input] volume down");
    adjustVolume(-1);
    lastVolumeRepeatMs = now;
  } else if (volumeUpInput.stable && !volumeDownInput.stable &&
             now - lastVolumeRepeatMs >= 300) {
    adjustVolume(1);
    lastVolumeRepeatMs = now;
  } else if (volumeDownInput.stable && !volumeUpInput.stable &&
             now - lastVolumeRepeatMs >= 300) {
    adjustVolume(-1);
    lastVolumeRepeatMs = now;
  }

  if (volumeMuteInput.pressed) {
    Serial.println("[Input] mute");
    toggleMute();
  }
  maybeSaveVolume();
}

static void handleM5Keys() {
  const bool key1Pressed = M5.BtnA.isPressed();
  const bool key2Pressed = M5.BtnB.isPressed();
  if (key1Pressed && key2Pressed) {
    if (!keyChordActive) {
      keyChordActive = true;
      Serial.println("[Input] KEY1+KEY2 EQ preset");
      cycleEqPreset();
    }
    return;
  }
  if (keyChordActive) {
    if (!key1Pressed && !key2Pressed) {
      keyChordActive = false;
    }
    return;
  }

  if (M5.BtnB.wasHold()) {
    const AppMode next = appMode == AppMode::Radio ? AppMode::Mp3 : AppMode::Radio;
    pendingAppMode = static_cast<int>(next);
  } else if (M5.BtnB.wasClicked() && appMode == AppMode::Mp3) {
    cycleMp3LoopMode();
  }

  if (appMode == AppMode::Radio) {
    if (M5.BtnA.wasHold()) {
      if (wifiConfigPortalActive) {
        Serial.println("[Input] KEY1 exit WiFi setup");
        wifiConfigPortalExitRequested = true;
      } else {
        Serial.println("[Input] KEY1 WiFi setup");
        wifiConfigPortalRequested = true;
        tuneAbortRequested = tuningBusy;
      }
    }
    return;
  }

  if (appMode != AppMode::Mp3) {
    return;
  }
  if (M5.BtnA.wasHold()) {
    mp3LowerView = mp3LowerView == Mp3LowerView::Progress ? Mp3LowerView::Spectrum
                                                         : Mp3LowerView::Progress;
    Serial.printf("[MP3] view=%s\n",
                  mp3LowerView == Mp3LowerView::Progress ? "progress" : "spectrum");
    uiNeedsFullRedraw = true;
  } else if (M5.BtnA.wasClicked()) {
    toggleMp3Pause();
  }
}

static bool playStream();

static void printStationList() {
  Serial.println("[Station] list");
  for (size_t i = 0; i < kStationCount; ++i) {
    Serial.printf("[Station] %2u: %s%s\n", static_cast<unsigned>(i + 1), kStations[i].name,
                  i == stationIndex ? "  <==" : "");
  }
}

static bool tuneStation(size_t index) {
  if (index >= kStationCount) {
    return false;
  }

  if (stationIndex != index) {
    selectStationImmediately(index);
  }

  tuningBusy = true;
  tuneAbortRequested = false;
  pendingTuneIndex = -1;

  if (WiFi.status() != WL_CONNECTED) {
    tuningBusy = false;
    return false;
  }
  const bool ok = playStream();
  tuningBusy = false;
  return ok;
}

static bool tuneNextStation() {
  requestNextStation();
  return true;
}

static bool tunePreviousStation() {
  requestPreviousStation();
  return true;
}

static String htmlEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 16);
  for (size_t i = 0; i < value.length(); ++i) {
    switch (value[i]) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '\"':
        escaped += "&quot;";
        break;
      default:
        escaped += value[i];
        break;
    }
  }
  return escaped;
}

static String wifiConfigPage(const String& message = "") {
  String page;
  page.reserve(1800);
  page += F("<!doctype html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Pocket Audio Deck Wi-Fi</title><style>"
            "body{font-family:system-ui,sans-serif;max-width:420px;margin:32px auto;padding:0 18px;"
            "background:#111;color:#eee}h1{font-size:24px}label{display:block;margin-top:18px}"
            "input{box-sizing:border-box;width:100%;padding:12px;margin-top:6px;font-size:16px}"
            "button{width:100%;margin-top:24px;padding:13px;font-size:16px;font-weight:700}"
            ".message{padding:12px;background:#333}</style></head><body>"
            "<h1>Pocket Audio Deck</h1><p>Wi-Fi settings</p>");
  if (!message.isEmpty()) {
    page += "<p class='message'>";
    page += htmlEscape(message);
    page += "</p>";
  }
  page += F("<form method='post' action='/save'><label>SSID"
            "<input name='ssid' maxlength='32' required value='");
  page += htmlEscape(wifiSsid);
  page += F("'></label><label>Password"
            "<input name='password' type='password' maxlength='63' autocomplete='new-password'>"
            "</label><button type='submit'>SAVE AND CONNECT</button></form></body></html>");
  return page;
}

static void redirectToWifiConfig() {
  wifiConfigServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  wifiConfigServer.sendHeader("Location", "http://192.168.4.1/", true);
  wifiConfigServer.send(302, "text/plain", "");
}

static void configureWifiPortalRoutes() {
  if (wifiConfigRoutesReady) {
    return;
  }
  wifiConfigRoutesReady = true;

  wifiConfigServer.on("/", HTTP_GET, []() {
    wifiConfigServer.send(200, "text/html; charset=utf-8", wifiConfigPage());
  });
  wifiConfigServer.on("/generate_204", HTTP_ANY, redirectToWifiConfig);
  wifiConfigServer.on("/gen_204", HTTP_ANY, redirectToWifiConfig);
  wifiConfigServer.on("/canonical.html", HTTP_ANY, redirectToWifiConfig);
  wifiConfigServer.on("/success.txt", HTTP_ANY, redirectToWifiConfig);
  wifiConfigServer.on("/connecttest.txt", HTTP_ANY, redirectToWifiConfig);
  wifiConfigServer.on("/ncsi.txt", HTTP_ANY, redirectToWifiConfig);
  wifiConfigServer.on("/redirect", HTTP_ANY, redirectToWifiConfig);
  wifiConfigServer.on("/connectivity-check.html", HTTP_ANY, redirectToWifiConfig);
  wifiConfigServer.on("/library/test/success.html", HTTP_ANY, redirectToWifiConfig);
  wifiConfigServer.on("/fwlink", HTTP_ANY, redirectToWifiConfig);
  wifiConfigServer.on("/hotspot-detect.html", HTTP_ANY, []() {
    wifiConfigServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    wifiConfigServer.send(200, "text/html; charset=utf-8", wifiConfigPage());
  });
  wifiConfigServer.on("/save", HTTP_POST, []() {
    String ssid = wifiConfigServer.arg("ssid");
    const String password = wifiConfigServer.arg("password");
    ssid.trim();
    if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63 ||
        (!password.isEmpty() && password.length() < 8)) {
      wifiConfigServer.send(
          400, "text/html; charset=utf-8",
          wifiConfigPage("SSID or password is invalid. Password must be blank or 8-63 characters."));
      return;
    }

    preferences.begin("pocket-audio", false);
    preferences.putString("wifi_ssid", ssid);
    preferences.putString("wifi_pass", password);
    preferences.end();
    wifiSsid = ssid;
    wifiPassword = password;
    wifiCredentialsSubmitted = true;
    Serial.printf("[WiFi Setup] saved SSID '%s'\n", wifiSsid.c_str());
    wifiConfigServer.send(
        200, "text/html; charset=utf-8",
        wifiConfigPage("Saved. Pocket Audio Deck is connecting to the new network."));
  });
  wifiConfigServer.onNotFound(redirectToWifiConfig);
}

static void drawWifiConfigQrScreen() {
  const int32_t qrSize = M5.Display.height();
  const String wifiQr = String("WIFI:T:WPA;S:") + WIFI_SETUP_AP_SSID + ";P:" +
                        WIFI_SETUP_AP_PASSWORD + ";;";
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.qrcode(wifiQr.c_str(), 0, 0, qrSize, 5, true);

  const int32_t x = qrSize + 8;
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setCursor(x, 5);
  M5.Display.print("WI-FI SETUP");
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setCursor(x, 31);
  M5.Display.print("SCAN TO JOIN");
  M5.Display.setCursor(x, 45);
  M5.Display.print("PocketAudioDeck-");
  M5.Display.setCursor(x, 57);
  M5.Display.print("Setup");
  M5.Display.setCursor(x, 81);
  M5.Display.print("PORTAL OPENS");
  M5.Display.setCursor(x, 93);
  M5.Display.print("AUTOMATICALLY");
  M5.Display.setCursor(x, 115);
  M5.Display.print("192.168.4.1");
}

static void startWifiConfigPortal() {
  const uint32_t startedAtMs = millis();
  wifiConfigPortalRequested = false;
  wifiConfigPortalExitRequested = false;
  wifiCredentialsSubmitted = false;
  resetAudioVisualization();
  WiFi.disconnect(true, false);
  audio.stopSong();
  WiFi.mode(WIFI_AP);
  const IPAddress apIp(192, 168, 4, 1);
  const IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIp, apIp, subnet);

  if (!WiFi.softAP(WIFI_SETUP_AP_SSID, WIFI_SETUP_AP_PASSWORD)) {
    Serial.println("[WiFi Setup] AP start failed");
    wifiConnectionFailed = true;
    showStatus("WiFi setup failed");
    return;
  }

  configureWifiPortalRoutes();
  wifiConfigDns.start(53, "*", WiFi.softAPIP());
  wifiConfigServer.begin();
  wifiConfigPortalActive = true;
  wifiConnectionFailed = false;
  currentStatus = "WiFi setup";
  spectrumStatusDrawn = false;
  drawWifiConfigQrScreen();
  updateAudioOutputGate(true);
  Serial.printf("[WiFi Setup] AP='%s' IP=%s ready=%ums\n", WIFI_SETUP_AP_SSID,
                WiFi.softAPIP().toString().c_str(),
                static_cast<unsigned>(millis() - startedAtMs));
}

static void serviceWifiConfigPortal() {
  wifiConfigDns.processNextRequest();
  wifiConfigServer.handleClient();
  if (!wifiCredentialsSubmitted && !wifiConfigPortalExitRequested) {
    return;
  }

  const bool cancelled = wifiConfigPortalExitRequested;
  wifiCredentialsSubmitted = false;
  wifiConfigPortalExitRequested = false;
  wifiConfigServer.stop();
  wifiConfigDns.stop();
  WiFi.softAPdisconnect(true);
  wifiConfigPortalActive = false;
  spectrumStatusDrawn = false;
  uiNeedsFullRedraw = true;
  tuneAbortRequested = false;
  delay(100);
  Serial.printf("[WiFi Setup] %s, reconnecting\n", cancelled ? "closed by KEY1" : "saved");
  if (connectWiFi()) {
    playStream();
  }
}

static bool connectWiFi() {
  wifiConnectionFailed = false;
  showStatus("WiFi connecting");
  Serial.printf("[WiFi] connecting to SSID '%s'\n", wifiSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());

  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < 20000) {
    delay(250);
    Serial.print(".");
    if (pollUserControlsDuringTuning()) {
      Serial.println();
      Serial.println("[WiFi] aborted by user");
      return false;
    }
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[WiFi] failed, status=%d\n", WiFi.status());
    wifiConnectionFailed = true;
    showStatus("WiFi failed");
    return false;
  }

  Serial.printf("[WiFi] connected, ip=%s\n", WiFi.localIP().toString().c_str());
  showStatus("WiFi connected");
  return true;
}

static bool playStream() {
  const char* url = activeStreamUrl();
  if (!url) {
    Serial.println("[Stream] no stream URL resolved");
    showStatus("No stream URL");
    return false;
  }
  showStatus("Stream connecting");
  Serial.printf("[Stream] connecting: %s\n", url);

  streamReadyShown = false;
  resetAudioVisualization();
  audio.stopSong();
  for (int i = 0; i < 50; ++i) {
    audio.loop();
    delay(10);
    if (tuningBusy && pollUserControlsDuringTuning()) {
      Serial.println("[Stream] aborted before connect");
      return false;
    }
  }

  audio.setVolume(volumeLevel);
  updateAudioOutputGate(true);
  audio.setConnectionTimeout(3000, 3000);
  if (tuningBusy && pollUserControlsDuringTuning()) {
    Serial.println("[Stream] aborted before connecttohost");
    return false;
  }
  if (!audio.connecttohost(url)) {
    if (tuneAbortRequested) {
      Serial.println("[Stream] connecttohost aborted by user");
      return false;
    }
    Serial.println("[Stream] connecttohost failed");
    showStatus("Error");
    return false;
  }
  if (tuningBusy && pollUserControlsDuringTuning()) {
    Serial.println("[Stream] aborted after connecttohost");
    return false;
  }

  showStatus("Stream opened");
  return true;
}

static void handleSerialCommand() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == 'p' || c == 'P') {
      playStream();
    } else if (c == 'r' || c == 'R') {
      playStream();
    } else if (c == 'v' || c == 'V') {
      adjustVolume(1);
    } else if (c == 'n' || c == 'N') {
      if (appMode == AppMode::Radio) {
        tuneNextStation();
      } else {
        playNextMp3(true);
      }
    } else if (c == 'm' || c == 'M') {
      if (appMode == AppMode::Radio) {
        tunePreviousStation();
      } else {
        playPreviousMp3();
      }
    } else if (c == 'o' || c == 'O') {
      pendingAppMode = static_cast<int>(appMode == AppMode::Radio ? AppMode::Mp3
                                                                 : AppMode::Radio);
    } else if (c == 'w' || c == 'W') {
      if (appMode == AppMode::Radio) {
        if (wifiConfigPortalActive) {
          Serial.println("[Input] UART exit WiFi setup");
          wifiConfigPortalExitRequested = true;
        } else {
          Serial.println("[Input] UART WiFi setup");
          wifiConfigPortalRequested = true;
          tuneAbortRequested = tuningBusy;
        }
      }
    } else if (c == 'i' || c == 'I') {
      Serial.printf("[Input] adc=%d up=%d down=%d mute=%d\n", analogRead(MEDIA_KEY_ADC),
                    digitalRead(VOLUME_UP_PIN), digitalRead(VOLUME_DOWN_PIN),
                    digitalRead(VOLUME_MUTE_PIN));
    } else if (c == 'd' || c == 'D') {
      if (appMode == AppMode::Mp3 && mountAndScanSd()) {
        playMp3Track(mp3TrackIndex);
      }
    } else if (c == 'l' || c == 'L') {
      printStationList();
    } else if (c >= '1' && c <= '9') {
      tuneStation(static_cast<size_t>(c - '1'));
    }
  }
}

void audioInfo(Audio::msg_t msg) {
  if (kVerboseAudioLog) {
    Serial.printf("[Audio:%s] %s\n", msg.s ? msg.s : "info", msg.msg ? msg.msg : "");
  }
  if (appMode == AppMode::Mp3 && msg.e == Audio::evt_eof) {
    mp3EofPending = true;
    return;
  }
  if (appMode == AppMode::Mp3 && msg.e == Audio::evt_id3data && msg.msg) {
    static constexpr char kId3V22TitlePrefix[] =
        "Title/Songname/Content description: ";
    static constexpr char kId3V22ArtistPrefix[] =
        "Lead artist(s)/Lead performer(s)/Soloist(s)/Performing group: ";
    const char* value = nullptr;
    uint8_t metadataField = 0;
    if (strncmp(msg.msg, "Title: ", 7) == 0) {
      value = msg.msg + 7;
      metadataField = kMp3MetadataTitle;
    } else if (strncmp(msg.msg, kId3V22TitlePrefix, sizeof(kId3V22TitlePrefix) - 1) == 0) {
      value = msg.msg + sizeof(kId3V22TitlePrefix) - 1;
      metadataField = kMp3MetadataTitle;
    } else if (strncmp(msg.msg, "Artist: ", 8) == 0) {
      value = msg.msg + 8;
      metadataField = kMp3MetadataArtist;
    } else if (strncmp(msg.msg, kId3V22ArtistPrefix, sizeof(kId3V22ArtistPrefix) - 1) == 0) {
      value = msg.msg + sizeof(kId3V22ArtistPrefix) - 1;
      metadataField = kMp3MetadataArtist;
    }
    if (value && value[0] != '\0') {
      portENTER_CRITICAL(&mp3MetadataMux);
      if (metadataField == kMp3MetadataTitle) {
        strlcpy(pendingMp3Title, value, sizeof(pendingMp3Title));
      } else {
        strlcpy(pendingMp3Artist, value, sizeof(pendingMp3Artist));
      }
      pendingMp3MetadataMask |= metadataField;
      portEXIT_CRITICAL(&mp3MetadataMux);
    }
    return;
  }
  if (appMode == AppMode::Radio && msg.e == Audio::evt_streamtitle && msg.msg) {
    String title(msg.msg);
    title.trim();
    if (!title.isEmpty()) {
      programTitle = title;
      lastDrawnProgramTitle = "";
      titleScrollStartMs = millis();
      uiNeedsFullRedraw = true;
      Serial.printf("[Radio] title=%s\n", programTitle.c_str());
    }
  }
  if (appMode == AppMode::Radio && !streamReadyShown && msg.msg) {
    const String text(msg.msg);
    if (text.indexOf("stream ready") >= 0) {
      streamReadyShown = true;
      showStatus("Playing");
    }
  }
}

static float stereoToMonoRaw(int32_t leftSample, int32_t rightSample) {
  const float left = static_cast<float>(leftSample);
  const float right = static_cast<float>(rightSample);
  if ((left >= 0.0f && right >= 0.0f) || (left < 0.0f && right < 0.0f)) {
    return (left + right) * 0.5f;
  }
  return fabsf(left) >= fabsf(right) ? left : right;
}

static void pushFftSample(float normalized) {
  if (normalized > 1.0f) {
    normalized = 1.0f;
  } else if (normalized < -1.0f) {
    normalized = -1.0f;
  }

  portENTER_CRITICAL(&fftMux);
  fftRing[fftWriteIndex] = normalized;
  fftWriteIndex = (fftWriteIndex + 1) & (kFftHistorySize - 1);
  if (fftSampleCount < kFftHistorySize) {
    fftSampleCount = fftSampleCount + 1;
  }
  portEXIT_CRITICAL(&fftMux);
}

static void processPcmFrames(int32_t* outBuff, int16_t validSamples) {
  if (validSamples <= 0) {
    return;
  }

  float rawPeak = 0.0f;
  for (int i = 0; i < validSamples; ++i) {
    const float sample = stereoToMonoRaw(outBuff[i * 2], outBuff[i * 2 + 1]);
    const float absSample = fabsf(sample);
    if (absSample > rawPeak) {
      rawPeak = absSample;
    }
  }

  if (rawPeak > 0.0f) {
    float targetGain = 0.68f / rawPeak;
    if (targetGain > 1.0f) {
      targetGain = 1.0f;
    } else if (targetGain < 1.0e-12f) {
      targetGain = 1.0e-12f;
    }

    if (!fftInputGainReady) {
      fftInputGain = targetGain;
      fftInputGainReady = true;
    } else {
      const float rate = targetGain < fftInputGain ? 0.45f : 0.06f;
      fftInputGain += (targetGain - fftInputGain) * rate;
    }
  }

  const float visualPeak = rawPeak * fftInputGain;
  fftInputPeak = visualPeak;
  if (visualPeak > 0.002f) {
    playbackSamplesSeen = true;
  }

  for (int i = 0; i < validSamples; ++i) {
    const float sample = stereoToMonoRaw(outBuff[i * 2], outBuff[i * 2 + 1]) * fftInputGain;
    pushFftSample(sample);
  }
  pcmBlocks = pcmBlocks + 1;
  pcmSamples += validSamples;
  const uint32_t peak = static_cast<uint32_t>(visualPeak * 32767.0f);
  if (peak > pcmPeak) {
    pcmPeak = peak;
  }
}

void pocket_audio_process_raw_samples(int32_t* outBuff, int16_t validSamples) {
  lastRawPcmMs = millis();
  processPcmFrames(outBuff, validSamples);
}

void pocket_audio_process_i2s(int32_t* outBuff, int16_t validSamples, bool* continueI2S) {
  (void)continueI2S;
  if (millis() - lastRawPcmMs > 250) {
    processPcmFrames(outBuff, validSamples);
  }
  if (audioOutputMuted && outBuff && validSamples > 0) {
    memset(outBuff, 0, static_cast<size_t>(validSamples) * 2 * sizeof(int32_t));
  }
}

static void reportPcmActivity() {
  if (!kVerbosePcmLog) {
    return;
  }
  const uint32_t now = millis();
  if (now - lastPcmReportMs < 2000) {
    return;
  }
  lastPcmReportMs = now;

  const uint32_t blocks = pcmBlocks;
  const uint32_t samples = pcmSamples;
  const uint32_t peak = pcmPeak;
  if (blocks > 0 || currentStatus == "Stream opened" || currentStatus == "Playing") {
    Serial.printf("[PCM] blocks=%u samples=%u peak=%u\n", blocks, samples, peak);
  }
}

void setup() {
  pinMode(I2S_BCLK, OUTPUT);
  pinMode(I2S_LRCK, OUTPUT);
  pinMode(I2S_DOUT, OUTPUT);
  digitalWrite(I2S_BCLK, LOW);
  digitalWrite(I2S_LRCK, LOW);
  digitalWrite(I2S_DOUT, LOW);

  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("M5 PocketAudioDeck");

  auto cfg = M5.config();
  cfg.fallback_board = m5::board_t::board_M5StickS3;
  cfg.internal_spk = false;
  cfg.internal_mic = false;
  cfg.internal_imu = false;
  cfg.internal_rtc = false;
  M5.begin(cfg);
  updateUsbPowerState(true);

  M5.Display.setRotation(1);
  M5.Display.setFont(&fonts::Font2);
  const uint32_t welcomeStartedAtMs = millis();
  showWelcomeScreen();
  topCanvas.setColorDepth(8);
  spectrumCanvas.setColorDepth(8);
  topCanvas.createSprite(M5.Display.width(), infoAreaHeight());
  spectrumCanvas.createSprite(M5.Display.width(), spectrumAreaHeight());
  loadSavedStation();
  if (appMode == AppMode::Mp3) {
    disableWiFiForMp3();
  }

  analogReadResolution(12);
  pinMode(MEDIA_KEY_ADC, INPUT);
  pinMode(VOLUME_UP_PIN, INPUT_PULLUP);
  pinMode(VOLUME_DOWN_PIN, INPUT_PULLUP);
  pinMode(VOLUME_MUTE_PIN, INPUT_PULLUP);

  Audio::audio_info_callback = audioInfo;

  if (!audio.setPinout(I2S_BCLK, I2S_LRCK, I2S_DOUT)) {
    Serial.println("[Audio] external PCM5102A I2S init failed");
    showStatus("Error");
    return;
  }
  Serial.printf("[Audio] PCM5102A BCLK=%u LRCK=%u DOUT=%u\n", I2S_BCLK, I2S_LRCK,
                I2S_DOUT);
  audio.setVolume(volumeLevel);
  audioOutputReady = true;
  updateAudioOutputGate(true);
  applyEqPreset(false);
  initSpectrumFft();

  if (appMode == AppMode::Mp3) {
    Serial.printf("[Startup] MP3 preload begin at %u ms\n",
                  static_cast<unsigned>(millis() - welcomeStartedAtMs));
    const bool mp3Ready = mountAndScanSd();
    Serial.printf("[Startup] MP3 preload %s at %u ms\n", mp3Ready ? "ready" : "failed",
                  static_cast<unsigned>(millis() - welcomeStartedAtMs));
    waitForWelcomeScreen(welcomeStartedAtMs);
    if (mp3Ready) {
      playMp3Track(mp3TrackIndex);
    } else {
      renderUi();
    }
    return;
  }

  waitForWelcomeScreen(welcomeStartedAtMs);

  if (wifiSsid.isEmpty()) {
    Serial.println("[WiFi] no configured SSID");
    wifiConnectionFailed = true;
    showStatus("WiFi not configured");
    return;
  }

  if (!connectWiFi()) {
    return;
  }

  playStream();
}

void loop() {
  M5.update();
  updateUsbPowerState();
  handlePeripheralControls();
  handleM5Keys();

  if (wifiConfigPortalActive) {
    handleSerialCommand();
    serviceWifiConfigPortal();
    delay(1);
    return;
  }
  if (!tuningBusy && wifiConfigPortalRequested && appMode == AppMode::Radio) {
    startWifiConfigPortal();
    return;
  }
  if (!tuningBusy && pendingAppMode >= 0) {
    const AppMode mode = static_cast<AppMode>(pendingAppMode);
    pendingAppMode = -1;
    switchAppMode(mode);
    return;
  }
  if (!tuningBusy && pendingTuneIndex >= 0) {
    const int index = pendingTuneIndex;
    pendingTuneIndex = -1;
    tuneStation(static_cast<size_t>(index));
    return;
  }
  handleSerialCommand();
  handleSdHotplug();
  audio.loop();
  processMp3AudioEvents();
  reportPcmActivity();
  if (appMode == AppMode::Radio && playbackSamplesSeen &&
      currentStatus == "Stream opened") {
    showStatus("Playing");
  }
  updateAudioOutputGate();
  renderUi();

  if (appMode == AppMode::Radio && WiFi.status() != WL_CONNECTED &&
      !wifiConnectionFailed) {
    Serial.println("[WiFi] disconnected");
    wifiConnectionFailed = true;
    showStatus("WiFi disconnected");
  }

  delay(1);
}
