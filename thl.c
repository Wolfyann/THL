/*
 * Temperature / Humidity Station – FireBeetle ESP8266 + DF Robot DHT22 + OLED SSD1306 0.96'
 * Version 4.7
 *
 * A button press turns on the display; each subsequent press changes the page:
 *   PAGE 1: humidity and temperature (large digits) + full-height graph
 *   PAGE 2: history statistics (min / average / max, period, in-range rate)
 *   PAGE 3: dew point
 *   PAGE 4: hydric stress
 *   PAGE 5: peaks and alerts
 *   PAGE 6: diagnostics (battery, sensor status, failures, uptime)
 *
 * PAGE 1 - immediate reading:
 *   - the value area indicates the status: border = limit, filled background = alert
 *   - a single trend arrow (humidity, over 1 h)
 *   - a small battery icon appears only if the voltage is low
 *   - graph: curve = humidity, points = temperature, dashed lines = ideal zone
 *     45-55%, small ticks = 3 h, solid square = last measurement
 *
 * Voltage measurement and protection shutdown remain permanently active.
 * No symbols depend on the font (degree and arrows are drawn).
 *
 * WIRING:
 *   - Push button between GPIO12 and GND (internal pull-up, active low)
 *   - D0 (GPIO16) NOT connected to RST
 *   - DHT22 on GPIO10
 */

#define SSD1306_NO_SPLASH

#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <user_interface.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#include <Fonts/FreeSansBold18pt7b.h>   // Main digits
#include <Fonts/FreeSansBold12pt7b.h>   // Temperature decimal, voltage
#include <Fonts/FreeSansBold9pt7b.h>    // Units

extern "C" {
  #include <gpio.h>
}

#define FW_VERSION "4.7"
#define DEBUG_SERIAL 0
#define TEST_FAST    0

#if DEBUG_SERIAL
  #define LOG(...) Serial.printf(__VA_ARGS__)
#else
  #define LOG(...) do {} while (0)
#endif

// ---------- Hardware ----------
#define BUTTON_PIN     12
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_ADDR      0x3C
#define OLED_CONTRAST  0x30
#define DHTPIN         10
#define DHTTYPE        DHT22

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHT dht(DHTPIN, DHTTYPE);

ADC_MODE(ADC_VCC);

// ---------- Battery ----------
#define BATT_FULL_V     3.30f   // Voltage displayed as "full" on the icon (to be adjusted)
#define LOW_BATTERY_V   2.85f   // Alert: icon on page 1, status "LOW"
#define CUTOFF_V        2.60f   // Protective shutdown; empty icon
#define VCC_CALIB       1.22f

// ---------- Target Ranges ----------
#define HUM_OK_MIN      45.0f
#define HUM_OK_MAX      55.0f
#define HUM_ALERTE_MIN  40.0f
#define HUM_ALERTE_MAX  60.0f

#define TEMP_OK_MIN     17.0f
#define TEMP_OK_MAX     25.0f

#define TREND_H_DELTA   2.0f
#define TREND_T_DELTA   1.0f

// ---------- Parameters ----------
constexpr uint8_t  NUM_MEASUREMENTS = 48;
constexpr uint8_t  DHT_TRIES        = 3;
constexpr uint32_t SLICE_MS         = 350; // Wake-up periods
constexpr uint32_t DEBOUNCE_MS      = 150;  // Debounce: minimum delay between two presses
constexpr int16_t  T_NONE           = INT16_MIN;
constexpr uint16_t H_NONE           = 0xFFFF;
constexpr uint8_t  NB_PAGES         = 6;   // 0: measurements, 1: statistics, 2: dew point, 3: hydric stress, 4: peaks/alerts, 5: diagnostics

#if TEST_FAST
constexpr uint32_t MEASUREMENT_INTERVAL_MS = 60000UL;
constexpr uint32_t DISPLAY_MS              = 5000UL;
#else
constexpr uint32_t MEASUREMENT_INTERVAL_MS = 900000UL; // 15 min
constexpr uint32_t DISPLAY_MS              = 10000UL;  // 10 s
#endif

constexpr uint32_t TREND_WINDOW_MS = 3600000UL;
constexpr uint32_t TREND_RAW = TREND_WINDOW_MS / MEASUREMENT_INTERVAL_MS;
constexpr uint8_t TREND_SAMPLES =
    TREND_RAW < 1 ? 1 :
    (TREND_RAW > (uint32_t)(NUM_MEASUREMENTS - 1)
      ? (NUM_MEASUREMENTS - 1) : TREND_RAW);

// Number of samples between two 3h markers on the graph
constexpr uint32_t PAS_3H = 10800000UL / MEASUREMENT_INTERVAL_MS;

// ---------- Page 1 Geometry ----------
constexpr int PANEL_W = 85;                 // Width of humidity / temperature zones
constexpr int LARGEUR_UNITE_C = 21;         // Space required for "°C" (spacing + circle + "C" 9 pt)
constexpr int GX0 = 87, GY0 = 1;            // Graph frame (full height)
constexpr int GW  = 40, GH  = 62;

// ---------- State ----------
int16_t  histTemp[NUM_MEASUREMENTS];
uint16_t histHum[NUM_MEASUREMENTS];
uint8_t  histHead = 0;   // Next write index
uint8_t  histCount = 0;  // Number of slots actually acquired (valid or not)

