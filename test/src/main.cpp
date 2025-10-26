#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// --- Wi-Fi and MQTT Configuration ---
const char* ssid = "Nyiwg 9A"; // Your Wi-Fi Name
const char* password = "aaaaa11111"; // Your Wi-Fi Password
const char* mqttServer = "test.mosquitto.org"; // MQTT Broker Host
const int mqttPort = 1883;
const char* mqttClientId = "ESP32Client-AuraLink-V1"; // Unique ID for the client

// --- MQTT TOPICS (Must match Python backend) ---
#define TOPIC_SENSOR_DATA "auralink/sensor/data"
#define TOPIC_DISPLAY_QUOTE "auralink/display/quote"
#define TOPIC_DISPLAY_SUMMARY "auralink/display/summary"
#define TOPIC_URGENCY_LED "auralink/urgency/led"

// --- Hardware Definitions ---
#define DHTPIN 4
#define DHTTYPE DHT22
#define LDR_AO 34
#define LDR_DO 25
#define NOX_PIN 35
#define PIR_PIN 26
#define LED_TEMP_PIN 27      // Temperature LED
#define LED_LIGHT_PIN 33     // Light Level LED
#define LED_NOX_PIN 18       // NOx/Air Quality Status LED
#define LED_PIR_PIN 19       // PIR Motion Blink LED
#define LED_URGENCY_PIN 5    // **NEW**: Dedicated LED for Urgency (e.g., Red LED)

// I2C LCD (address confirmed by scanner)
#define I2C_SDA 21
#define I2C_SCL 22
LiquidCrystal_I2C lcd(0x27, 20, 4);

DHT dht(DHTPIN, DHTTYPE);

// --- Communication Objects ---
WiFiClient espClient;
PubSubClient client(espClient);

// --- Non-Blocking Blinking Variables for PIR LED ---
unsigned long previousMillisPIR = 0;
const long intervalPIR = 100;
int ledStatePIR = LOW;

// --- Helper Functions Declaration ---
void connectToWiFi();
void reconnectMQTT();
void callback(char* topic, byte* payload, unsigned int length);
void printLineFmt(uint8_t row, const char* fmt, ...);

// ------------------------------------------------------------------
// --- Helper: Print Padded LCD Line ---
// ------------------------------------------------------------------
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

// ------------------------------------------------------------------
// --- MQTT Callback: Handles Messages from Backend ---
// ------------------------------------------------------------------
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  // Convert payload to String
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  if (strcmp(topic, TOPIC_DISPLAY_QUOTE) == 0) {
    // Show Quote on the first two lines
    lcd.clear();
    printLineFmt(0, "Quote:");
    // Display the first part of the quote
    printLineFmt(1, "%s", message.substring(0, 20).c_str()); 
  } else if (strcmp(topic, TOPIC_DISPLAY_SUMMARY) == 0) {
    // Show Summary on the last two lines
    printLineFmt(2, "Summary:");
    // Display the first part of the summary
    printLineFmt(3, "%s", message.substring(0, 20).c_str());
  } else if (strcmp(topic, TOPIC_URGENCY_LED) == 0) {
    // Control the Urgency LED based on the one-word response
    if (message.indexOf("HIGH") != -1) {
      digitalWrite(LED_URGENCY_PIN, HIGH); // Turn on for HIGH urgency
    } else if (message.indexOf("MEDIUM") != -1) {
      // Could implement a slow blink for MEDIUM
    } else {
      digitalWrite(LED_URGENCY_PIN, LOW); // Turn off for LOW urgency
    }
  }
}

// ------------------------------------------------------------------
// --- MQTT Connection Logic ---
// ------------------------------------------------------------------
void reconnectMQTT() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(mqttClientId)) {
      Serial.println("connected");
      // Subscribe to topics where the backend publishes data
      client.subscribe(TOPIC_DISPLAY_QUOTE);
      client.subscribe(TOPIC_DISPLAY_SUMMARY);
      client.subscribe(TOPIC_URGENCY_LED);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" trying again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

// ------------------------------------------------------------------
// --- WiFi Connection Logic ---
// ------------------------------------------------------------------
void connectToWiFi() {
  Serial.println("\nAttempting to connect to WiFi network: " + String(ssid));
  lcd.clear();
  printLineFmt(0, "Connecting to WiFi...");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    if (attempts % 5 == 0) {
      Serial.println();
      Serial.printf("Attempt %d - WiFi status: %d\n", attempts + 1, WiFi.status());
    }
    printLineFmt(1, "Attempt: %d", attempts + 1);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    printLineFmt(0, "WiFi Connected!");
    printLineFmt(1, "IP: %s", WiFi.localIP().toString().c_str());
    delay(1500);
  } else {
    Serial.println("\nFailed to connect to WiFi.");
    printLineFmt(0, "WiFi Failed!");
    printLineFmt(1, "Check Credentials");
    delay(5000);
    ESP.restart(); // Restart if connection fails
  }
}

