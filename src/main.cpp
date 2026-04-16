#include <Arduino.h>
#include <Wire.h>
#include <DFRobot_MAX30102.h>
#include <MPU6050_light.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
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
#define VIBRATION_LEDC_FREQ     5000
#define VIBRATION_LEDC_RES      8
#define VIBRATION_INTENSITY     200
#define VIBRATION_DURATION_MS   200

// -- Battery ADC
#define BATTERY_SAMPLES           10
#define BATTERY_VOLTAGE_DIVIDER    2.0f
#define ADC_MAX_VALUE           4095
#define ADC_VOLTAGE_REFERENCE      3.3f
#define BATTERY_MIN_VOLTAGE        3.0f
#define BATTERY_MAX_VOLTAGE        4.2f

// -- BLE Configuration
#define BLE_DEVICE_NAME         "HealEdu_MVP"
#define BLE_SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHAR_SENSOR_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHAR_COMMAND_UUID  "beb5483e-36e1-4688-b7f6-ea07361b26a8"
#define BLE_CHAR_CONFIG_UUID   "beb5483e-36e1-4688-b7f7-ea07361b26a8"
#define BLE_UPDATE_INTERVAL    500

// -- WiFi / Firebase
#define WIFI_SSID           "HealEdu_MVP"
#define WIFI_PASSWORD       "healedu123"
#define FIREBASE_HOST       "YOUR_PROJECT.firebaseio.com"
#define FIREBASE_SECRET     "YOUR_FIREBASE_DATABASE_SECRET"
#define FIREBASE_PATH       "/healedu"
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

// -- Edge ML Configuration
#define ATTENTION_WINDOW_SIZE    20
#define ATTENTION_LOW_THRESHOLD  0.4f
#define ATTENTION_HIGH_THRESHOLD 0.7f
#define MOTION_LOW_THRESHOLD     0.1f
#define MOTION_HIGH_THRESHOLD    0.5f
#define HR_REST_MIN              50
#define HR_REST_MAX              100
#define HR_STRESS_THRESHOLD      110

// ============================================================================
// EDGE ML MODEL - ATTENTION DETECTION
// ============================================================================
class AttentionModel {
public:
    float attentionHistory[ATTENTION_WINDOW_SIZE];
    int historyIndex = 0;
    int historyCount = 0;
    float currentAttention = 1.0f;
    float stressLevel = 0.0f;
    bool isCalibrated = false;
    
    struct CalibrationData {
        float baselineHR = 75.0f;
        float baselineMotion = 0.0f;
        float baselineSpO2 = 98.0f;
    } calibration;

    void update(float heartRate, float spO2, float motion) {
        float featureHR = normalizeHeartRate(heartRate);
        float featureMotion = normalizeMotion(motion);
        float featureSpO2 = normalizeSpO2(spO2);
        
        // Simple weighted inference (real model would be a neural network)
        // Weights: HR (40%), Motion (35%), SpO2 (25%)
        float rawAttention = (featureHR * 0.4f) + (featureMotion * 0.35f) + (featureSpO2 * 0.25f);
        
        // Update rolling window
        attentionHistory[historyIndex] = rawAttention;
        historyIndex = (historyIndex + 1) % ATTENTION_WINDOW_SIZE;
        if (historyCount < ATTENTION_WINDOW_SIZE) historyCount++;
        
        // Smoothed attention (moving average)
        float sum = 0;
        for (int i = 0; i < historyCount; i++) sum += attentionHistory[i];
        currentAttention = sum / historyCount;
        
        // Calculate stress level
        if (heartRate > HR_STRESS_THRESHOLD) {
            stressLevel = (stressLevel + 0.1f > 1.0f) ? 1.0f : stressLevel + 0.1f;
        } else if (heartRate < HR_REST_MAX) {
            stressLevel = (stressLevel - 0.05f < 0.0f) ? 0.0f : stressLevel - 0.05f;
        }
    }

    float getAttentionLevel() { return currentAttention; }
    float getStressLevel() { return stressLevel; }
    
    int getAttentionState() {
        if (currentAttention < ATTENTION_LOW_THRESHOLD) return 0; // LOW
        if (currentAttention < ATTENTION_HIGH_THRESHOLD) return 1; // MEDIUM
        return 2; // HIGH
    }
    
