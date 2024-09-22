#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>

// הגדרות MUX
#define pinMuxA D5
#define pinMuxB D6
#define pinMuxC D7
#define pinMuxInOut A0

const int SENSOR_PIN = D3;

// קבועי הכיול
const float a = 0.00002;
const float b = 0.003;
const float c = -0.5;

unsigned long lastTriggerTime = 0;
int pplInRoom = 1;
const unsigned long ZERO_WEIGHT_DURATION = 15000;
bool hasWeight1BeenAboveZero = false;
bool hasWeight2BeenAboveZero = false;
long zeroWeightStartTime1 = -1;
long zeroWeightStartTime2 = -1;

// פין לחצן חירום
const int buttonPin = D1;

// משתנים לטיפול בלחיצת כפתור
volatile bool buttonPressed = false;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

String btnPressed = "normal";
String EmptyBed = "normal";

// קבועים להגדרת הפינים עבור חיישני הדלת
const int trigPinIn = D2;
const int echoPinIn = D8;
const int trigPinOut = D7;
const int echoPinOut = D6;

enum SensorState { NONE, OUT_DETECTED, IN_DETECTED };
SensorState lastSensorTriggered = NONE;

const char* ssid = "";
const char* password = "";
bool dataSent = false;
ESP8266WebServer server(7155);

void ICACHE_RAM_ATTR handleInterrupt() {
  if ((millis() - lastDebounceTime) > debounceDelay) {
    buttonPressed = true;
    lastDebounceTime = millis();
  }
}

void wifi_Setup() {
  Serial.begin(9600);
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Attempting to connect to network...");
  }

  Serial.println("Connected to network");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  server.on("/index", GetData);
  server.begin();
  Serial.println("HTTP server started");
}

String GetData() {
  String deviceNumber = "1";
  String message = "{\"btnPressed\": \"" + btnPressed + "\", \"emptyBed\": \"" + EmptyBed + "\", \"deviceNumber\": " + deviceNumber + "}";

  if (btnPressed != "normal" || EmptyBed != "normal") {
    server.send(200, "application/json", message);
    dataSent = true;
  }

  return message;
}

void CheckServerAck() {
  if (dataSent) {
    btnPressed = "normal";
    EmptyBed = "normal";
    dataSent = false;
  }
}

void setup() {
  Serial.begin(9600); 
  wifi_Setup();

  
  pinMode(buttonPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(buttonPin), handleInterrupt, FALLING);//הפונקציה attachInterrupt מקשרת בין הפין של הכפתור לפונקציית הטיפול בפסיקה (handleInterrupt). הפרמטר FALLING אומר שהפסיקה תתרחש כאשר המתח על הפין יורד (כלומר, כאשר הכפתור נלחץ).
  
  pinMode(pinMuxA, OUTPUT);
  pinMode(pinMuxB, OUTPUT);
  pinMode(pinMuxC, OUTPUT);
  pinMode(pinMuxInOut, INPUT);
  pinMode(SENSOR_PIN, INPUT_PULLUP);

  pinMode(trigPinIn, OUTPUT);
  pinMode(echoPinIn, INPUT);
  pinMode(trigPinOut, OUTPUT);
  pinMode(echoPinOut, INPUT);
}

