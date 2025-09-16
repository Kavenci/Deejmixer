#include <Adafruit_NeoPixel.h>
#include <math.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <DNSServer.h>
#include <EEPROM.h>

// ==================== Eeprom =================
#define EEPROM_BYTES   16      // small block is fine
#define MODE_ADDR      0       // where we'll store currentMode (1 byte)

// ==================== Sliders ====================
const int NUM_SLIDERS = 9;
const int analogInputs[NUM_SLIDERS] = {34, 35, 32, 33, 25, 36, 14, 27, 26};
int analogSliderValues[NUM_SLIDERS];

// ==================== NeoPixel / Grid ====================
#define LED_PIN  22
#define NUM_LEDS 25
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Grid config (5x5)
const int ROWS = 5;
const int COLS = 5;
const bool SERPENTINE = false; // set true if your physical wiring snakes each row

int pixelIndex(int r, int c) {
  // r: 0..ROWS-1 (top->bottom), c: 0..COLS-1 (left->right)
  if (!SERPENTINE) {
    return r * COLS + c;
  } else {
    int base = r * COLS;
    if (r % 2 == 0) return base + c;               // left->right
    else return base + (COLS - 1 - c);             // right->left
  }
}

// ==================== Button / Modes ====================
const int BUTTON_PIN = 4;
int currentMode = 1;               // 0=off,1=rainbow,2=meteor,3=breathing,4=vertical wave,5=column wave,6=diagonal wave,7=raindrop sparkle
const int MAX_MODE = 7;

// ---- Button & press/hold logic ----
const unsigned long DEBOUNCE_MS = 30;
const unsigned long LONG_HOLD_MS = 5000;

int  btnRaw = HIGH, btnLastRaw = HIGH;
int  btnStable = HIGH;
unsigned long btnLastChangeMs = 0;

bool btnIsPressed = false;
unsigned long btnPressStartMs = 0;
bool longHoldFired = false;

// ==================== Non-blocking timing ====================
unsigned long nowMs;

// Rainbow
static uint8_t rainbowHue = 0;
unsigned long rainbowLastStep = 0;
const unsigned long rainbowInterval = 20;

// Meteor
unsigned long meteorLastStep = 0;
const unsigned long meteorInterval = 50;

// Breathing
static uint32_t breathColor = 0;
static int breathBrightness = 0;
static int breathDir = 1;
unsigned long breathLastStep = 0;
const unsigned long breathInterval = 10;

// Vertical / Column / Diagonal waves
unsigned long waveLastStep = 0;
const unsigned long waveInterval = 25;  // ms per step
float wavePhase = 0.0f;                 // advances over time
const float waveSpeed = 0.15f;          // phase increment per step

// Raindrop sparkle
const int MAX_DROPS = 6;
bool dropActive[MAX_DROPS] = {false};
int  dropRow[MAX_DROPS];
int  dropCol[MAX_DROPS];
unsigned long dropLastStep = 0;
const unsigned long dropInterval = 60;    // ms per fall step
const uint8_t dropTailLen = 2;            // how many trailing pixels to glow
const uint8_t dropHeadBright = 255;       // head brightness
const uint8_t dropTailBright = 80;        // tail brightness
const uint8_t dropSpawnChance = 35;       // 0..100 per step (% chance to spawn if slot free)

// ==================== OTA AP + Captive Portal ====================
WebServer server(80);
DNSServer dnsServer;
bool otaMode = false;
const char* OTA_SSID = "DeeJ-OTA";
const char* OTA_PASS = "flashme123";

// Idle timeout (10 minutes)
const unsigned long OTA_IDLE_TIMEOUT_MS = 10UL * 60UL * 1000UL;
unsigned long lastOtaActivityMs = 0;
int prevModeBeforeOTA = 0;