    const char* getAttentionLabel() {
        switch (getAttentionState()) {
            case 0: return "LOW";
            case 1: return "MEDIUM";
            default: return "HIGH";
        }
    }
    
    void calibrate(float hr, float motion, float spO2) {
        calibration.baselineHR = (calibration.baselineHR * 0.7f) + (hr * 0.3f);
        calibration.baselineMotion = (calibration.baselineMotion * 0.7f) + (motion * 0.3f);
        calibration.baselineSpO2 = (calibration.baselineSpO2 * 0.7f) + (spO2 * 0.3f);
        isCalibrated = true;
    }

private:
    inline float fabs_diff(float a, float b) { return (a > b) ? (a - b) : (b - a); }
    
    float normalizeHeartRate(float hr) {
        if (hr <= 0 || !isCalibrated) {
            if (hr > 0 && hr < HR_STRESS_THRESHOLD) return 0.8f;
            return 0.5f;
        }
        float diff = fabs_diff(hr, calibration.baselineHR);
        if (diff < 10) return 0.9f;
        if (diff < 20) return 0.7f;
        if (hr > HR_STRESS_THRESHOLD) return 0.3f;
        return 0.5f;
    }
    
    float normalizeMotion(float motion) {
        if (!isCalibrated) {
            if (motion < MOTION_LOW_THRESHOLD) return 0.6f;
            if (motion < MOTION_HIGH_THRESHOLD) return 0.8f;
            return 0.4f;
        }
        float diff = fabs_diff(motion, calibration.baselineMotion);
        if (diff < 0.1f) return 0.9f;
        if (diff < 0.3f) return 0.7f;
        return 0.4f;
    }
    
    float normalizeSpO2(float spO2) {
        if (spO2 <= 0) return 0.5f;
        if (spO2 >= 97) return 1.0f;
        if (spO2 >= 95) return 0.8f;
        return 0.4f;
    }
};

AttentionModel attentionModel;

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
    float attention        = 1.0f;
    float stress            = 0.0f;
} sensor;

struct SystemState {
    bool sensorMAX30102 = false;
    bool sensorMPU6050  = false;
    bool sensorDisplay  = false;
    bool wifiConnected  = false;
    bool firebaseOK     = false;
    bool bleConnected  = false;
    bool bleModeActive = false;
    bool vibrating      = false;
    bool attentionAlertEnabled = true;
    unsigned long vibrationEnd = 0;
    unsigned long sessionStart = 0;
    unsigned long lastVibrationAlert = 0;
    String deviceId;
} sys;

unsigned long lastSensorUpdate   = 0;
unsigned long lastDisplayUpdate  = 0;
unsigned long lastBatteryCheck   = 0;
unsigned long lastFirebaseUpdate = 0;
unsigned long lastWifiCheck      = 0;
unsigned long lastBLEUpdate      = 0;
unsigned long lastAttentionCheck = 0;

BLEServer* bleServer = nullptr;
BLEService* bleService = nullptr;
BLECharacteristic* bleSensorChar = nullptr;
BLECharacteristic* bleCommandChar = nullptr;
BLECharacteristic* bleConfigChar = nullptr;
bool bleDeviceConnected = false;

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
void initBLE();
void handleBLECommand(const uint8_t* data, size_t length);
void sendBLEData();

void updateSensors();
void updateMAX30102();
void updateMPU6050();
void updateBattery();
void updateAttention();
void checkWiFi();

void renderDisplay();
void showWelcomeScreen();
void showMainScreen();
void showErrorScreen(const char* msg);

void startVibration();
void tickVibration();
void attentionVibration();

void pushToFirebase();
bool firebasePut(const String& path, const String& body);
bool firebasePost(const String& path, const String& body);
bool firebaseGet(const String& path, String& out);
String buildJSON();
unsigned long getEpochTime();
void logStatus();