void loop() {
  server.handleClient();
  CheckServerAck();

  if (buttonPressed) {
    Serial.println("Button pressed!");
    btnPressed = "A distress call was detected";
    String result = GetData();
    Serial.print("Server response: ");
    Serial.println(result);
    buttonPressed = false;
  }

  float weight1 = ConvertToWeight(ReadMuxChannel(0));
  float weight2 = ConvertToWeight(ReadMuxChannel(1));
  
  bool zeroWeightDetected = CheckZeroWeight(weight1, weight2);
  
  bool isCutOutDetectedOut = ChecDoorSensorOut();
  bool isCutOutDetectedIn = ChecDoorSensorIn();
  
  unsigned long currentMillis = millis();

  // ניהול חיישן יציאה
  if (isCutOutDetectedOut) {
    if (lastSensorTriggered == IN_DETECTED && (currentMillis - lastTriggerTime <= 2000)) {
      if (pplInRoom > 1) {
        pplInRoom--;
        Serial.println("Person left the room. pplInRoom: " + String(pplInRoom));
      } else {
        Serial.println("Cannot decrement: pplInRoom is already at minimum value of 1.");
      }
      lastSensorTriggered = NONE;
    } else {
      lastSensorTriggered = OUT_DETECTED;
      lastTriggerTime = currentMillis;
    }
  }

  // ניהול חיישן כניסה
  if (isCutOutDetectedIn) {
    if (lastSensorTriggered == OUT_DETECTED && (currentMillis - lastTriggerTime <= 2000)) {
      pplInRoom++;
      Serial.println("Person entered the room. pplInRoom: " + String(pplInRoom));
      lastSensorTriggered = NONE;
    } else {
      lastSensorTriggered = IN_DETECTED;
      lastTriggerTime = currentMillis;
    }
  }

  if (pplInRoom < 1) {
    pplInRoom = 1;
    Serial.println("Corrected pplInRoom to minimum value of 1.");
  }

  if (lastSensorTriggered != NONE && (currentMillis - lastTriggerTime >= 5000)) {
    lastSensorTriggered = NONE;
    Serial.println("Resetting sensor state after timeout.");
  }

  if (zeroWeightDetected && pplInRoom == 1) {
    EmptyBed = "A fall from the bed was detected";    
    String result = GetData();
    Serial.print("Server response: ");
    Serial.println(result);
  }
}

int ReadMuxChannel(byte chnl) {
  int a = (bitRead(chnl, 0) > 0) ? HIGH : LOW;
  int b = (bitRead(chnl, 1) > 0) ? HIGH : LOW;
  int c = (bitRead(chnl, 2) > 0) ? HIGH : LOW;
  digitalWrite(pinMuxA, a);
  digitalWrite(pinMuxB, b);
  digitalWrite(pinMuxC, c);
  delay(10);
  return analogRead(pinMuxInOut);
}

float ConvertToWeight(int analogValue) {
  return a * analogValue * analogValue + b * analogValue + c;
}

bool CheckZeroWeight(float weight1, float weight2) {
  bool result = false;

  if (weight1 > 0) hasWeight1BeenAboveZero = true;
  if (weight2 > 0) hasWeight2BeenAboveZero = true;

  if (hasWeight1BeenAboveZero || hasWeight2BeenAboveZero) {
    bool isWeight1Zero = (weight1 <= 0);
    bool isWeight2Zero = (weight2 <= 0);

    if (!isWeight1Zero || !isWeight2Zero) {
      zeroWeightStartTime1 = -1;
      zeroWeightStartTime2 = -1;
      return false; 
    }

    if (isWeight1Zero && zeroWeightStartTime1 < 0) zeroWeightStartTime1 = millis();
    if (isWeight2Zero && zeroWeightStartTime2 < 0) zeroWeightStartTime2 = millis();

    if (zeroWeightStartTime1 > 0 && zeroWeightStartTime2 > 0) {
      unsigned long elapsedTime = min(millis() - zeroWeightStartTime1, millis() - zeroWeightStartTime2);
      float elapsedSeconds = elapsedTime / 1000.0;

      Serial.print("Time with zero or less weight for both sensors: ");
      Serial.print(elapsedSeconds);
      Serial.println(" seconds");

      if (elapsedTime >= ZERO_WEIGHT_DURATION) {
        Serial.println("Both sensors have detected zero or less weight for the required duration.");
        result = true;
        zeroWeightStartTime1 = -1;
        zeroWeightStartTime2 = -1;
      }
    }
  } else {
    Serial.println("No weight has been detected above zero on either sensor.");
  }

  return result;
}

bool ChecDoorSensorIn() {
  digitalWrite(trigPinIn, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPinIn, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPinIn, LOW);
  
  long duration = pulseIn(echoPinIn, HIGH, 30000);

  if (duration == 0) return false;
  
  long distance = duration * 0.034 / 2;
  Serial.print("The distance is in : ");
  Serial.println(distance);

  if (distance < 6) return true;

  delay(1500);
  return false;
}

bool ChecDoorSensorOut() {
  digitalWrite(trigPinOut, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPinOut, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPinOut, LOW);
  
  long duration = pulseIn(echoPinOut, HIGH, 30000);

  if (duration == 0) return false;
  
  long distance = duration * 0.034 / 2;
  Serial.print("The distance out is: ");
  Serial.println(distance);

  if (distance < 6) return true;

  delay(1500);
  return false;
}
