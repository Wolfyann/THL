#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <user_interface.h>

// Firmware version
#define VERSION_  1.3

// Push-button configuration
#define BUTTON_PIN 12

// OLED screen configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// DHT sensor configuration
#define DHTPIN 14
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define  LOW_BATTERY_THRESHOLD    2.85
#define  DISPLAY_DURATION_MS      30000    // Display time in milliseconds
#define  MEASUREMENT_INTERVAL_MS  1800000  // 30 minutes
#define  NUM_MEASUREMENTS         24       // 12 hours with measurement every 30 minutes

struct Button {
  const uint8_t PIN;
  bool pressed;
};

// Initialize ADC mode for voltage measurement
ADC_MODE(ADC_VCC);

// Global parameters
float temperatureMeasurements[NUM_MEASUREMENTS];
float humidityMeasurements[NUM_MEASUREMENTS];
uint8_t measurementIndex = 0;

//variables to keep track of the timing of recent interrupts
unsigned long currentTime = 0;
unsigned long previousTime = 0;
unsigned long screenOffTime = 0;

Button button1 = {12, false};

void ICACHE_RAM_ATTR isr() {
  if (currentTime - previousTime > 250)    // avoid multiples pushs
  {
    button1.pressed = true;
    previousTime = currentTime;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(2000);

  pinMode(button1.PIN, INPUT_PULLUP);
  attachInterrupt(button1.PIN, isr, FALLING);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Erreur allocation SSD1306")); // save SRAM by moving cste to FLASH
    while (true);  // Block on screen failure
  }
  else {
    display.clearDisplay();
    display.setTextColor(WHITE);
    afficherEcran();
  }

  Serial.printf("Release %02f", VERSION_);

  // Energy-saving configuration
  wifi_set_opmode_current(NULL_MODE);
  wifi_fpm_set_sleep_type(MODEM_SLEEP_T);
  wifi_fpm_open();
  wifi_fpm_do_sleep(0xFFFFFFF);

  dht.begin();
  memset(temperatureMeasurements, NAN, sizeof(temperatureMeasurements));
  memset(humidityMeasurements, NAN, sizeof(humidityMeasurements));
  // Initial sensor reading and measurement display
  lireCapteurs(); 
  // start sequence
  afficherEcran();  
  screenOffTime = millis() + DISPLAY_DURATION_MS;
}

void loop() {
  currentTime = millis();

  // OLED management
  if (currentTime > screenOffTime) {
    eteindreOLED();
  }

  // Buton management
  if (button1.pressed) {
    afficherEcran();
    screenOffTime = currentTime + DISPLAY_DURATION_MS;
    button1.pressed = false;
  }

  // Periodic reading of sensors
  if (currentTime - previousTime >= MEASUREMENT_INTERVAL_MS) {
    lireCapteurs();
    previousTime = currentTime;
  }
}

void afficherEcran() {
  display.ssd1306_command(SSD1306_DISPLAYON);
  display.clearDisplay();

  float temperature = temperatureMeasurements[(measurementIndex - 1 + NUM_MEASUREMENTS) % NUM_MEASUREMENTS];
  float humidity = humidityMeasurements[(measurementIndex - 1 + NUM_MEASUREMENTS) % NUM_MEASUREMENTS];
  float batteryVoltage = (1.1 * ESP.getVcc()) / 1023;

  // Display measurements
  afficherMesures(temperature, humidity, batteryVoltage);
}

void eteindreOLED() {
  fillrect();
  display.ssd1306_command(SSD1306_DISPLAYOFF);  // Switching off the OLED screen
}

void lireCapteurs() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println(F("Erreur de lecture du capteur DHT !"));
    return;
  }

  // Saving measurement
  temperatureMeasurements[measurementIndex] = temperature;
  humidityMeasurements[measurementIndex] = humidity;
  measurementIndex = (measurementIndex + 1) % NUM_MEASUREMENTS;

  // update screen if activated
  if (currentTime <= screenOffTime) {
    afficherMesures(temperature, humidity, (1.1 * ESP.getVcc()) / 1023);
  }
  Serial.printf("Température: %.1f °C, Humidité: %.1f %%\n", temperature, humidity);
}