void saveSession();
void loadSession();

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n==========================================");
    Serial.println(" HealEdu MVP – XIAO ESP32-C3");
    Serial.println(" Edge AI Enabled");
    Serial.println("==========================================");

    initDeviceId();
    initI2C();
    initVibration();
    initBatteryMonitor();

    sys.sensorMAX30102 = initMAX30102();
    sys.sensorMPU6050  = initMPU6050();
    sys.sensorDisplay  = initDisplay();

    // Check if BLE mode is set (stored in preferences)
    preferences.begin("healedu", false);
    sys.bleModeActive = preferences.getBool("bleMode", false);
    preferences.end();

    if (sys.bleModeActive) {
        Serial.println("Mode: BLE-Only (WiFi disabled)");
        initBLE();
    } else {
        initWiFi();
        initBLE();
    }
    
    updateBattery();
    loadSession();

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

    if (!sys.bleModeActive && now - lastFirebaseUpdate >= FIREBASE_UPDATE_INTERVAL) {
        pushToFirebase();
        lastFirebaseUpdate = now;
    }

    if (!sys.bleModeActive && now - lastWifiCheck >= WIFI_CHECK_INTERVAL) {
        checkWiFi();
        lastWifiCheck = now;
    }

    if (now - lastBLEUpdate >= BLE_UPDATE_INTERVAL) {
        sendBLEData();
        lastBLEUpdate = now;
    }

    if (now - lastAttentionCheck >= 1000) {
        updateAttention();
        lastAttentionCheck = now;
    }

    tickVibration();
    delay(1);
}

// ============================================================================
// INITIALIZATION
// ============================================================================
void initDeviceId() {
    preferences.begin("healedu", false);
    sys.deviceId = preferences.getString("deviceId", "");
    if (sys.deviceId.isEmpty()) {
        sys.deviceId = "HEALEDU_" + String((uint32_t)ESP.getEfuseMac(), HEX);
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
        Serial.println("MAX30102: NOT FOUND");
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
    WiFi.setTxPower(WIFI_TX_POWER);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("WiFi connecting");

    for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) {
        delay(500);
        Serial.print('.');
    }

    if (WiFi.status() == WL_CONNECTED) {
        sys.wifiConnected  = true;
        sys.firebaseOK     = true;
        wifiClient.setInsecure();
        timeClient.begin();
        timeClient.update();
        Serial.printf("\nWiFi OK – IP %s\n", WiFi.localIP().toString().c_str());
    } else {
        sys.wifiConnected = false;
        sys.firebaseOK    = false;
        Serial.println("\nWiFi FAILED – starting AP");
        WiFi.mode(WIFI_AP);
        WiFi.softAP("HealEdu_MVP", "healedu123");
        Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    }
}

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

void updateAttention() {
    attentionModel.update(sensor.heartRate, sensor.spO2, sensor.motionMag);
    
    if (attentionModel.historyCount > 5 && !attentionModel.isCalibrated) {
        attentionModel.calibrate(sensor.heartRate, sensor.motionMag, sensor.spO2);
    }
    
    sensor.attention = attentionModel.getAttentionLevel();
    sensor.stress = attentionModel.getStressLevel();
    
    if (sys.attentionAlertEnabled && attentionModel.getAttentionState() == 0) {
        attentionVibration();
    }
}

// ============================================================================
// ATTENTION-BASED VIBRATION
// ============================================================================
void attentionVibration() {
    unsigned long now = millis();
    if (now - sys.lastVibrationAlert < 30000) return;
    
    sys.lastVibrationAlert = now;
    Serial.printf("Attention Alert: %s (%.0f%%) - Vibrating\n", 
                  attentionModel.getAttentionLabel(), sensor.attention * 100);
    startVibration();
}

// ============================================================================
// FIREBASE
// ============================================================================
void pushToFirebase() {
    if (!sys.wifiConnected || !sys.firebaseOK) return;

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
    doc["attn"]    = sensor.attention;
    doc["stress"]  = sensor.stress;

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
    return millis() / 1000UL;
}

// ============================================================================
// SESSION STORAGE
// ============================================================================
void saveSession() {
    preferences.begin("healedu", false);
    preferences.putFloat("lastHR", sensor.avgHeartRate);
    preferences.putFloat("lastAttn", sensor.attention);
    preferences.putInt("hrSamples", sensor.totalHRSamples);
    preferences.putULong("sessionStart", sys.sessionStart);
    preferences.end();
    Serial.println("Session saved");
}

void loadSession() {
    preferences.begin("healedu", false);
    float lastHR = preferences.getFloat("lastHR", 0);
    if (lastHR > 0) {
        Serial.printf("Previous session HR: %.1f\n", lastHR);
    }
    preferences.end();
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
    display.println("HEALEDU");
    display.setTextSize(1);
    display.setCursor(28, 42);
    display.println("MVP Edition");
    display.display();
}