volatile bool boutonReveil = false;
bool boutonRelache = true;
bool oledOk = false;
bool ecranAllume = false;

uint32_t ecranDebut = 0;
uint32_t derniereMesure = 0;
uint32_t dernierAppui = 0;

uint8_t pageActuelle = 0;

uint64_t uptimeMs = 0;       // Cumulative operating time (without millis overflow)
uint32_t dernierMillis = 0;
uint64_t sommeilMs = 0;      // Cumulative time spent in light sleep (diagnostics)

// ---------- History ----------

static void ajouterMesure(bool ok, float t, float h) {
  histTemp[histHead] = ok ? (int16_t)lroundf(t * 10.0f) : T_NONE;
  histHum[histHead]  = ok ? (uint16_t)lroundf(h * 10.0f) : H_NONE;

  histHead = (histHead + 1) % NUM_MEASUREMENTS;

  if (histCount < NUM_MEASUREMENTS) {
    histCount++;
  }
}

// k = 0: first slot kept; k = histCount-1: last slot.
// Before buffer is full, measurements start at index 0.
// Once full, histHead points to the oldest measurement.
static bool echantillon(uint8_t k, float &t, float &h) {
  if (k >= histCount) return false;

  uint8_t index;
  if (histCount < NUM_MEASUREMENTS) {
    index = k;
  } else {
    index = (histHead + k) % NUM_MEASUREMENTS;
  }

  if (histTemp[index] == T_NONE || histHum[index] == H_NONE) {
    return false;
  }

  t = histTemp[index] / 10.0f;
  h = histHum[index] / 10.0f;
  return true;
}

static bool dernierEchantillon(float &t, float &h) {
  for (int k = (int)histCount - 1; k >= 0; k--) {
    if (echantillon((uint8_t)k, t, h)) return true;
  }
  return false;
}

// 1 = rise, -1 = drop, 0 = stable, 2 = insufficient/invalid history.
static int8_t tendance(bool humidite) {
  if (histCount <= TREND_SAMPLES) return 2;

  float t1, h1, t0, h0;
  const uint8_t dernier = histCount - 1;
  const uint8_t precedent = dernier - TREND_SAMPLES;

  if (!echantillon(dernier, t1, h1) ||
      !echantillon(precedent, t0, h0)) {
    return 2;
  }

  const float delta = humidite ? (h1 - h0) : (t1 - t0);
  const float seuil = humidite ? TREND_H_DELTA : TREND_T_DELTA;

  if (delta > seuil) return 1;
  if (delta < -seuil) return -1;
  return 0;
}

// ---------- Comfort Zones ----------
// 0 = OK; +-1 = limit; +-2 = alert.
static int8_t zoneHumidite(float h) {
  if (isnan(h)) return 0;
  if (h < HUM_ALERTE_MIN) return -2;
  if (h > HUM_ALERTE_MAX) return 2;
  if (h < HUM_OK_MIN) return -1;
  if (h > HUM_OK_MAX) return 1;
  return 0;
}

static int8_t zoneTemperature(float t) {
  if (isnan(t)) return 0;
  if (t < TEMP_OK_MIN) return -2;
  if (t > TEMP_OK_MAX) return 2;
  return 0;
}

// ---------- Sensors ----------

static float lireTension() {
  return (ESP.getVcc() / 1024.0f) * VCC_CALIB;
}

static bool lireDHT(float &t, float &h, uint8_t essais) {
  for (uint8_t i = 0; i < essais; i++) {
    float hh = dht.readHumidity();
    float tt = dht.readTemperature();

    if (!isnan(hh) && !isnan(tt) &&
        tt > -40.0f && tt < 80.0f &&
        hh >= 0.0f && hh <= 100.0f) {
      t = tt;
      h = hh;
      return true;
    }

    if (i + 1 < essais) {
      delay(2100); // Minimum delay between two DHT22 readings
    }
  }

  t = NAN;
  h = NAN;
  return false;
}

// Dew point calculation from temperature and relative humidity
static float calculerPointDeRosee(float t, float h) {
  if (isnan(t) || isnan(h) || h <= 0.0f) return NAN;
  const float a = 17.27f;
  const float b = 237.7f;
  float alpha = ((a * t) / (b + t)) + logf(h / 100.0f);
  return (b * alpha) / (a - alpha);
}

// ---------- Display: Base Functions ----------

static bool initOLED() {
  Wire.begin();
  Wire.setClock(400000);

  Wire.beginTransmission(OLED_ADDR);
  if (Wire.endTransmission() != 0) return false;

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) return false;

  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(OLED_CONTRAST);
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  display.display();

  return true;
}

static void oledOff() {
  display.ssd1306_command(SSD1306_DISPLAYOFF);
}

// ---------- Optimized CRT Animation ----------

