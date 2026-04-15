#include <Arduino.h>
#include <Wire.h>
#include <DFRobot_MAX30102.h>
#include <MPU6050_light.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <NTPClient.h>
#include <WiFiUDP.h>

// ============================================================================
// CONFIGURATION CONSTANTS
// ============================================================================

// -- Pins
#define I2C_SDA_PIN          6
#define I2C_SCL_PIN          7
#define VIBRATION_PIN        4
#define BATTERY_ADC_PIN      2

// -- OLED Display
#define SCREEN_WIDTH         128
#define SCREEN_HEIGHT        64
#define OLED_RESET_PIN       -1
#define OLED_I2C_ADDRESS     0x3C

// -- MAX30102 Sensor
#define PULSE_LED_BRIGHTNESS   50
#define PULSE_SAMPLE_AVERAGING  4
#define PULSE_SAMPLE_RATE      400
#define PULSE_LED_PULSE_WIDTH  411
#define PULSE_ADC_RANGE       4096

// -- Vibration Motor (LEDC / PWM)
#define VIBRATION_LEDC_CHANNEL  0
#define VIBRATION_LEDC_FREQ     5000   // Hz
#define VIBRATION_LEDC_RES      8      // bits (0–255)
#define VIBRATION_INTENSITY     200    // 0–255
#define VIBRATION_DURATION_MS   200

// -- Battery ADC
#define BATTERY_SAMPLES           10
#define BATTERY_VOLTAGE_DIVIDER    2.0f
#define ADC_MAX_VALUE           4095
#define ADC_VOLTAGE_REFERENCE      3.3f
#define BATTERY_MIN_VOLTAGE        3.0f
#define BATTERY_MAX_VOLTAGE        4.2f

// -- WiFi / Firebase
#define WIFI_SSID           "Attenio_MVP"
#define WIFI_PASSWORD       "attenio123"
#define FIREBASE_HOST       "YOUR_PROJECT.firebaseio.com"
#define FIREBASE_SECRET     "YOUR_FIREBASE_DATABASE_SECRET"
#define FIREBASE_PATH       "/attenio"
#define FIREBASE_TIMEOUT    10000

// -- Update intervals (ms)
#define SENSOR_UPDATE_INTERVAL   100
#define DISPLAY_UPDATE_INTERVAL  200
#define BATTERY_CHECK_INTERVAL  5000
#define FIREBASE_UPDATE_INTERVAL 5000
#define WIFI_CHECK_INTERVAL    30000

// -- Heart-rate validity
#define HR_MIN  30
#define HR_MAX 220
#define SPO2_MIN 80
#define SPO2_MAX 100

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================
DFRobot_MAX30102   particleSensor;
MPU6050            mpu(Wire);
Adafruit_SSD1306   display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET_PIN);
WiFiClientSecure   wifiClient;
Preferences        preferences;
WiFiUDP            ntpUDP;
NTPClient          timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

// ============================================================================
// GLOBAL STATE
// ============================================================================
struct SensorData {
    float heartRate     = 0;
    float spO2          = 0;
    float accelX        = 0, accelY = 0, accelZ = 0;
    float gyroX         = 0, gyroY  = 0, gyroZ  = 0;
    float motionMag     = 0;
    float batteryVoltage    = 0;
    int   batteryPercent    = 0;
    float avgHeartRate      = 0;
    int   totalHRSamples    = 0;
} sensor;

struct SystemState {
    bool sensorMAX30102 = false;
    bool sensorMPU6050  = false;
    bool sensorDisplay  = false;
    bool wifiConnected  = false;
    bool firebaseOK     = false;
    bool vibrating      = false;
    unsigned long vibrationEnd = 0;
    unsigned long sessionStart = 0;
    String deviceId;
} sys;

unsigned long lastSensorUpdate   = 0;
unsigned long lastDisplayUpdate  = 0;
unsigned long lastBatteryCheck   = 0;
unsigned long lastFirebaseUpdate = 0;
unsigned long lastWifiCheck      = 0;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
void initDeviceId();
void initI2C();
void initVibration();
void initBatteryMonitor();
bool initMAX30102();
bool initMPU6050();
bool initDisplay();
void initWiFi();

void updateSensors();
void updateMAX30102();
void updateMPU6050();
void updateBattery();
void checkWiFi();

void renderDisplay();
void showWelcomeScreen();
void showMainScreen();
void showErrorScreen(const char* msg);

void startVibration();
void tickVibration();