void showMainScreen() {
    display.setTextSize(1);
    display.println("=== HEALEDU MVP ===");

    display.print("Attn: ");
    display.print(attentionModel.getAttentionLabel());
    display.print(" ");
    display.print((int)(sensor.attention * 100));
    display.println("%");

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

    display.print("BLE:  ");
    display.println(sys.bleConnected ? "ON" : "OFF");
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
// VIBRATION
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
// BLE SERVER
// ============================================================================
class BLEServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        bleDeviceConnected = true;
        sys.bleConnected = true;
        Serial.println("BLE: Client connected");
    }

    void onDisconnect(BLEServer* pServer) {
        bleDeviceConnected = false;
        sys.bleConnected = false;
        pServer->startAdvertising();
        Serial.println("BLE: Client disconnected");
    }
};

class BLECommandCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string rxValue = pCharacteristic->getValue();
        if (rxValue.length() > 0) {
            Serial.printf("BLE Command received (%d bytes)\n", rxValue.length());
            handleBLECommand((const uint8_t*)rxValue.data(), rxValue.length());
        }
    }
};

class BLEConfigCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string rxValue = pCharacteristic->getValue();
        if (rxValue.length() > 0) {
            handleBLEConfig((const uint8_t*)rxValue.data(), rxValue.length());
        }
    }
};

void initBLE() {
    BLEDevice::init(BLE_DEVICE_NAME);
    
    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new BLEServerCallbacks());
    
    bleService = bleServer->createService(BLE_SERVICE_UUID);
    
    bleSensorChar = bleService->createCharacteristic(
        BLE_CHAR_SENSOR_UUID,
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
    );
    bleSensorChar->addDescriptor(new BLE2902());
    
    bleCommandChar = bleService->createCharacteristic(
        BLE_CHAR_COMMAND_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
    );
    bleCommandChar->setCallbacks(new BLECommandCallbacks());
    bleCommandChar->addDescriptor(new BLE2902());
    
    bleConfigChar = bleService->createCharacteristic(
        BLE_CHAR_CONFIG_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ
    );
    bleConfigChar->setCallbacks(new BLEConfigCallbacks());
    bleConfigChar->addDescriptor(new BLE2902());
    
    bleService->start();
    
    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(BLE_SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);
    advertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
    
    Serial.println("BLE: Server started, advertising...");
}

void handleBLEConfig(const uint8_t* data, size_t length) {
    if (length < 1) return;
    
    uint8_t cmd = data[0];
    
    switch (cmd) {
        case 0x10: // Enable/disable attention alerts
            if (length >= 2) {
                sys.attentionAlertEnabled = data[1] > 0;
                Serial.printf("BLE Config: Attention alerts %s\n", 
                              sys.attentionAlertEnabled ? "enabled" : "disabled");
            }
            break;
        case 0x11: // Calibrate attention model
            attentionModel.calibrate(sensor.heartRate, sensor.motionMag, sensor.spO2);
            Serial.println("BLE Config: Model calibrated");
            break;
        case 0x12: // Set mode (BLE-only / WiFi+BLE)
            if (length >= 2) {
                sys.bleModeActive = data[1] > 0;
                preferences.begin("healedu", false);
                preferences.putBool("bleMode", sys.bleModeActive);
                preferences.end();
                Serial.printf("BLE Config: Mode set to %s\n", 
                              sys.bleModeActive ? "BLE-only" : "WiFi+BLE");
            }
            break;
        case 0x13: // Get device status
            {
                uint8_t status[16];
                status[0] = 0x13;
                status[1] = sys.sensorMAX30102 ? 1 : 0;
                status[2] = sys.sensorMPU6050 ? 1 : 0;
                status[3] = sys.wifiConnected ? 1 : 0;
                status[4] = sensor.batteryPercent;
                status[5] = (uint8_t)(sensor.attention * 100);
                bleConfigChar->setValue(status, 6);
                bleConfigChar->notify();
            }
            break;
        default:
            Serial.printf("BLE Config: Unknown 0x%02X\n", cmd);
    }
    
    if (bleConfigChar) {
        uint8_t ack[2] = {cmd, 0x01};
        bleConfigChar->setValue(ack, 2);
        bleConfigChar->notify();
    }
}