const char* OTA_FORM_HTML =
  "<!DOCTYPE html><html><head>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>ESP32 OTA Update</title>"
  "<style>body{font-family:sans-serif;max-width:600px;margin:24px auto;padding:0 16px}"
  "h2{margin-bottom:12px}form{margin:16px 0}input[type=file]{margin:12px 0}</style>"
  "</head><body>"
  "<h2>ESP32 OTA Update</h2>"
  "<p>Select your compiled .bin and press Update.</p>"
  "<form method='POST' action='/update' enctype='multipart/form-data'>"
  "<input type='file' name='update' accept='.bin' required>"
  "<br><input type='submit' value='Update'>"
  "</form>"
  "<p>If this page didn't auto-open, browse to <b>http://192.168.4.1</b>.</p>"
  "</body></html>";

void servePortalPage() {
  lastOtaActivityMs = millis();
  server.send(200, "text/html", OTA_FORM_HTML);
}

void redirectToRoot() {
  lastOtaActivityMs = millis();
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Connection", "close");
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "Redirecting to OTA page...");
}


void startOtaAP() {
  if (otaMode) return;
  otaMode = true;

  prevModeBeforeOTA = currentMode;
  currentMode = 0;
  lastOtaActivityMs = millis();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(OTA_SSID, OTA_PASS);
  IPAddress apIP = WiFi.softAPIP();

  // DNS Captive Portal: resolve all to AP IP
  dnsServer.start(53, "*", apIP);

  // --- Captive portal / probe endpoints ---

  // Our OTA page
  server.on("/", HTTP_GET, servePortalPage);

  // ANDROID probe (204) + iOS/macOS — redirect to portal
  server.on("/generate_204", HTTP_GET, redirectToRoot);           // Android
  server.on("/redirect", HTTP_GET, redirectToRoot);               // Android alt
  server.on("/hotspot-detect.html", HTTP_GET, redirectToRoot);    // Apple

  // WINDOWS probes — ***redirect*** to trigger captive portal popup
  server.on("/ncsi.txt", HTTP_GET, redirectToRoot);               // older probe
  server.on("/connecttest.txt", HTTP_GET, redirectToRoot);        // newer probe
  server.on("/connecttest", HTTP_GET, redirectToRoot);
  server.on("/msftconnecttest/redirect", HTTP_GET, redirectToRoot);
  server.on("/fwlink", HTTP_GET, redirectToRoot);                 // old Windows captive URL

  // OTA upload endpoint
  server.on("/update", HTTP_POST,
    []() {
      lastOtaActivityMs = millis();
      bool ok = !Update.hasError();
      server.send(200, "text/plain", ok ? "Update SUCCESS. Rebooting..." : "Update FAILED.");
      delay(500);
      if (ok) ESP.restart();
    },
    []() {
      HTTPUpload& upload = server.upload();
      lastOtaActivityMs = millis();
      if (upload.status == UPLOAD_FILE_START) {
        size_t maxSketch = UPDATE_SIZE_UNKNOWN;
        if (!Update.begin(maxSketch)) { Update.printError(Serial); }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (!Update.end(true)) {
          Update.printError(Serial);
        }
      }
    }
  );

  // Anything else → 302 to our portal (helps push normal browsers to the page)
  server.onNotFound([]() {
    lastOtaActivityMs = millis();
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Connection", "close");
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "Redirecting to OTA page...");
  });

  server.begin();
}


void stopOtaAP() {
  if (!otaMode) return;
  server.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  otaMode = false;

  currentMode = prevModeBeforeOTA;
  strip.clear();
  strip.show();
}