void pushToFirebase();
bool firebasePut(const String& path, const String& body);
bool firebasePost(const String& path, const String& body);
bool firebaseGet(const String& path, String& out);
String buildJSON();
unsigned long getEpochTime();
void logStatus();

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n==========================================");
    Serial.println(" Attenio MVP – XIAO ESP32-C3");
    Serial.println("==========================================");

    initDeviceId();
    initI2C();
    initVibration();
    initBatteryMonitor();

    sys.sensorMAX30102 = initMAX30102();
    sys.sensorMPU6050  = initMPU6050();
    sys.sensorDisplay  = initDisplay();

    initWiFi();
    updateBattery();

    sys.sessionStart = millis();
    logStatus();

    showWelcomeScreen();
    delay(2000);
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
    unsigned long now = millis();

    if (now - lastSensorUpdate >= SENSOR_UPDATE_INTERVAL) {
        updateSensors();
        lastSensorUpdate = now;
    }

    if (now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
        renderDisplay();
        lastDisplayUpdate = now;
    }

    if (now - lastBatteryCheck >= BATTERY_CHECK_INTERVAL) {
        updateBattery();
        lastBatteryCheck = now;
    }

    if (now - lastFirebaseUpdate >= FIREBASE_UPDATE_INTERVAL) {
        pushToFirebase();
        lastFirebaseUpdate = now;
    }

    if (now - lastWifiCheck >= WIFI_CHECK_INTERVAL) {
        checkWiFi();
        lastWifiCheck = now;
    }

    tickVibration();
    delay(1);
}

// ============================================================================
// INITIALIZATION
// ============================================================================
void initDeviceId() {
    preferences.begin("attenio", false);
    sys.deviceId = preferences.getString("deviceId", "");
    if (sys.deviceId.isEmpty()) {
        sys.deviceId = "ATTENIO_" + String((uint32_t)ESP.getEfuseMac(), HEX);
        preferences.putString("deviceId", sys.deviceId);
    }
    preferences.end();
    Serial.println("Device ID: " + sys.deviceId);
}

void initI2C() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000);
    Serial.println("I2C ready (SDA=GPIO6, SCL=GPIO7)");
}

// ESP32-C3 does not support analogWrite; use LEDC instead.
void initVibration() {
    ledcSetup(VIBRATION_LEDC_CHANNEL, VIBRATION_LEDC_FREQ, VIBRATION_LEDC_RES);
    ledcAttachPin(VIBRATION_PIN, VIBRATION_LEDC_CHANNEL);
    ledcWrite(VIBRATION_LEDC_CHANNEL, 0);
    Serial.println("Vibration motor ready (LEDC, GPIO4)");
}

void initBatteryMonitor() {
    pinMode(BATTERY_ADC_PIN, INPUT);
    Serial.println("Battery monitor ready (GPIO2)");
}

bool initMAX30102() {
    if (!particleSensor.begin()) {
        Serial.println("MAX30102: NOT FOUND – check wiring");
        return false;
    }
    particleSensor.sensorConfiguration(
        PULSE_LED_BRIGHTNESS,
        PULSE_SAMPLE_AVERAGING,
        PULSE_SAMPLE_RATE,
        PULSE_LED_PULSE_WIDTH,
        PULSE_ADC_RANGE
    );
    Serial.println("MAX30102: OK");
    return true;
}

bool initMPU6050() {
    byte status = mpu.begin();
    if (status != 0) {
        Serial.printf("MPU6050: FAILED (status %d)\n", status);
        return false;
    }
    Serial.println("MPU6050: calibrating...");
    mpu.calcOffsets();
    Serial.println("MPU6050: OK");
    return true;
}

bool initDisplay() {
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
        Serial.println("SSD1306: NOT FOUND");
        return false;
    }
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    Serial.println("SSD1306: OK");
    return true;
}

void initWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("WiFi connecting");

    for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) {
        delay(500);
        Serial.print('.');
    }

    if (WiFi.status() == WL_CONNECTED) {
        sys.wifiConnected  = true;
        sys.firebaseOK     = true;
        wifiClient.setInsecure();   // Use a CA cert in production!
        timeClient.begin();
        timeClient.update();
        Serial.printf("\nWiFi OK – IP %s\n", WiFi.localIP().toString().c_str());
    } else {
        sys.wifiConnected = false;
        sys.firebaseOK    = false;
        Serial.println("\nWiFi FAILED – starting AP");
        WiFi.mode(WIFI_AP);
        WiFi.softAP("Attenio_MVP", "attenio123");
        Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    }
}

