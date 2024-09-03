#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPClient.h>

// הגדרות MUX
#define pinMuxA D5     // פין לשליטה בקו A של המולטיפלקסר
#define pinMuxB D6     // פין לשליטה בקו B של המולטיפלקסר
#define pinMuxC D7     // פין לשליטה בקו C של המולטיפלקסר
#define pinMuxInOut A0 // פין לקריאה מהחיישנים המחוברים למולטיפלקסר

const int SENSOR_PIN = D3; // מחבר החיישן (לא בשימוש בקוד הזה)

// קבועי הכיול שנמצאו אחרי הכיול (להתאים לפי הכיול האמיתי)
const float a = 0.00002; // קבוע לפולינום מדרגה שנייה (משתמשים בו בפונקציה ConvertToWeight)
const float b = 0.003;   // קבוע לפולינום מדרגה ראשונה (משתמשים בו בפונקציה ConvertToWeight)
const float c = -0.5;    // קבוע חותך ציר ה-y (משתמשים בו בפונקציה ConvertToWeight)

// משתנה לניהול זמן אחרון בו זוהתה תנועה בדלת
unsigned long lastTriggerTime = 0;

// משתנה למספר האנשים בחדר (מוגדר על 1 בתחילה)
int pplInRoom = 1;

// זמן נדרש שבו המשקל חייב להיות 0 על מנת להיחשב כאילו אין משקל (15 שניות)
const unsigned long ZERO_WEIGHT_DURATION = 15000;

// משתנים למעקב אם המשקל היה אי פעם מעל 0
bool hasWeight1BeenAboveZero = false; // חיישן 1
bool hasWeight2BeenAboveZero = false; // חיישן 2

// משתנים גלובליים למעקב אחר הזמן שבו המשקל של כל חיישן הוא 0 או פחות
long zeroWeightStartTime1 = -1; // זמן התחלה למשקל 0 בחיישן 1
long zeroWeightStartTime2 = -1; // זמן התחלה למשקל 0 בחיישן 2

// פין לחצן שמשמש להגדרת מצב חירום
const int buttonPin = D5;

// משתנה למעקב אחרי מצב הלחצן (נלחץ או לא)
bool buttonPressed = false;

// מחרוזות לנתונים שנשלחים לשרת
String btnPressed = "test";    // מצב ברירת מחדל לחצן חירום
String EmptyBed = "testtt";    // מצב ברירת מחדל למיטה ריקה

// קבועים להגדרת הפינים עבור חיישני הדלת
const int trigPinIn = D2;      // פין טריגר לחיישן כניסה
const int echoPinIn = D8;      // פין אקו לחיישן כניסה
const int trigPinOut = D7;     // פין טריגר לחיישן יציאה
const int echoPinOut = D6;     // פין אקו לחיישן יציאה

// משתנה לניהול מצב החיישנים
enum SensorState { NONE, OUT_DETECTED, IN_DETECTED }; // מצב חיישן: לא זוהה, יציאה זוהתה, כניסה זוהתה
SensorState lastSensorTriggered = NONE; // מצב אחרון שבו חיישן הופעל

void setup() {
    Serial.begin(9600); 
    wifi_Setup(); // קריאה לפונקציה להגדרת התקשורת האלחוטית

    pinMode(buttonPin, INPUT_PULLUP);  // הגדרת הפין של הלחצן עם משיכה מעלה
    pinMode(pinMuxA, OUTPUT);          // הגדרת הפין לקו A של המולטיפלקסר כיציאה
    pinMode(pinMuxB, OUTPUT);          // הגדרת הפין לקו B של המולטיפלקסר כיציאה
    pinMode(pinMuxC, OUTPUT);          // הגדרת הפין לקו C של המולטיפלקסר כיציאה
    pinMode(pinMuxInOut, INPUT);       // הגדרת הפין של הכניסה מהחיישנים ככניסה
    pinMode(SENSOR_PIN, INPUT_PULLUP); // הגדרת פין החיישן (לא בשימוש בקוד הזה)

    pinMode(trigPinIn, OUTPUT);        // הגדרת פין טריגר של חיישן כניסה כיציאה
    pinMode(echoPinIn, INPUT);         // הגדרת פין אקו של חיישן כניסה ככניסה
    pinMode(trigPinOut, OUTPUT);       // הגדרת פין טריגר של חיישן יציאה כיציאה
    pinMode(echoPinOut, INPUT);        // הגדרת פין אקו של חיישן יציאה ככניסה
}