// ==================== Setup ====================
void setup() {
  for (int i = 0; i < NUM_SLIDERS; i++) pinMode(analogInputs[i], INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  strip.begin();
  strip.show();
  Serial.begin(9600);
  EEPROM.begin(EEPROM_BYTES);

  uint8_t saved = EEPROM.read(MODE_ADDR);
  if (saved <= MAX_MODE) {
    currentMode = saved;
  } else {
    currentMode = 1;  // sane default
  }

  // Seed initial breathing color
  breathColor = strip.Color(random(256), random(256), random(256));
}

// ==================== Main loop ====================
void loop() {
  nowMs = millis();

  updateSliderValues();
  sendSliderValues();

  handleButton();

  switch (currentMode) {
    case 0: displayOffNB();          break;
    case 1: displayRainbowNB();      break;
    case 2: displayMeteorNB();       break;
    case 3: displayBreathingNB();    break;
    case 4: displayVerticalWaveNB(); break;
    case 5: displayColumnWaveNB();   break;
    case 6: displayDiagonalWaveNB(); break;
    case 7: displayRaindropSparkleNB(); break;
  }

  if (otaMode) {
    dnsServer.processNextRequest();
    server.handleClient();
    if (millis() - lastOtaActivityMs >= OTA_IDLE_TIMEOUT_MS) {
      stopOtaAP();
    }
  }

  delay(1);
}

// ==================== Sliders ====================
void updateSliderValues() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    int raw = analogRead(analogInputs[i]);
    analogSliderValues[i] = (int)(((uint64_t)raw) * 1023 / 4095);
  }
}

void sendSliderValues() {
  String s;
  for (int i = 0; i < NUM_SLIDERS; i++) {
    s += String(analogSliderValues[i]);
    if (i < NUM_SLIDERS - 1) s += '|';
  }
  Serial.println(s);
}

// ==================== Button logic ====================
void handleButton() {
  int reading = digitalRead(BUTTON_PIN);

  if (reading != btnLastRaw) {
    btnLastChangeMs = nowMs;
    btnLastRaw = reading;
  }

  if ((nowMs - btnLastChangeMs) > DEBOUNCE_MS && reading != btnStable) {
    btnStable = reading;

    if (btnStable == LOW) {
      btnIsPressed = true;
      btnPressStartMs = nowMs;
      longHoldFired = false;
    } else {
      if (btnIsPressed) {
        unsigned long held = nowMs - btnPressStartMs;
        if (!longHoldFired && held >= DEBOUNCE_MS) {
          onShortPress();
        }
      }
      btnIsPressed = false;
    }
  }

  if (btnIsPressed && !longHoldFired && (nowMs - btnPressStartMs >= LONG_HOLD_MS)) {
    onLongHold();
    longHoldFired = true;
  }
}

void onShortPress() {
  currentMode = (currentMode + 1) % (MAX_MODE + 1);
  saveMode();
}

void onLongHold() {
  for (int n = 0; n < 3; n++) {
    for (int i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, strip.Color(255, 255, 255));
    }
    strip.show();
    delay(120);
    strip.clear();
    strip.show();
    delay(120);
  }
  startOtaAP();
}

// ==================== Effects ====================
void displayOffNB() {
  static bool wasOff = false;
  if (!wasOff) {
    strip.clear();
    strip.show();
    wasOff = true;
  }
  if (currentMode != 0) wasOff = false;
}

void displayRainbowNB() {
  if (nowMs - rainbowLastStep < rainbowInterval) return;
  rainbowLastStep = nowMs;
  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, Wheel((i + rainbowHue) & 255));
  }
  strip.show();
  rainbowHue++;
}

void displayMeteorNB() {
  if (nowMs - meteorLastStep < meteorInterval) return;
  meteorLastStep = nowMs;
  for (int i = 0; i < strip.numPixels(); i++) {
    if (random(10) > 5) strip.setPixelColor(i, strip.Color(255, 255, 255));
    else strip.setPixelColor(i, 0);
  }
  strip.show();
}

void displayBreathingNB() {
  if (nowMs - breathLastStep < breathInterval) return;
  breathLastStep = nowMs;
  breathBrightness += breathDir;
  if (breathBrightness >= 255) { breathBrightness = 255; breathDir = -1; }
  if (breathBrightness <= 0)   { breathBrightness = 0;   breathDir =  1; breathColor = strip.Color(random(256), random(256), random(256)); }

  uint8_t r = ((breathColor >> 16) & 0xFF) * breathBrightness / 255;
  uint8_t g = ((breathColor >>  8) & 0xFF) * breathBrightness / 255;
  uint8_t b = ((breathColor      ) & 0xFF) * breathBrightness / 255;

  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  strip.show();
}

