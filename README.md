# ESP8266 Temperature and Humidity Monitor with OLED Display

This project provides an ESP8266-based solution for monitoring temperature and humidity with a real-time display on an OLED screen. It also includes a low battery warning feature.

**Features:**

* Measures temperature and humidity using a DHT22 sensor.
* Displays the current readings, including a basic temperature graph, on a 128x64 OLED screen.
* Alerts for low battery voltage.
* Configurable measurement interval and display duration.
* Low-power design with Wi-Fi disabled and ESP deep sleep during screen off periods.

**Hardware Requirements:**

* ESP8266 development board (e.g., NodeMCU, D1 Mini)
* DHT22 temperature and humidity sensor
* 128x64 OLED display (e.g., SSD1306)
* Pushbutton switch (optional)
* Breadboard and jumper wires

**Software Requirements:**

* Arduino IDE
* ESP8266 libraries:
    * Adafruit_GFX
    * Adafruit_SSD1306
    * DHT

**Installation:**

1. Download the code from this repository.
2. Install the required libraries in the Arduino IDE:
    * Go to **Sketch > Include Library > Manage Libraries**.
    * Search for and install the mentioned libraries.
3. Connect your ESP8266 board, OLED display, and DHT22 sensor according to the wiring diagram provided (if applicable).
4. Modify the configuration constants in the code (`BUTTON_PIN`, `SCREEN_WIDTH`, `SCREEN_HEIGHT`, etc.) to match your setup.
5. Upload the code to your ESP8266 board using the Arduino IDE.

**Usage:**

* Power on your ESP8266 board.
* The OLED screen will display the current temperature, humidity, voltage, and a basic temperature graph.
* The temperature and humidity readings will update periodically at the configured interval.
* Press the button (if connected) to turn on the OLED screen for the set display duration.

**Additional Notes:**

* The code utilizes deep sleep mode to reduce power consumption when the OLED screen is off.
* Feel free to customize the code for your specific needs, such as modifying the displayed information on the screen or adding data logging features.

**License:**

This code is licensed under the MIT License (see LICENSE file)