static void animationExtinction() {

  constexpr uint8_t W = SCREEN_WIDTH;
  constexpr uint8_t H = SCREEN_HEIGHT;
  constexpr uint16_t BUFFER_SIZE = W * H / 8;

  static uint8_t source[BUFFER_SIZE];

  uint8_t* buffer = display.getBuffer();

  if (!buffer || display.width() != W || display.height() != H) {
    return;
  }

  // Backup initial image
  memcpy(source, buffer, BUFFER_SIZE);

  // Read a pixel in the source framebuffer
  auto pixelSource = [&](uint8_t x, uint8_t y) -> bool {
    const uint16_t index = x + (y >> 3) * W;
    return (source[index] & (1U << (y & 7))) != 0;
  };

  // Write a pixel in the current framebuffer
  auto pixelDestination = [&](uint8_t x, uint8_t y) {
    const uint16_t index = x + (y >> 3) * W;
    buffer[index] |= (1U << (y & 7));
  };

  // =================================================
  // PHASE 1: VERTICAL COMPRESSION
  // The image progressively turns into a line.
  // =================================================

  const uint8_t hauteurs[] = {16, 4, 1};

  for (uint8_t h : hauteurs) {

    memset(buffer, 0, BUFFER_SIZE);

    const uint8_t y0 = (H - h) / 2;

    for (uint8_t x = 0; x < W; x++) {

      for (uint8_t y = 0; y < h; y++) {

        // Each destination pixel represents a band of pixels from the original image.
        const uint8_t sy0 = (uint16_t)y * H / h;
        const uint8_t sy1 = (uint16_t)(y + 1) * H / h;

        bool allume = false;

        for (uint8_t sy = sy0; sy < sy1; sy++) {
          if (pixelSource(x, sy)) {
            allume = true;
            break;
          }
        }

        if (allume) {
          pixelDestination(x, y0 + y);
        }
      }
    }

    display.display();
  }

  // =================================================
  // PHASE 2: HORIZONTAL COMPRESSION
  // The line progressively turns into a point.
  // =================================================

  const uint8_t largeurs[] = {64, 16, 4, 1};

  constexpr uint8_t Y_CENTRE = H / 2;

  for (uint8_t w : largeurs) {

    memset(buffer, 0, BUFFER_SIZE);

    const uint8_t x0 = (W - w) / 2;

    for (uint8_t x = 0; x < w; x++) {

      const uint8_t sx0 = (uint16_t)x * W / w;
      const uint8_t sx1 = (uint16_t)(x + 1) * W / w;

      bool allume = false;

      // Preserve all luminous information from the initial vertical band.
      for (uint8_t sx = sx0; sx < sx1 && !allume; sx++) {

        for (uint8_t sy = 0; sy < H; sy++) {
          if (pixelSource(sx, sy)) {
            allume = true;
            break;
          }
        }
      }

      if (allume) {
        pixelDestination(x0 + x, Y_CENTRE);
      }
    }

    display.display();
  }

  // Very short persistence of the central point
  delay(16);
}

// Drawn trend arrow (independent of any font), 11 x 11 px box.
static void dessinerTendance(int8_t tend, int x, int y, uint16_t col) {
  if (tend == 2) return;

  if (tend > 0) {                                   // rise
    display.fillTriangle(x + 4, y, x, y + 6, x + 8, y + 6, col);
    display.fillRect(x + 3, y + 6, 3, 5, col);
  } else if (tend < 0) {                            // drop
    display.fillTriangle(x + 4, y + 10, x, y + 4, x + 8, y + 4, col);
    display.fillRect(x + 3, y, 3, 5, col);
  } else {                                          // stable
    display.fillTriangle(x + 10, y + 5, x + 4, y, x + 4, y + 10, col);
    display.fillRect(x, y + 3, 5, 5, col);
  }
}

// Drawn degree symbol: small 5 x 5 px circle, top-left corner at (x, y).
static void dessinerDegre(int x, int y, uint16_t col) {
  display.drawCircle(x + 2, y + 2, 2, col);
}

// Small "low battery" icon (13 x 6 px), single segment.
static void dessinerBatterieFaible(int x, int y, uint16_t col) {
  display.drawRect(x, y, 11, 6, col);
  display.fillRect(x + 11, y + 2, 2, 2, col);
  display.fillRect(x + 2, y + 2, 2, 2, col);
}

// ---------- Page 0: Humidity, Temperature, Graph ----------