void loop() {
    // קריאה מהחיישנים והמרה למשקל
    float weight1 = ConvertToWeight(ReadMuxChannel(0)); // קריאה והמרת משקל מחיישן 1
    float weight2 = ConvertToWeight(ReadMuxChannel(1)); // קריאה והמרת משקל מחיישן 2
    
    // בדיקת מצב משקל 0 בשני החיישנים
    bool zeroWeightDetected = CheckZeroWeight(weight1, weight2);
    
    // בדיקת חיישני הדלת
    bool isCutOutDetectedOut = ChecDoorSensorOut(); // בדיקת חיישן יציאה
    bool isCutOutDetectedIn = ChecDoorSensorIn();   // בדיקת חיישן כניסה
    
    // בדיקת לחיצה על הלחצן
    bool isButtonPressed = checkPress();

    unsigned long currentMillis = millis(); // זמן נוכחי

    // ניהול חיישן יציאה
    if (isCutOutDetectedOut) {
        if (lastSensorTriggered == IN_DETECTED && (currentMillis - lastTriggerTime <= 2000)) {
            if (pplInRoom > 1) { // בדוק אם יש יותר מ-1 אנשים בחדר לפני ההפחתה
                pplInRoom--;
                Serial.println("Person left the room. pplInRoom: " + String(pplInRoom));
            } else {
                Serial.println("Cannot decrement: pplInRoom is already at minimum value of 1.");
            }
            lastSensorTriggered = NONE; // איפוס המצב אחרי פעולה מוצלחת
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
            lastSensorTriggered = NONE; // איפוס המצב אחרי פעולה מוצלחת
        } else {
            lastSensorTriggered = IN_DETECTED;
            lastTriggerTime = currentMillis;
        }
    }

    // ודא שמספר האנשים בחדר לא יורד מתחת ל-1
    if (pplInRoom < 1) {
        pplInRoom = 1;
        Serial.println("Corrected pplInRoom to minimum value of 1.");
    }

    // איפוס מצב החיישן אם החיישן השני לא הופעל בתוך 5 שניות
    if (lastSensorTriggered != NONE && (currentMillis - lastTriggerTime >= 5000)) {
        lastSensorTriggered = NONE; // איפוס המצב אם החיישן השני לא הופעל בזמן
        Serial.println("Resetting sensor state after timeout.");
    }

    // אם זוהה משקל 0 ואין אנשים בחדר, דווח על מיטה ריקה
    if (zeroWeightDetected && pplInRoom == 1) {
        EmptyBed = "danger";    
        String result = GetData();
        Serial.print("Server response: ");
        Serial.println(result);
    }

    // אם נלחץ הלחצן, שלח קריאת חירום לשרת
    if (isButtonPressed) {
        btnPressed = "emergency!";
        String result = GetData();
        Serial.print("Server response: ");
        Serial.println(result);
    }
}

// פונקציה לבדיקת לחיצה על הלחצן
bool checkPress() {
    int buttonState = digitalRead(buttonPin); // קריאת מצב הלחצן

    if (buttonState == LOW && !buttonPressed) { // לחצן נלחץ
        buttonPressed = true;
        Serial.println("Button pressed.");
        return true; // החזרת true אם הלחצן נלחץ
    }

    if (buttonState == HIGH) { // לחצן לא נלחץ
        buttonPressed = false;
    }

    return false; // החזרת false אם הלחצן לא נלחץ
}

