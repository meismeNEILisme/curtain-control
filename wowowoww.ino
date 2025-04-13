#include <WiFi.h>
#include <FirebaseESP32.h>
#include <DHT.h>
#include <RTClib.h>
#include <Servo.h>

// WiFi Credentials
#define WIFI_SSID "Maoniwifini_2.4G"
#define WIFI_PASS "ragas2018"

// Firebase Configuration
#define FIREBASE_HOST "curtain-control-system-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "V2CZDAtQLAWQEnqd9P8k7Ydqj6POCPalnZvoiwuB"

// Pin Definitions
#define SERVO_PIN 13
#define DHT_PIN 4
#define LIMIT_SWITCH 5
#define RELAY_PIN 12

// Firebase Paths
#define STATUS_PATH "/curtainControl/status"
#define MODE_PATH "/curtainControl/mode"
#define COMMAND_PATH "/curtainControl/command"
#define TEMP_SETTINGS_PATH "/curtainControl/temperatureSettings"
#define SCHEDULE_PATH "/curtainControl/schedule"
#define ENV_PATH "/environment"

// Objects
FirebaseData fbdo;
DHT dht(DHT_PIN, DHT22);
RTC_DS3231 rtc;
Servo servo;

// Variables
int currentPos = 0; // 0=closed, 180=open
bool isAutoMode = true;
unsigned long lastUpdate = 0;

void setup() {
  Serial.begin(115200);
  
  // Initialize Hardware
  pinMode(LIMIT_SWITCH, INPUT_PULLUP);
  servo.attach(SERVO_PIN);
  dht.begin();
  
  // Connect WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi Connected!");
  
  // Initialize Firebase
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  
  // Initialize RTC
  if (!rtc.begin()) Serial.println("RTC Not Found!");
  if (rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  // Set Initial Values
  Firebase.setString(fbdo, STATUS_PATH, "close");
  Firebase.setString(fbdo, MODE_PATH, "auto");
}

void loop() {
  // Update sensors every 2 seconds
  if (millis() - lastUpdate > 2000) {
    updateSensors();
    checkAutoMode();
    lastUpdate = millis();
  }
  
  checkManualCommands();
  checkLimitSwitch();
}

void updateSensors() {
  // Read and send temperature/humidity
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  
  if (!isnan(temp)) Firebase.setFloat(fbdo, ENV_PATH "/temperature", temp);
  if (!isnan(hum)) Firebase.setFloat(fbdo, ENV_PATH "/humidity", hum);
}

void checkAutoMode() {
  if (!isAutoMode) return;

  DateTime now = rtc.now();
  String currentTime = String(now.hour()) + ":" + (now.minute() < 10 ? "0" : "") + String(now.minute());
  
  // Get settings from Firebase
  int openAbove = 25, closeBelow = 20;
  String openTime = "07:00", closeTime = "19:00";
  
  if (Firebase.getInt(fbdo, TEMP_SETTINGS_PATH "/openAbove")) openAbove = fbdo.intData();
  if (Firebase.getInt(fbdo, TEMP_SETTINGS_PATH "/closeBelow")) closeBelow = fbdo.intData();
  if (Firebase.getString(fbdo, SCHEDULE_PATH "/openTime")) openTime = fbdo.stringData();
  if (Firebase.getString(fbdo, SCHEDULE_PATH "/closeTime")) closeTime = fbdo.stringData();

  // Temperature-based control
  float currentTemp = dht.readTemperature();
  if (currentTemp >= openAbove && currentPos < 180) {
    setCurtainPosition(180); // Open fully
  } else if (currentTemp < closeBelow && currentPos > 0) {
    setCurtainPosition(0); // Close fully
  }

  // Time-based control
  if (currentTime == openTime && currentPos < 180) {
    setCurtainPosition(180);
  } else if (currentTime == closeTime && currentPos > 0) {
    setCurtainPosition(0);
  }
}

void checkManualCommands() {
  if (Firebase.getString(fbdo, COMMAND_PATH)) {
    String cmd = fbdo.stringData();
    
    if (cmd == "open") setCurtainPosition(180);
    else if (cmd == "close") setCurtainPosition(0);
    else if (cmd == "stop") servo.write(90); // Stop
    
    Firebase.setString(fbdo, COMMAND_PATH, ""); // Clear command
  }
}

void checkLimitSwitch() {
  if (digitalRead(LIMIT_SWITCH) == LOW && currentPos != 0) {
    currentPos = 0;
    Firebase.setString(fbdo, STATUS_PATH, "close");
  }
}

void setCurtainPosition(int pos) {
  currentPos = pos;
  servo.write(pos);
  
  if (pos == 180) {
    Firebase.setString(fbdo, STATUS_PATH, "open");
  } else if (pos == 0) {
    Firebase.setString(fbdo, STATUS_PATH, "close");
  } else {
    Firebase.setString(fbdo, STATUS_PATH, pos > currentPos ? "opening" : "closing");
  }
  
  delay(15); // Servo movement delay
}