// ------------------------------------------------------------------
// --- Setup ---
// ------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\nAuraLink ESP32 Starting...");
  dht.begin();
  Serial.println("DHT sensor initialized");

  // Initialize Pins
  pinMode(LDR_DO, INPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_TEMP_PIN, OUTPUT);
  pinMode(LED_LIGHT_PIN, OUTPUT);
  pinMode(LED_NOX_PIN, OUTPUT);
  pinMode(LED_PIR_PIN, OUTPUT);
  pinMode(LED_URGENCY_PIN, OUTPUT); // **Setup NEW Urgency LED**
  Serial.println("All pins initialized");

  // Explicit I2C pins for ESP32
  Wire.begin(I2C_SDA, I2C_SCL);

  // LCD init
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("AuraLink ESP32 Start");
  delay(800);

  // Connection Setup
  connectToWiFi();
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
}

// ------------------------------------------------------------------
// --- Main Loop ---
// ------------------------------------------------------------------
void loop() {
  // Check and maintain MQTT connection
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop(); // Required to process incoming MQTT messages

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
    delay(1000);
    return;
  }

  // --- Serial Output ---
  Serial.printf("Temp: %.1f C | Hum: %.1f %% | Light: %d%% | NOx: %d%% | PIR: %d\n",
                t, h, ldrPercent, noxPercent, pirState);

  // =========================================================
  // --- Publish Sensor Data to Backend ---
  // =========================================================
  // Create JSON payload
  char jsonBuffer[256];
  snprintf(jsonBuffer, sizeof(jsonBuffer), 
           "{\"temperature\":%.1f, \"humidity\":%.1f, \"light_percent\":%d, \"nox_percent\":%d}",
           t, h, ldrPercent, noxPercent);
  
  // Publish the data
  client.publish(TOPIC_SENSOR_DATA, jsonBuffer);
  Serial.printf("Published to %s: %s\n", TOPIC_SENSOR_DATA, jsonBuffer);

  // =========================================================
  // --- Local LED Logic (UNCHANGED) ---
  // =========================================================
  
  // NEW LED Logic 1: NOx / Air Quality (LED_NOX_PIN)
  if (noxPercent <= 30) {
    digitalWrite(LED_NOX_PIN, LOW);
  } else if (noxPercent > 60) {
    digitalWrite(LED_NOX_PIN, HIGH);
  } else {
    // Caution: BLINK (Blocking delay is less impactful due to long main delay)
    digitalWrite(LED_NOX_PIN, HIGH);
    delay(100); 
    digitalWrite(LED_NOX_PIN, LOW);
    delay(100); 
  }

  // NEW LED Logic 2: PIR Motion (LED_PIR_PIN) - Non-Blocking
  if (pirState == HIGH) {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillisPIR >= intervalPIR) {
      previousMillisPIR = currentMillis;
      ledStatePIR = !ledStatePIR;
      digitalWrite(LED_PIR_PIN, ledStatePIR);
    }
  } else {
    digitalWrite(LED_PIR_PIN, LOW);
    ledStatePIR = LOW;
  }
  
  // Original LED Logic: Temperature Alert (LED_TEMP_PIN)
  if (t > 30 || t < 20) {
    digitalWrite(LED_TEMP_PIN, HIGH);
    delay(150);
    digitalWrite(LED_TEMP_PIN, LOW);
    delay(150);
  } else {
    digitalWrite(LED_TEMP_PIN, HIGH);
  }

  // Original LED Logic: Light Level Indication (LED_LIGHT_PIN)
  digitalWrite(LED_LIGHT_PIN, (ldrPercent > 50) ? LOW : HIGH);

  // IMPORTANT: Set a long delay for data publishing and WDT management
  delay(3000); 
}