// Full-height history graph:
//  - fixed time axis: the most recent measurement is always on the right edge,
//    history fills from right to left (12 h = full width)
//  - humidity: solid curve, 40-60% scale (expanded if measurements go out of range),
//    ideal zone 45-55% marked by two dashed lines
//  - temperature: points, automatic scale (minimum span of 2 °C)
//  - small ticks every 3 h, solid square on the last measurement
static void dessinerGraphique() {
  const int xD = GX0 + 2;               // First useful pixel (oldest)
  const int innerW = GW - 4;
  const int xF = xD + innerW;           // Last useful pixel (now)
  const int yHaut = GY0 + 2;
  const int innerH = GH - 4;
  const int yBas = yHaut + innerH;

  display.drawRect(GX0, GY0, GW, GH, SSD1306_WHITE);

  // Bounds for valid values
  float tMin = 1000.0f, tMax = -1000.0f;
  float hMin = 1000.0f, hMax = -1000.0f;
  float t, hh;
  bool donnees = false;

  for (uint8_t k = 0; k < histCount; k++) {
    if (!echantillon(k, t, hh)) continue;
    if (t < tMin) tMin = t;
    if (t > tMax) tMax = t;
    if (hh < hMin) hMin = hh;
    if (hh > hMax) hMax = hh;
    donnees = true;
  }

  if (!donnees) return;

  if (tMax - tMin < 2.0f) {
    const float centre = (tMin + tMax) / 2.0f;
    tMin = centre - 1.0f;
    tMax = centre + 1.0f;
  }

  const float hBas = (hMin < HUM_ALERTE_MIN) ? hMin : HUM_ALERTE_MIN;
  const float hHaut = (hMax > HUM_ALERTE_MAX) ? hMax : HUM_ALERTE_MAX;

  auto yHum = [&](float v) -> int {
    int y = yBas - (int)lroundf((v - hBas) / (hHaut - hBas) * innerH);
    return y < yHaut ? yHaut : (y > yBas ? yBas : y);
  };
  auto yTmp = [&](float v) -> int {
    int y = yBas - (int)lroundf((v - tMin) / (tMax - tMin) * innerH);
    return y < yHaut ? yHaut : (y > yBas ? yBas : y);
  };
  auto xAt = [&](uint8_t k) -> int {
    return xF - (((int)histCount - 1 - (int)k) * innerW) / (NUM_MEASUREMENTS - 1);
  };

  // Ideal humidity zone: two dashed lines
  const int y55 = yHum(HUM_OK_MAX);
  const int y45 = yHum(HUM_OK_MIN);
  for (int x = xD; x <= xF; x += 4) {
    display.drawPixel(x, y55, SSD1306_WHITE);
    display.drawPixel(x, y45, SSD1306_WHITE);
  }

  // Markers every 3 h (small ticks at the bottom of the frame)
  if (PAS_3H >= 1) {
    for (uint32_t i = PAS_3H; i <= (uint32_t)(NUM_MEASUREMENTS - 1); i += PAS_3H) {
      const int x = xF - (int)((i * innerW) / (NUM_MEASUREMENTS - 1));
      display.drawFastVLine(x, GY0 + GH - 4, 3, SSD1306_WHITE);
    }
  }

  // Curves: humidity as solid line, temperature as points
  int px = -1;
  int py = -1;
  int dernierX = -1;
  int dernierY = -1;

  for (uint8_t k = 0; k < histCount; k++) {
    if (!echantillon(k, t, hh)) {
      px = -1;
      continue;
    }

    const int x = xAt(k);
    const int yh = yHum(hh);

    if (px >= 0) display.drawLine(px, py, x, yh, SSD1306_WHITE);
    else display.drawPixel(x, yh, SSD1306_WHITE);

    display.drawPixel(x, yTmp(t), SSD1306_WHITE);

    px = x;
    py = yh;

    if (k == histCount - 1) {
      dernierX = x;
      dernierY = yh;
    }
  }

  // Solid square on the latest measurement
  if (dernierX >= 0) {
    display.fillRect(dernierX - 1, dernierY - 1, 2, 2, SSD1306_WHITE);
  }
}

// Zone background based on status: +-1 = border (limit), +-2 = filled (alert).
static void fondZone(int y, int h, int8_t zone) {
  if (zone == 2 || zone == -2) {
    display.fillRect(0, y, PANEL_W, h, SSD1306_WHITE);
  } else if (zone == 1 || zone == -1) {
    display.drawRect(0, y, PANEL_W, h, SSD1306_WHITE);
  }
}

// Humidity: 18 pt digits, "%" in 9 pt, trend arrow, low battery icon if applicable.
// Positions follow cursor (getCursorX): no hardcoded width.
static void dessinerHumidite(float h, int8_t zone, bool batterieFaible) {
  const bool inv = (zone == 2 || zone == -2);
  const uint16_t fg = inv ? SSD1306_BLACK : SSD1306_WHITE;
  fondZone(0, 31, zone);
  display.setTextColor(fg);
  display.setTextSize(1);

  const int hv = isnan(h) ? -1 : (int)lroundf(h);

  const bool trois = (hv >= 100);                      // 100%: reduced font
  const int base = trois ? 24 : 28;                    // Baseline (centered digits)

  display.setFont(trois ? &FreeSansBold12pt7b : &FreeSansBold18pt7b);
  display.setCursor(2, base);
  if (hv < 0) display.print(F("--"));
  else display.printf("%d", hv);

  display.setFont(&FreeSansBold9pt7b);
  display.setCursor(display.getCursorX() + 1, base);
  display.print(F("%"));

  if (batterieFaible) dessinerBatterieFaible(70, 3, fg);
  if (hv >= 0 && !trois) {
    // Arrow attached to "%" and centered on digit height (18 pt = 25 px)
    dessinerTendance(tendance(true), display.getCursorX() + 3, base - 17, fg);
  }
}

// Temperature: integer part 18 pt, decimal 12 pt, unit "°C" in 9 pt.
static void dessinerTemperature(float t, int8_t zone) {
  const bool inv = (zone == 2 || zone == -2);
  const uint16_t fg = inv ? SSD1306_BLACK : SSD1306_WHITE;
  const int base = 61;

  fondZone(33, 31, zone);
  display.setTextColor(fg);
  display.setTextSize(1);

  display.setFont(&FreeSansBold18pt7b);
  display.setCursor(2, base);

  // Text: "--" if measurement is invalid; no "-0.0" for values close to 0
  char buf[10];
  const char* decimale = nullptr;
  if (isnan(t)) {
    snprintf(buf, sizeof buf, "--");
  } else {
    const float tv = (fabsf(t) < 0.05f) ? 0.0f : t;
    snprintf(buf, sizeof buf, "%.1f", tv);
    char* point = strchr(buf, '.');
    if (point) {
      *point = '\0';
      decimale = point + 1;
    }
  }

  display.print(buf);                                  // Integer part

  if (decimale) {
    display.setFont(&FreeSansBold12pt7b);
    display.setCursor(display.getCursorX(), base);
    display.print('.');
    display.print(decimale);                           // Smaller decimal
  }

  const int xFin = display.getCursorX();
  if (xFin + LARGEUR_UNITE_C <= PANEL_W - 1) {         // Complete "°C"
    dessinerDegre(xFin + 2, base - 13, fg);
    display.setFont(&FreeSansBold9pt7b);
    display.setCursor(xFin + 8, base);
    display.print(F("C"));
  } else if (xFin + 8 <= PANEL_W - 1) {                // Not enough space: degree only
    dessinerDegre(xFin + 2, base - 13, fg);
  }
}