// ============================================================================
// WiFi RECONNECTION
// ============================================================================
void checkWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi lost – reconnecting...");
        sys.wifiConnected = false;
        sys.firebaseOK    = false;
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
            delay(500);
        }
        if (WiFi.status() == WL_CONNECTED) {
            sys.wifiConnected = true;
            sys.firebaseOK    = true;
            timeClient.update();
            Serial.println("WiFi reconnected");
        }
    } else {
        timeClient.update();
    }
}

// ============================================================================
// SENSOR UPDATES
// ============================================================================
void updateSensors() {
    if (sys.sensorMAX30102) updateMAX30102();
    if (sys.sensorMPU6050)  updateMPU6050();
}

void updateMAX30102() {
    int32_t rawSpO2  = 0; int8_t spO2Valid = 0;
    int32_t rawHR    = 0; int8_t hrValid   = 0;

    particleSensor.heartrateAndOxygenSaturation(
        &rawSpO2, &spO2Valid, &rawHR, &hrValid);

    if (hrValid && rawHR >= HR_MIN && rawHR <= HR_MAX) {
        sensor.heartRate = (float)rawHR;
        // Cumulative moving average
        sensor.totalHRSamples++;
        sensor.avgHeartRate += (sensor.heartRate - sensor.avgHeartRate)
                               / sensor.totalHRSamples;
    } else {
        sensor.heartRate = 0;
    }

    sensor.spO2 = (spO2Valid && rawSpO2 >= SPO2_MIN && rawSpO2 <= SPO2_MAX)
                  ? (float)rawSpO2 : 0;
}

void updateMPU6050() {
    mpu.update();

    sensor.accelX = mpu.getAccX();
    sensor.accelY = mpu.getAccY();
    sensor.accelZ = mpu.getAccZ();
    sensor.gyroX  = mpu.getGyroX();
    sensor.gyroY  = mpu.getGyroY();
    sensor.gyroZ  = mpu.getGyroZ();

    // Net acceleration excluding gravity (~1 g)
    float mag = sqrt(sensor.accelX * sensor.accelX
                   + sensor.accelY * sensor.accelY
                   + sensor.accelZ * sensor.accelZ) - 1.0f;
    sensor.motionMag = (mag > 0) ? mag : 0;
}

void updateBattery() {
    long sum = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
        sum += analogRead(BATTERY_ADC_PIN);
        delayMicroseconds(100);
    }
    float pin = ((float)sum / BATTERY_SAMPLES / ADC_MAX_VALUE) * ADC_VOLTAGE_REFERENCE;
    sensor.batteryVoltage = pin * BATTERY_VOLTAGE_DIVIDER;

    float pct = (sensor.batteryVoltage - BATTERY_MIN_VOLTAGE)
              / (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE) * 100.0f;
    sensor.batteryPercent = (int)constrain(pct, 0, 100);
}

// ============================================================================
// FIREBASE
// ============================================================================
void pushToFirebase() {
    if (!sys.wifiConnected || !sys.firebaseOK) return;

    // Use epoch time as key; fall back to millis() if NTP unavailable
    String key  = String(getEpochTime());
    String path = String(FIREBASE_PATH) + "/" + sys.deviceId + "/readings/" + key;

    if (!firebasePut(path, buildJSON())) {
        Serial.println("Firebase: PUT failed");
        sys.firebaseOK = false;
    } else {
        Serial.println("Firebase: OK");
    }
}

