#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <ArduinoJson.h>

// WiFi Configuration
const char* ssid = "YourWiFiSSID";
const char* password = "YourWiFiPassword";

// FastAPI Server Configuration
const char* fastApiHost = "YOUR_COMPUTER_IP"; // Example: "192.168.1.100"
const int fastApiPort = 8000;
const char* fastApiEndpoint = "/data";

// MQTT Configuration
const char* mqttServer = "test.mosquitto.org";
const int mqttPort = 1883;

// MQTT Topics
const char* TOPIC_QUOTE = "auralink/display/quote";
const char* TOPIC_SUMMARY = "auralink/display/summary";
const char* TOPIC_URGENCY = "auralink/urgency/led";

// --- Sensor Definitions ---
#define DHTPIN 4
#define DHTTYPE DHT22
#define LDR_AO 34     
#define LDR_DO 25     
#define NOX_PIN 35    
#define PIR_PIN 26
#define LED_TEMP_PIN 27   // Renamed for clarity
#define LED_LIGHT_PIN 33

// --- NEW LED Definitions ---
#define LED_NOX_PIN 18    // NEW Pin for NOx/Air Quality Status
#define LED_PIR_PIN 19    // NEW Pin for PIR Motion Blink

// I2C LCD (address confirmed by scanner)
#define I2C_SDA 21
#define I2C_SCL 22
LiquidCrystal_I2C lcd(0x27, 20, 4);

DHT dht(DHTPIN, DHTTYPE);

// --- Non-Blocking Blinking Variables for PIR LED ---
unsigned long previousMillisPIR = 0;
const long intervalPIR = 100; // Blink interval for PIR (100ms ON, 100ms OFF)
int ledStatePIR = LOW;

// helper: print padded line (clears leftover chars)
void printLineFmt(uint8_t row, const char* fmt, ...) {
  char buf[21]; 
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  lcd.setCursor(0, row);
  lcd.print(buf);
  // pad to end of line
  uint8_t len = strlen(buf);
  for (uint8_t i = len; i < 20; i++) lcd.print(' ');
}

void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(LDR_DO, INPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_TEMP_PIN, OUTPUT);
  pinMode(LED_LIGHT_PIN, OUTPUT);
  
  // --- Setup NEW LED Pins ---
  pinMode(LED_NOX_PIN, OUTPUT); 
  pinMode(LED_PIR_PIN, OUTPUT);

  // Explicit I2C pins for ESP32
  Wire.begin(I2C_SDA, I2C_SCL);

  // LCD init
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sensors Initializing");
  delay(800);
}

void loop() {
  // --- Sensor Readings ---
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  int ldrAnalog = analogRead(LDR_AO); 
  int ldrPercent = map(ldrAnalog, 4095, 0, 0, 100);
  ldrPercent = constrain(ldrPercent, 0, 100);

  int ldrDigital = digitalRead(LDR_DO);
  int noxRaw = analogRead(NOX_PIN);
  int noxPercent = map(noxRaw, 0, 4095, 0, 100);
  noxPercent = constrain(noxPercent, 0, 100);

  int pirState = digitalRead(PIR_PIN);

  // --- DHT Error Check ---
  if (isnan(h) || isnan(t)) {
    Serial.println("DHT22 read error");
    printLineFmt(0, "DHT22 Error");
    printLineFmt(1, "Check wiring");
    printLineFmt(2, " ");
    printLineFmt(3, " ");
    delay(1000);
    return;
  }

  // --- Status Variables ---
  const char* lightStatus = ldrDigital ? "LOW" : "HIGH"; 
  const char* motionStatus = pirState ? "Motion Detected" : "No Motion";

  // --- Serial Output ---
  Serial.printf("Temp: %.1f C | Hum: %.1f %% | Light: %d%% | DO: %s | NOx: %d%% | PIR: %s\n",
                 t, h, ldrPercent, lightStatus, noxPercent, motionStatus);

  // --- LCD Output (use print + padding) ---
  printLineFmt(0, "T:%4.1fC  H:%4.1f%%  L:%3d%%", t, h, ldrPercent);
  printLineFmt(1, "Light Level: %-4s", lightStatus);
  printLineFmt(2, "NOx:%3d%% Raw:%4d", noxPercent, noxRaw);
  printLineFmt(3, "PIR: %s", motionStatus);

  // =========================================================
  // --- NEW LED Logic 1: NOx / Air Quality (LED_NOX_PIN) ---
  // =========================================================
  if (noxPercent <= 30) {
    // Safe: 0-30% NOx -> LED OFF
    digitalWrite(LED_NOX_PIN, LOW);
  } else if (noxPercent > 60) {
    // High Pollution: >60% NOx -> LED ON
    digitalWrite(LED_NOX_PIN, HIGH);
  } else {
    // Caution: 31-60% NOx -> BLINK (using blocking delay for simplicity here)
    // NOTE: This BLOCKS the program, but is acceptable since the main loop has a long delay.
    digitalWrite(LED_NOX_PIN, HIGH);
    delay(200);
    digitalWrite(LED_NOX_PIN, LOW);
    delay(200);
  }

  // =========================================================
  // --- NEW LED Logic 2: PIR Motion (LED_PIR_PIN) ---
  // =========================================================
  if (pirState == HIGH) {
    // H Mode: Motion Detected -> BLINK (Non-Blocking using millis())
    unsigned long currentMillis = millis();

    if (currentMillis - previousMillisPIR >= intervalPIR) {
      previousMillisPIR = currentMillis;
      ledStatePIR = !ledStatePIR; // Toggle the state
      digitalWrite(LED_PIR_PIN, ledStatePIR);
    }
  } else {
    // No Motion -> LED OFF
    digitalWrite(LED_PIR_PIN, LOW);
    // Reset state so it starts HIGH when motion is detected next
    ledStatePIR = LOW; 
  }
  
  // =========================================================
  // --- Original LED Logic (Temp and Light) ---
  // =========================================================
  
  // Temperature Alert (using blocking delay as before)
  if (t > 30) {
    digitalWrite(LED_TEMP_PIN, HIGH);
    delay(150);
    digitalWrite(LED_TEMP_PIN, LOW);
    delay(150);
  } else if (t < 20) {
    digitalWrite(LED_TEMP_PIN, HIGH);
    delay(500);
    digitalWrite(LED_TEMP_PIN, LOW);
    delay(500);
  } else {
    digitalWrite(LED_TEMP_PIN, HIGH);
  }

  // Light Level Indication 
  digitalWrite(LED_LIGHT_PIN, (ldrPercent > 50) ? LOW : HIGH); // Assuming HIGH=OFF when bright

  // Refresh rate (This delay is critical for the ESP32 WDT)
  delay(2000);
}