void handleBLECommand(const uint8_t* data, size_t length) {
    if (length < 1) return;
    
    uint8_t cmd = data[0];
    
    switch (cmd) {
        case 0x01: // Trigger vibration
            Serial.println("BLE: Command - Vibrate");
            startVibration();
            break;
        case 0x02: // Stop vibration
            Serial.println("BLE: Command - Stop vibration");
            ledcWrite(VIBRATION_LEDC_CHANNEL, 0);
            sys.vibrating = false;
            break;
        case 0x03: // Request sensor data
            Serial.println("BLE: Command - Request data");
            sendBLEData();
            break;
        case 0x04: // Change intensity
            if (length >= 2) {
                ledcWrite(VIBRATION_LEDC_CHANNEL, data[1]);
                Serial.printf("BLE: Command - Set intensity %d\n", data[1]);
            }
            break;
        case 0x05: // Save session
            saveSession();
            break;
        case 0x06: // Reset attention model
            attentionModel.historyCount = 0;
            attentionModel.historyIndex = 0;
            attentionModel.isCalibrated = false;
            Serial.println("BLE: Command - Reset attention model");
            break;
        default:
            Serial.printf("BLE: Unknown command 0x%02X\n", cmd);
    }
    
    if (bleCommandChar) {
        uint8_t ack[2] = {cmd, 0x01};
        bleCommandChar->setValue(ack, 2);
        bleCommandChar->notify();
    }
}

void sendBLEData() {
    if (!bleSensorChar) return;
    
    JsonDocument doc;
    doc["hr"]      = sensor.heartRate;
    doc["spo2"]    = sensor.spO2;
    doc["motion"]  = sensor.motionMag;
    doc["avg_hr"]  = sensor.avgHeartRate;
    doc["hr_n"]    = sensor.totalHRSamples;
    doc["bat_pct"] = sensor.batteryPercent;
    doc["bat_v"]   = sensor.batteryVoltage;
    doc["attn"]    = sensor.attention;
    doc["attn_state"] = attentionModel.getAttentionState();
    doc["stress"]  = sensor.stress;
    doc["ax"]      = sensor.accelX;
    doc["ay"]      = sensor.accelY;
    doc["az"]      = sensor.accelZ;
    doc["gx"]      = sensor.gyroX;
    doc["gy"]      = sensor.gyroY;
    doc["gz"]      = sensor.gyroZ;
    doc["ts"]      = getEpochTime();
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    
    std::string payload = jsonStr.c_str();
    bleSensorChar->setValue(payload);
    if (bleDeviceConnected) {
        bleSensorChar->notify();
    }
}

// ============================================================================
// DIAGNOSTICS
// ============================================================================
void logStatus() {
    Serial.println("\n==========================================");
    Serial.println(" System Status");
    Serial.println("==========================================");
    Serial.printf(" Device ID    : %s\n",  sys.deviceId.c_str());
    Serial.printf(" MAX30102     : %s\n",  sys.sensorMAX30102 ? "OK" : "FAIL");
    Serial.printf(" MPU6050      : %s\n",  sys.sensorMPU6050  ? "OK" : "FAIL");
    Serial.printf(" SSD1306      : %s\n",  sys.sensorDisplay  ? "OK" : "FAIL");
    Serial.printf(" Mode         : %s\n",  sys.bleModeActive ? "BLE-only" : "WiFi+BLE");
    Serial.printf(" WiFi         : %s\n",  sys.wifiConnected  ? "OK" : "FAIL");
    Serial.printf(" BLE          : %s\n",  sys.bleConnected   ? "OK" : "ADVERTISING");
    Serial.printf(" Firebase     : %s\n",  sys.firebaseOK     ? "OK" : "FAIL");
    Serial.printf(" Battery      : %d%%  %.2fV\n",
                  sensor.batteryPercent, sensor.batteryVoltage);
    Serial.printf(" Attention ML : %s\n",  attentionModel.isCalibrated ? "CALIBRATED" : "CALIBRATING");
    if (sys.wifiConnected) {
        Serial.printf(" IP           : %s\n", WiFi.localIP().toString().c_str());
    }
    Serial.println("==========================================\n");
}