bool firebasePut(const String& path, const String& body) {
    HTTPClient http;
    String url = "https://" + String(FIREBASE_HOST) + path
               + ".json?auth=" + FIREBASE_SECRET;
    http.begin(wifiClient, url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(FIREBASE_TIMEOUT);
    int code = http.PUT(body);
    http.end();
    return (code == 200 || code == 201);
}

bool firebasePost(const String& path, const String& body) {
    HTTPClient http;
    String url = "https://" + String(FIREBASE_HOST) + path
               + ".json?auth=" + FIREBASE_SECRET;
    http.begin(wifiClient, url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(FIREBASE_TIMEOUT);
    int code = http.POST(body);
    http.end();
    return (code == 200 || code == 201);
}

bool firebaseGet(const String& path, String& out) {
    HTTPClient http;
    String url = "https://" + String(FIREBASE_HOST) + path
               + ".json?auth=" + FIREBASE_SECRET;
    http.begin(wifiClient, url);
    http.setTimeout(FIREBASE_TIMEOUT);
    int code = http.GET();
    if (code == 200) {
        out = http.getString();
        http.end();
        return true;
    }
    http.end();
    return false;
}

String buildJSON() {
    // ArduinoJson v7: use JsonDocument (no size template needed)
    JsonDocument doc;

    doc["ts"]      = getEpochTime();
    doc["uptime"]  = millis();
    doc["hr"]      = sensor.heartRate;
    doc["spo2"]    = sensor.spO2;
    doc["avg_hr"]  = sensor.avgHeartRate;
    doc["hr_n"]    = sensor.totalHRSamples;
    doc["motion"]  = sensor.motionMag;
    doc["bat_pct"] = sensor.batteryPercent;
    doc["bat_v"]   = sensor.batteryVoltage;

    JsonObject accel = doc["accel"].to<JsonObject>();
    accel["x"] = sensor.accelX;
    accel["y"] = sensor.accelY;
    accel["z"] = sensor.accelZ;

    JsonObject gyro = doc["gyro"].to<JsonObject>();
    gyro["x"] = sensor.gyroX;
    gyro["y"] = sensor.gyroY;
    gyro["z"] = sensor.gyroZ;

    String out;
    serializeJson(doc, out);
    return out;
}

unsigned long getEpochTime() {
    if (sys.wifiConnected && timeClient.isTimeSet()) {
        return (unsigned long)timeClient.getEpochTime();
    }
    return millis() / 1000UL;   // Rough fallback (seconds since boot)
}

// ============================================================================
// DISPLAY
// ============================================================================
void renderDisplay() {
    if (!sys.sensorDisplay) return;
    display.clearDisplay();
    display.setCursor(0, 0);
    showMainScreen();
    display.display();
}

void showWelcomeScreen() {
    if (!sys.sensorDisplay) return;
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(20, 16);
    display.println("ATTENIO");
    display.setTextSize(1);
    display.setCursor(28, 42);
    display.println("MVP Edition");
    display.display();
}

void showMainScreen() {
    display.setTextSize(1);
    display.println("=== ATTENIO MVP ===");

    display.print("HR:   ");
    if (sensor.heartRate > 0) {
        display.print((int)sensor.heartRate);
        display.println(" bpm");
    } else {
        display.println("-- bpm");
    }

    display.print("SpO2: ");
    if (sensor.spO2 > 0) {
        display.print((int)sensor.spO2);
        display.println(" %");
    } else {
        display.println("-- %");
    }

    display.print("Mot:  ");
    display.print(sensor.motionMag, 2);
    display.println(" g");

    display.print("Bat:  ");
    display.print(sensor.batteryPercent);
    display.println(" %");

    display.print("FB:   ");
    display.println(sys.firebaseOK ? "ON" : "OFF");
}

void showErrorScreen(const char* msg) {
    if (!sys.sensorDisplay) return;
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("ERROR:");
    display.println(msg);
    display.display();
}

// ============================================================================
// VIBRATION (LEDC / PWM – ESP32-C3 compatible)
// ============================================================================
void startVibration() {
    if (sys.vibrating) return;
    sys.vibrating    = true;
    sys.vibrationEnd = millis() + VIBRATION_DURATION_MS;
    ledcWrite(VIBRATION_LEDC_CHANNEL, VIBRATION_INTENSITY);
    Serial.println("Vibration ON");
}

void tickVibration() {
    if (!sys.vibrating) return;
    if (millis() >= sys.vibrationEnd) {
        ledcWrite(VIBRATION_LEDC_CHANNEL, 0);
        sys.vibrating = false;
        Serial.println("Vibration OFF");
    }
}

// ============================================================================
// DIAGNOSTICS
// ============================================================================
void logStatus() {
    Serial.println("\n==========================================");
    Serial.println(" System Status");
    Serial.println("==========================================");
    Serial.printf(" Device ID : %s\n",  sys.deviceId.c_str());
    Serial.printf(" MAX30102  : %s\n",  sys.sensorMAX30102 ? "OK" : "FAIL");
    Serial.printf(" MPU6050   : %s\n",  sys.sensorMPU6050  ? "OK" : "FAIL");
    Serial.printf(" SSD1306   : %s\n",  sys.sensorDisplay  ? "OK" : "FAIL");
    Serial.printf(" WiFi      : %s\n",  sys.wifiConnected  ? "OK" : "FAIL");
    Serial.printf(" Firebase  : %s\n",  sys.firebaseOK     ? "OK" : "FAIL");
    Serial.printf(" Battery   : %d%%  %.2fV\n",
                  sensor.batteryPercent, sensor.batteryVoltage);
    if (sys.wifiConnected) {
        Serial.printf(" IP        : %s\n", WiFi.localIP().toString().c_str());
    }
    Serial.println("==========================================\n");
}