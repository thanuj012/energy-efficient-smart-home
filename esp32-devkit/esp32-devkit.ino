#include <WiFi.h>
#include <HTTPClient.h>
#include <math.h>

// -------- WIFI --------
const char* ssid = "MADTITAN";
const char* password = "iambatmanhehe";

// >>> PUT S3 BOX IP HERE <<<
String serverURL = "http://10.220.110.251/data";

// -------- RELAYS --------
#define RELAY1 32
#define RELAY2 33
#define RELAY3 25

// -------- SENSORS --------
#define PIR_PIN 4
#define LDR_PIN 34
#define ACS_PIN 35

// -------- ACS712 SETTINGS --------
#define ADC_MAX     4095.0
#define VREF        3.3
#define SENSITIVITY 0.185   // ACS 5A
#define MAX_CURRENT 5.0

float zeroVoltage = 1.65;
float energy_kWh = 0;
unsigned long lastTime = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);

  digitalWrite(RELAY1, LOW);
  digitalWrite(RELAY2, LOW);
  digitalWrite(RELAY3, LOW);

  pinMode(PIR_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);

  analogSetPinAttenuation(LDR_PIN, ADC_11db);

  Serial.println("System warmup...");
  delay(5000);

  // ===== ZERO CURRENT CALIBRATION =====
  long sum = 0;
  for (int i = 0; i < 500; i++) {
    sum += analogRead(ACS_PIN);
    delayMicroseconds(200);
  }
  zeroVoltage = (sum / 500.0) * (VREF / ADC_MAX);

  Serial.print("Zero Voltage: ");
  Serial.println(zeroVoltage, 3);

  // ===== WIFI =====
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());

  lastTime = millis();
}

float readCurrentRMS() {
  const int samples = 400;
  float sumSquares = 0;

  for (int i = 0; i < samples; i++) {
    float adc = analogRead(ACS_PIN);
    float voltage = adc * (VREF / ADC_MAX);
    float centered = voltage - zeroVoltage;
    sumSquares += centered * centered;
    delayMicroseconds(150);
  }

  float vrms = sqrt(sumSquares / samples);
  float irms = vrms / SENSITIVITY;

  if (irms < 0) irms = 0;
  if (irms > MAX_CURRENT) irms = MAX_CURRENT;

  return irms;
}

void loop() {

  int motion = digitalRead(PIR_PIN);

  // 🔴 RAW LDR VALUE
  int ldrRaw = analogRead(LDR_PIN);

  float current = readCurrentRMS();
  float voltage = 230.0;
  float power = voltage * current;

  unsigned long now = millis();
  float hours = (now - lastTime) / 3600000.0;
  lastTime = now;

  energy_kWh += (power * hours) / 1000.0;

  // ===== SERIAL DEBUG =====
  Serial.print("RAW LDR: ");
  Serial.print(ldrRaw);
  Serial.print(" | Current: ");
  Serial.print(current,3);
  Serial.println(" A");

  // ===== JSON =====
  String json = "{";
  json += "\"motion\":" + String(motion) + ",";
  json += "\"current\":" + String(current,3) + ",";
  json += "\"energy\":" + String(energy_kWh,3) + ",";
  json += "\"ldr\":" + String(ldrRaw) + ",";
  json += "\"power\":" + String(power,1);
  json += "}";

  Serial.println(json);

  if (WiFi.status() == WL_CONNECTED) {

    HTTPClient http;
    http.begin(serverURL);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(json);

    if (code > 0) {
      String response = http.getString();
      Serial.println("S3 CMD: " + response);

      // ===== COMMANDS FROM S3 =====
      if (response == "DEVICE1_ON") digitalWrite(RELAY1, HIGH);
      if (response == "DEVICE1_OFF") digitalWrite(RELAY1, LOW);

      if (response == "DEVICE2_ON") digitalWrite(RELAY2, HIGH);
      if (response == "DEVICE2_OFF") digitalWrite(RELAY2, LOW);

      if (response == "DEVICE3_ON") digitalWrite(RELAY3, HIGH);
      if (response == "DEVICE3_OFF") digitalWrite(RELAY3, LOW);

      if (response == "ALL_OFF") {
        digitalWrite(RELAY1, LOW);
        digitalWrite(RELAY2, LOW);
        digitalWrite(RELAY3, LOW);
      }
    }

    http.end();
  }

  delay(2000);
}