void fillrect(void) {
  for (int16_t i = 0; i < display.height() / 2; i++) {
    // The INVERSE color is used so rectangles alternate white/black
    display.fillRect(i, i, display.width() - i * 2, display.height() - i * 2, INVERSE);
    display.display(); // Update screen with each newly-drawn rectangle
    delay(5);
    display.clearDisplay();
  }
}

void afficherMesures(float temperature, float humidity, float batteryVoltage) {
  display.clearDisplay();

  // Print temperature
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Temperature");
  display.setTextSize(2);
  display.setCursor(0, 10);
  display.printf("%.2f", temperature);
  display.setTextSize(1);
  display.cp437(true);
  display.write(167);
  display.setTextSize(2);
  display.print("C");

  // Print humidity
  display.setTextSize(1);
  display.setCursor(0, 35);
  display.print("Humidity");
  display.setTextSize(2);
  display.setCursor(0, 45);
  display.printf("%.2f%%", humidity);

  // Monitoring voltage
  display.setTextSize(1);
  display.setCursor(85, 5);
  display.print("Voltage");
  display.setCursor(90, 15);
  display.printf("%.2f V", batteryVoltage);

  // Low battery warning
  if (batteryVoltage < LOW_BATTERY_THRESHOLD) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 10);
    display.print("Low battery !");
    display.setCursor(0, 25);
    display.printf("%.2f V", batteryVoltage);
  }

  // Gfx drawing
  dessinerGraphique();
  // show on OLED
  display.display();
  delay(250);
}

void dessinerGraphique() {
  int graphWidth = 40;  // Gfx Width
  int graphHeight = 35; // Gfx height
  int graphX = (SCREEN_WIDTH - graphWidth) - 2; // Position X Gfx position
  int graphY = (SCREEN_HEIGHT - graphHeight) - 2; // Position Y Gfx position

  // Border on Gfx
  display.drawRect(graphX, graphY, graphWidth, graphHeight, WHITE);

  // Calculation of minimum and maximum temperature values
  float tempMin = -50, tempMax = 80;
  float humMin = 0, humMax = 100;

  // Get min and max thresholds to adjust scale
  tempMin = *std::min_element(temperatureMeasurements, temperatureMeasurements + NUM_MEASUREMENTS);
  tempMax = *std::max_element(temperatureMeasurements, temperatureMeasurements + NUM_MEASUREMENTS);

  // Drawing data
  for (uint8_t i = 0; i < NUM_MEASUREMENTS - 1; i++) {
    if (!isnan(temperatureMeasurements[i]) && !isnan(temperatureMeasurements[i + 1])) {
      int x1 = graphX + i * graphWidth / (NUM_MEASUREMENTS - 1);
      int y1 = graphY + graphHeight - map(temperatureMeasurements[i], tempMax / 4, tempMax + 2, 0, graphHeight);
      int x2 = graphX + (i + 1) * graphWidth / (NUM_MEASUREMENTS - 1);
      int y2 = graphY + graphHeight - map(temperatureMeasurements[i + 1], tempMax / 4, tempMax + 2, 0, graphHeight);
      display.drawLine(x1, y1, x2, y2, WHITE);
    }

    if (!isnan(humidityMeasurements[i]) && !isnan(humidityMeasurements[i + 1])) {
      int x1 = graphX + i * graphWidth / (NUM_MEASUREMENTS - 1);
      int y1 = graphY + graphHeight - map(humidityMeasurements[i], 10, 95, 0, graphHeight);
      display.drawPixel(x1, y1, WHITE); // Point 
    }
  }

    // Draw vertical line for current index
  int currentX = graphX + measurementIndex * graphWidth / (NUM_MEASUREMENTS - 1);
  display.drawLine(currentX, graphY + 15 , currentX, graphY + (graphHeight - 15), WHITE);

  // Puts "T" and "H" labels
  display.setTextSize(1);
  display.setCursor(graphX - 8, graphY + graphHeight - map(humidityMeasurements[0] + 2, 10, 95, 0, graphHeight));
  display.print("H");
  display.setCursor(graphX - 8, graphY + graphHeight - map(temperatureMeasurements[0] + 2, tempMin, tempMax + 2, 0, graphHeight));
  display.print("T");
}