// Top-to-bottom sine wave, all columns in sync
void displayVerticalWaveNB() {
  if (nowMs - waveLastStep < waveInterval) return;
  waveLastStep = nowMs;
  wavePhase += waveSpeed;

  for (int r = 0; r < ROWS; r++) {
    float v = 0.5f * (sinf(wavePhase - r * 0.6f) + 1.0f);
    uint8_t br = (uint8_t)(v * 255);
    uint32_t col = strip.Color(br / 3, br, br); // cyan-ish
    for (int c = 0; c < COLS; c++) {
      strip.setPixelColor(pixelIndex(r, c), col);
    }
  }
  strip.show();
}

// Column-wise traveling ripple (offset per column)
void displayColumnWaveNB() {
  if (nowMs - waveLastStep < waveInterval) return;
  waveLastStep = nowMs;
  wavePhase += waveSpeed;

  for (int c = 0; c < COLS; c++) {
    float v = 0.5f * (sinf(wavePhase - c * 0.8f) + 1.0f);
    uint8_t br = (uint8_t)(v * 255);
    uint32_t col = strip.Color(br, br / 3, 0); // warm
    for (int r = 0; r < ROWS; r++) {
      strip.setPixelColor(pixelIndex(r, c), col);
    }
  }
  strip.show();
}

// Diagonal wave (top-left → bottom-right)
void displayDiagonalWaveNB() {
  if (nowMs - waveLastStep < waveInterval) return;
  waveLastStep = nowMs;
  wavePhase += waveSpeed;

  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      float phase = wavePhase - (r + c) * 0.5f;     // diagonal offset
      float v = 0.5f * (sinf(phase) + 1.0f);
      uint8_t br = (uint8_t)(v * 255);
      // purple-ish
      uint32_t col = strip.Color(br, 0, br);
      strip.setPixelColor(pixelIndex(r, c), col);
    }
  }
  strip.show();
}

// Raindrop sparkle (random drops fall with short tails)
void displayRaindropSparkleNB() {
  if (nowMs - dropLastStep < dropInterval) return;
  dropLastStep = nowMs;

  // Dim the whole panel slightly each step (soft persistence)
  for (int i = 0; i < strip.numPixels(); i++) {
    uint32_t c = strip.getPixelColor(i);
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >> 8)  & 0xFF;
    uint8_t b = (c)       & 0xFF;
    // fade
    r = (r > 10) ? r - 10 : 0;
    g = (g > 10) ? g - 10 : 0;
    b = (b > 10) ? b - 10 : 0;
    strip.setPixelColor(i, strip.Color(r, g, b));
  }

  // Advance existing drops
  for (int i = 0; i < MAX_DROPS; i++) {
    if (!dropActive[i]) continue;

    // Draw head and tail at current position BEFORE moving (so tail lingers)
    for (int t = 0; t <= dropTailLen; t++) {
      int rr = dropRow[i] - t;
      if (rr < 0) break;
      uint8_t br = (t == 0) ? dropHeadBright : dropTailBright;
      // cool blue-white drop
      uint32_t col = strip.Color(br / 2, br / 2, br);
      strip.setPixelColor(pixelIndex(rr, dropCol[i]), col);
    }

    // Move down
    dropRow[i]++;
    if (dropRow[i] >= ROWS) {
      dropActive[i] = false; // off screen
    }
  }

  // Try to spawn new drops
  for (int i = 0; i < MAX_DROPS; i++) {
    if (dropActive[i]) continue;
    if (random(100) < dropSpawnChance) {
      dropActive[i] = true;
      dropRow[i] = 0;
      dropCol[i] = random(COLS); // random column
    }
  }

  strip.show();
}

// ==================== Eeprom Save ====================
void saveMode() {
  EEPROM.write(MODE_ADDR, (uint8_t)currentMode);
  EEPROM.commit(); // important on ESP32
}


// ==================== Color wheel ====================
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } else if (WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  } else {
    WheelPos -= 170;
    return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  }
}