static void afficherMesures(float t, float h, float v) {
  display.clearDisplay();

  dessinerHumidite(h, zoneHumidite(h), v < LOW_BATTERY_V);
  dessinerTemperature(t, zoneTemperature(t));

  display.setFont(nullptr);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  dessinerGraphique();

  display.display();
}

// ---------- Page 1: Statistics ----------

static void afficherPageStatistiques() {
  float sommeT = 0.0f;
  float sommeH = 0.0f;

  float tMin = 1000.0f;
  float tMax = -1000.0f;
  float hMin = 1000.0f;
  float hMax = -1000.0f;

  float t, h;
  uint8_t nValid = 0;
  uint8_t nOk = 0;       // Measurements with humidity in ideal range

  for (uint8_t k = 0; k < histCount; k++) {
    if (!echantillon(k, t, h)) continue;

    sommeT += t;
    sommeH += h;

    if (t < tMin) tMin = t;
    if (t > tMax) tMax = t;
    if (h < hMin) hMin = h;
    if (h > hMax) hMax = h;

    if (zoneHumidite(h) == 0) nOk++;
    nValid++;
  }

  display.clearDisplay();
  display.setFont(nullptr);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  if (nValid == 0) {
    display.setCursor(25, 28);
    display.print(F("No measurement"));
    display.display();
    return;
  }

  const float tMoy = sommeT / nValid;
  const float hMoy = sommeH / nValid;
  char b[16];

  // Column headers (simple outlines)
  display.drawRect(0, 0, 63, 10, SSD1306_WHITE);
  display.drawRect(65, 0, 63, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(13, 1);
  display.print(F("TEMP"));
  dessinerDegre(38, 1, SSD1306_WHITE);               // Drawn degree
  display.setCursor(44, 1);
  display.print(F("C"));
  display.setCursor(76, 1);
  display.print(F("HYGRO %"));
  display.setTextColor(SSD1306_WHITE);

  display.drawFastVLine(63, 12, 40, SSD1306_WHITE);

  // Temperature column: max / average / min
  snprintf(b, sizeof b, "max %.1f", tMax);
  display.setCursor(3, 14);
  display.print(b);

  display.setTextSize(2);
  display.setCursor(3, 25);
  display.printf("%.1f", tMoy);

  display.setTextSize(1);
  snprintf(b, sizeof b, "min %.1f", tMin);
  display.setCursor(3, 43);
  display.print(b);

  // Hygrometry column (integer values: DHT22 accuracy is around 2 to 5%)
  snprintf(b, sizeof b, "max %.0f", hMax);
  display.setCursor(68, 14);
  display.print(b);

  display.setTextSize(2);
  display.setCursor(68, 25);
  display.printf("%.0f%%", hMoy);

  display.setTextSize(1);
  snprintf(b, sizeof b, "min %.0f", hMin);
  display.setCursor(68, 43);
  display.print(b);

  // Footer: covered period, valid / acquired measurements, rate in ideal range.
  // An invalid acquisition counts towards duration, but not towards statistics.
  const uint32_t periodeMinutes =
      (histCount > 1)
        ? ((uint32_t)(histCount - 1) * MEASUREMENT_INTERVAL_MS) / 60000UL
        : 0;

  display.drawFastHLine(0, 53, 128, SSD1306_WHITE);

  display.setCursor(2, 56);
  display.printf("%luh%02lu",
                 (unsigned long)(periodeMinutes / 60),
                 (unsigned long)(periodeMinutes % 60));

  display.setCursor(38, 56);
  display.printf("%u/%u", (unsigned)nValid, (unsigned)histCount);

  snprintf(b, sizeof b, "H OK %d%%", (int)lroundf(100.0f * nOk / nValid));
  display.setCursor(126 - (int)strlen(b) * 6, 56);
  display.print(b);

  display.display();
}

// ---------- Page 2: Dew Point ----------

static void afficherPagePointDeRosee() {
  float sommeTd = 0.0f;
  float tdMin = 1000.0f;
  float tdMax = -1000.0f;
  float t, h;
  uint8_t nValid = 0;

  for (uint8_t k = 0; k < histCount; k++) {
    if (!echantillon(k, t, h)) continue;
    float td = calculerPointDeRosee(t, h);
    if (!isnan(td)) {
      sommeTd += td;
      if (td < tdMin) tdMin = td;
      if (td > tdMax) tdMax = td;
      nValid++;
    }
  }

  display.clearDisplay();
  display.setFont(nullptr);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  if (nValid == 0) {
    display.setCursor(25, 28);
    display.print(F("No measurement"));
    display.display();
    return;
  }

  const float tdMoy = sommeTd / nValid;
  float tDernier = NAN, hDernier = NAN;
  dernierEchantillon(tDernier, hDernier);
  float tdActuel = calculerPointDeRosee(tDernier, hDernier);

  // Page header (simple outline)
  display.drawRect(0, 0, 128, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(24, 1);
  display.print(F("DEW POINT"));
  display.setTextColor(SSD1306_WHITE);

  // Current value
  display.setCursor(3, 14);
  display.print(F("Current: "));
  if (!isnan(tdActuel)) {
    display.printf("%.1f", tdActuel);
    int x = display.getCursorX();
    dessinerDegre(x + 1, 13, SSD1306_WHITE);
    display.setCursor(x + 7, 14);
    display.print(F("C"));
  } else {
    display.print(F("--"));
  }

  // Average value over history
  display.setCursor(3, 28);
  display.print(F("Average: "));
  display.printf("%.1f", tdMoy);
  int x = display.getCursorX();
  dessinerDegre(x + 1, 27, SSD1306_WHITE);
  display.setCursor(x + 7, 28);
  display.print(F("C"));

  // Min and Max over history
  display.setCursor(3, 42);
  display.print(F("Min: "));
  display.printf("%.1f", tdMin);
  x = display.getCursorX();
  dessinerDegre(x + 1, 41, SSD1306_WHITE);

  display.setCursor(68, 42);
  display.print(F("Max: "));
  display.printf("%.1f", tdMax);
  x = display.getCursorX();
  dessinerDegre(x + 1, 41, SSD1306_WHITE);

  // --- Footer adapted for lutherie / string instruments ---
  display.drawFastHLine(0, 53, 128, SSD1306_WHITE);
  display.setCursor(2, 56);

  if (isnan(hDernier) || isnan(tdActuel)) {
    display.print(F("Status: Unknown"));
  } else if (hDernier < 40.0f) {
    display.print(F("ALERT: TOO DRY!"));     // Risk of wood cracking
  } else if (hDernier > 60.0f) {
    display.print(F("ALERT: TOO HUMID!"));  // Risk of glue debonding
  } else if (tdActuel >= 15.0f) {
    display.print(F("Condensation risk"));   // Attention to cold walls
  } else {
    display.print(F("Instruments: SAFE"));    // Ideal range reached
  }

  display.display();
}

// ---------- Page 3: Hydric Stress (Streamlined version) ----------

static void afficherPageStressHydrique() {
  uint8_t nValid = 0;
  uint8_t nSec = 0;      // < 40%
  uint8_t nOk = 0;       // 40% - 60%
  uint8_t nHumide = 0;   // > 60%
  float t, h;

  for (uint8_t k = 0; k < histCount; k++) {
    if (!echantillon(k, t, h)) continue;
    if (h < 40.0f) nSec++;
    else if (h > 60.0f) nHumide++;
    else nOk++;
    nValid++;
  }

  display.clearDisplay();
  display.setFont(nullptr);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  if (nValid == 0) {
    display.setCursor(25, 28);
    display.print(F("No measurement"));
    display.display();
    return;
  }

  int pSec = lroundf(100.0f * nSec / nValid);
  int pOk = lroundf(100.0f * nOk / nValid);
  int pHumide = lroundf(100.0f * nHumide / nValid);

  // Streamlined header
  display.drawRect(0, 0, 128, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(22, 1);
  display.print(F("HYDRIC STRESS"));

  // Compact and airy layout by tier
  display.setCursor(3, 16);
  display.printf("Dry  (<40%%) : %3d%%", pSec);

  display.setCursor(3, 28);
  display.printf("Ideal[40-60]: %3d%%", pOk);

  display.setCursor(3, 40);
  display.printf("Humid(>60%%) : %3d%%", pHumide);

  // Minimalist footer
  display.drawFastHLine(0, 52, 128, SSD1306_WHITE);
  display.setCursor(2, 55);
  
  if (pSec > 25) {
    display.print(F("Status : TOO DRY"));
  } else if (pHumide > 25) {
    display.print(F("Status : TOO HUMID"));
  } else {
    display.print(F("Status : STABLE"));
  }

  display.display();
}

// ---------- Page 4: Peaks & Alerts ----------

static void afficherPagePicsEtAlertes() {
  float t, h;
  float pMin = 100.0f;
  float pMax = 0.0f;
  int8_t indexMin = -1;
  int8_t indexMax = -1;

  // History analysis to find the worst humidity peak (dry or humid)
  for (uint8_t k = 0; k < histCount; k++) {
    if (!echantillon(k, t, h)) continue;
    if (h < pMin) {
      pMin = h;
      indexMin = k;
    }
    if (h > pMax) {
      pMax = h;
      indexMax = k;
    }
  }

  display.clearDisplay();
  display.setFont(nullptr);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  if (histCount == 0 || indexMin == -1) {
    display.setCursor(25, 28);
    display.print(F("No measurement"));
    display.display();
    return;
  }

  // Approximate age of the peak in hours
  // (most recent index = histCount - 1)
  uint8_t dernier = histCount - 1;
  uint32_t deltaMinK = (dernier >= indexMin) ? (dernier - indexMin) : (NUM_MEASUREMENTS - indexMin + dernier);
  uint32_t deltaMaxK = (dernier >= indexMax) ? (dernier - indexMax) : (NUM_MEASUREMENTS - indexMax + dernier);

  // Conversion into minutes/hours based on measurement interval
  uint32_t minutesMin = (deltaMinK * MEASUREMENT_INTERVAL_MS) / 60000UL;
  uint32_t minutesMax = (deltaMaxK * MEASUREMENT_INTERVAL_MS) / 60000UL;

  // Header
  display.drawRect(0, 0, 128, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(22, 1);
  display.print(F("PEAKS & ALERTS"));

  // Line 1: Worst recorded dryness
  display.setCursor(3, 16);
  display.printf("Worst Dry  : %.0f%%", pMin);
  display.setCursor(3, 26);
  if (minutesMin < 60) {
    display.printf("  %lum ago", (unsigned long)minutesMin);
  } else {
    display.printf("  %luh%02lu ago", (unsigned long)(minutesMin / 60), (unsigned long)(minutesMin % 60));
  }

  // Line 2: Worst recorded humidity
  display.setCursor(3, 38);
  display.printf("Worst Humid: %.0f%%", pMax);
  display.setCursor(3, 48);
  if (minutesMax < 60) {
    display.printf("  %lum ago", (unsigned long)minutesMax);
  } else {
    display.printf("  %luh%02lu ago", (unsigned long)(minutesMax / 60), (unsigned long)(minutesMax % 60));
  }

  // Separator line and footer
  display.drawFastHLine(0, 57, 128, SSD1306_WHITE);

  display.display();
}

// ---------- Page 5: Diagnostics ----------

// Diagnostic line: label on the left, value right-aligned (standard font).
static void ligneDiag(int y, const char* libelle, const char* valeur) {
  display.setCursor(3, y);
  display.print(libelle);
  display.setCursor(125 - (int)strlen(valeur) * 6, y);
  display.print(valeur);
}

static void afficherPageDiagnostic(float v) {
  // Count invalid acquisitions and check status of the latest one
  float t, h;
  uint8_t nValid = 0;
  for (uint8_t k = 0; k < histCount; k++) {
    if (echantillon(k, t, h)) nValid++;
  }
  const bool dernierOk = (histCount > 0) && echantillon(histCount - 1, t, h);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Battery icon: 4 segments depending on voltage
  const int bx = 3, by = 3, bw = 46, bh = 24;
  display.drawRect(bx, by, bw, bh, SSD1306_WHITE);
  display.fillRect(bx + bw, by + 7, 3, 10, SSD1306_WHITE);

  float frac = (v - CUTOFF_V) / (BATT_FULL_V - CUTOFF_V);
  if (frac < 0.0f) frac = 0.0f;
  if (frac > 1.0f) frac = 1.0f;
  const int segments = (int)ceilf(frac * 4.0f);
  for (int i = 0; i < 4; i++) {
    if (i < segments) {
      display.fillRect(bx + 3 + i * 10, by + 3, 8, bh - 6, SSD1306_WHITE);
    }
  }

  // Voltage and status
  display.setFont(&FreeSansBold12pt7b);
  display.setCursor(62, 22);
  display.printf("%.2f", v);
  const int xv = display.getCursorX();
  display.setFont(&FreeSansBold9pt7b);
  display.setCursor(xv + 2, 22);
  display.print(F("V"));

  display.setFont(nullptr);
  display.setCursor(62, 25);
  if (v < CUTOFF_V) display.print(F("CRITICAL"));
  else if (v < LOW_BATTERY_V) display.print(F("LOW"));
  else display.print(F("OK"));

  display.drawFastHLine(0, 34, 128, SSD1306_WHITE);

  // Diagnostic lines
  char b[14];

  ligneDiag(38, "DHT22 Sensor", dernierOk ? "OK" : "ERROR");

  // Share of time spent in light sleep: measures wakefulness attributable to the program
  const float veillePct = (uptimeMs > 0)
                            ? (100.0f * (float)sommeilMs / (float)uptimeMs)
                            : 0.0f;
  snprintf(b, sizeof b, "%.1f%%", veillePct);
  ligneDiag(47, "Sleep", b);

  const unsigned long minutes = (unsigned long)(uptimeMs / 60000ULL);
  if (minutes >= 1440UL) {
    snprintf(b, sizeof b, "%lud %02luh", minutes / 1440UL, (minutes % 1440UL) / 60UL);
  } else {
    snprintf(b, sizeof b, "%luh %02lum", minutes / 60UL, minutes % 60UL);
  }
  ligneDiag(56, "Uptime", b);

  display.display();
}

// ---------- Display Router ----------
// No DHT22 access here: display never modifies history.
static void rafraichirEcran(float tLive, float hLive) {
  if (!oledOk) return;

  if (pageActuelle == 0) {
    float t = tLive;
    float h = hLive;

    if (isnan(t) || isnan(h)) {
      t = NAN;
      h = NAN;
      dernierEchantillon(t, h);
    }

    afficherMesures(t, h, lireTension());
  } else if (pageActuelle == 1) {
    afficherPageStatistiques();
  } else if (pageActuelle == 2) {
    afficherPagePointDeRosee();
  } else if (pageActuelle == 3) {
    afficherPageStressHydrique();
  } else if (pageActuelle == 4) {
    afficherPagePicsEtAlertes(); // Page 5: Peaks & Alerts
  } else {
    afficherPageDiagnostic(lireTension()); // Page 6: Diagnostics
  }
}

// ---------- Screen Management ----------

static void allumerEcran() {
  if (!oledOk) oledOk = initOLED();
  if (!oledOk) return;

  // Single unstored reading.
  // Does not modify statistics or acquisition rate.
  float t = NAN;
  float h = NAN;
  lireDHT(t, h, 1);

  display.ssd1306_command(SSD1306_DISPLAYON);
  rafraichirEcran(t, h);

  ecranAllume = true;
  ecranDebut = millis();
}

static void eteindreEcran() {
  if (oledOk) {
    animationExtinction();
    oledOff();
  }

  ecranAllume = false;
}

// ---------- Battery Protection ----------

static void arretProtecteur(float v) {
  LOG("Critical battery %.2f V : shutdown\n", v);

  if (!oledOk) oledOk = initOLED();

  if (oledOk) {
    afficherPageDiagnostic(v);   // Empty icon and CRITICAL status
    delay(4000);
    oledOff();
  }

  ESP.deepSleep(0); // Recovery only by reset
}

// ---------- Periodic Measurement ----------

static void mesurer() {
  float t = NAN;
  float h = NAN;

  const bool ok = lireDHT(t, h, DHT_TRIES);
  ajouterMesure(ok, t, h);

  const float v = lireTension();

  if (ok) {
    LOG("T=%.1f C  H=%.1f %%  Vbat=%.2f V\n", t, h, v);
  } else {
    LOG("DHT Failure  Vbat=%.2f V\n", v);
  }

  if (v < CUTOFF_V) arretProtecteur(v);

  if (ecranAllume) {
    // Redraw current page (stored values, statistics, or diagnostics).
    rafraichirEcran(NAN, NAN);
  }
}

// ---------- Light Sleep ----------

// Called by the SDK upon wake-up (timer or button).
static void wakeCb() {
  gpio_pin_wakeup_disable();

  if (digitalRead(BUTTON_PIN) == LOW) {
    boutonReveil = true;
    LOG("wakeCb : button detected\n");
  }
}

// Returns true if the SDK accepted the light sleep request.
static bool dormirTranche(uint32_t ms) {
  wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);
  wifi_fpm_open();

  gpio_pin_wakeup_enable(GPIO_ID_PIN(BUTTON_PIN), GPIO_PIN_INTR_LOLEVEL);
  wifi_fpm_set_wakeup_cb(wakeCb);

  const bool accepte = (wifi_fpm_do_sleep(ms * 1000UL) == 0);
  if (accepte) {
    delay(ms + 1);
  } else {
    delay(ms);
  }

  gpio_pin_wakeup_disable();
  return accepte;
}

// ---------- Program ----------

void setup() {
#if DEBUG_SERIAL
  Serial.begin(115200);
  LOG("\nTHL v%s (light sleep)%s\n",
      FW_VERSION, TEST_FAST ? " [TEST_FAST]" : "");
#endif

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Radio turned off to limit power consumption.
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  delay(10);

  for (uint8_t i = 0; i < NUM_MEASUREMENTS; i++) {
    histTemp[i] = T_NONE;
    histHum[i] = H_NONE;
  }

  histHead = 0;
  histCount = 0;

  dht.begin();

  dernierMillis = millis();

  mesurer();                 // First stored acquisition
  derniereMesure = millis();

  pageActuelle = 0;
  allumerEcran();            // Initial display on page 1
}

void loop() {
  // Cumulative operating time (independent of millis overflow)
  uint32_t now = millis();
  uptimeMs += (uint32_t)(now - dernierMillis);
  dernierMillis = now;

  // 1. Button detection: wake-up or page change.
  const bool bas = (digitalRead(BUTTON_PIN) == LOW);
  bool evenement = boutonReveil || (bas && boutonRelache);

  boutonReveil = false;
  boutonRelache = !bas;

  // Debounce: release bounce must not count as a new press.
  if (evenement && (int32_t)(now - dernierAppui) < (int32_t)DEBOUNCE_MS) {
    evenement = false;
  }

  if (evenement) {
    LOG("Button press\n");

    if (!ecranAllume) {
      // Wake-up: always return to page 1.
      pageActuelle = 0;
      allumerEcran();
    } else {
      // Screen already on: next page.
      pageActuelle = (pageActuelle + 1) % NB_PAGES;
      rafraichirEcran(NAN, NAN);
      ecranDebut = millis();
    }

    dernierAppui = millis();
  }

  // 2. Automatic screen timeout.
  now = millis();
  if (ecranAllume && (int32_t)(now - ecranDebut) >= (int32_t)DISPLAY_MS) {
    eteindreEcran();
  }

  // 3. Periodic measurement independent of display.
  if (now - derniereMesure >= MEASUREMENT_INTERVAL_MS) {
    derniereMesure = now;
    mesurer();
  }

  // 4. Intelligent waiting
  //  - button held : short pause
  //  - screen on   : stay awake (no sleep) so every press is read immediately
  //  - screen off  : light sleep in short slices (the button wakes the CPU).
  //    A single long sleep until the next measurement is avoided: the SDK limits one
  //    light sleep to 0xFFFFFFF us (~268 s), and after an early wake-up by the button
  //    the delay() that follows keeps running until the end of the requested duration.
  if (bas) {
    delay(20);
  } else if (ecranAllume) {
    delay(10);
  } else {
    const uint32_t t0 = millis();
    if (dormirTranche(SLICE_MS)) {
      sommeilMs += (uint32_t)(millis() - t0);   // only sleeps accepted by the SDK count
    }
  }
}