// פונקציה לקריאת ערוץ מהמולטיפלקסר
int ReadMuxChannel(byte chnl) {
    // קביעת הפינים של המולטיפלקסר בהתאם לערוץ הנבחר
    int a = (bitRead(chnl, 0) > 0) ? HIGH : LOW;
    int b = (bitRead(chnl, 1) > 0) ? HIGH : LOW;
    int c = (bitRead(chnl, 2) > 0) ? HIGH : LOW;
    digitalWrite(pinMuxA, a);
    digitalWrite(pinMuxB, b);
    digitalWrite(pinMuxC, c);
    delay(10);  // זמן התייצבות למתמר
    return analogRead(pinMuxInOut); // קריאת ערך אנלוגי מהערוץ הנבחר
}

// פונקציה להמרת ערך אנלוגי למשקל
float ConvertToWeight(int analogValue) {
    // השתמש בקבועי הכיול להמיר את הקריאה האנלוגית למשקל בקילוגרם
    return a * analogValue * analogValue + b * analogValue + c;
}

// פונקציה לבדוק אם המשקל של שני החיישנים הוא 0 או פחות במשך זמן מוגדר
bool CheckZeroWeight(float weight1, float weight2) {
    bool result = false;

    // בדוק אם המשקל עבר את ה-0 אי פעם
    if (weight1 > 0) {
        hasWeight1BeenAboveZero = true;
    }
    if (weight2 > 0) {
        hasWeight2BeenAboveZero = true;
    }

    // אם אחד החיישנים היה אי פעם מעל 0
    if (hasWeight1BeenAboveZero || hasWeight2BeenAboveZero) {
        bool isWeight1Zero = (weight1 <= 0);
        bool isWeight2Zero = (weight2 <= 0);

        if (!isWeight1Zero || !isWeight2Zero) {
            zeroWeightStartTime1 = -1;
            zeroWeightStartTime2 = -1;
            return false; 
        }

        if (isWeight1Zero && zeroWeightStartTime1 < 0) {
            zeroWeightStartTime1 = millis();
        }

        if (isWeight2Zero && zeroWeightStartTime2 < 0) {
            zeroWeightStartTime2 = millis();
        }

        if (zeroWeightStartTime1 > 0 && zeroWeightStartTime2 > 0) {
            unsigned long elapsedTime1 = millis() - zeroWeightStartTime1;
            unsigned long elapsedTime2 = millis() - zeroWeightStartTime2;
            unsigned long elapsedTime = min(elapsedTime1, elapsedTime2);
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

// פונקציה לבדוק חיישן כניסה
bool ChecDoorSensorIn() {
    digitalWrite(trigPinIn, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPinIn, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPinIn, LOW);
    
    long duration = pulseIn(echoPinIn, HIGH, 30000); // זמן הדהוד

    if (duration == 0) {
        return false;
    } else {
        long distance = duration * 0.034 / 2;
        Serial.print("The distance is in : ");
        Serial.println(distance);

        if (distance < 6) { // מרחק פחות מ-6 ס"מ נחשב כזיהוי כניסה
            return true;
        } 
    }

    delay(1500);
    return false;
}

// פונקציה לבדוק חיישן יציאה
bool ChecDoorSensorOut() {
    digitalWrite(trigPinOut, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPinOut, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPinOut, LOW);
    
    long duration = pulseIn(echoPinOut, HIGH, 30000); // זמן הדהוד

    if (duration == 0) {
        return false;
    } else {
        long distance = duration * 0.034 / 2;
        Serial.print("The distance out is: ");
        Serial.println(distance);

        if (distance < 6) { // מרחק פחות מ-6 ס"מ נחשב כזיהוי יציאה
            return true;
        } 
    }

    delay(1500);
    return false;
}