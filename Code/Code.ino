#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>

#define WIFI_SSID ""
#define WIFI_PASSWORD ""

#define API_KEY ""
#define FIREBASE_PROJECT_ID ""
#define USER_EMAIL ""
#define USER_PASSWORD ""

#define RELAY_PIN 4
#define WATER_LEVEL_SENSOR_PIN 32
#define TDS_SENSOR_PIN 33
#define PH_SENSOR_PIN 35

#define TDS_VOLTAGE_REFERENCE 3.3
#define TDS_SAMPLE_COUNT 30

LiquidCrystal_I2C lcd(0x27, 16, 2);

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

int analogBuffer[TDS_SAMPLE_COUNT];
int analogBufferTemp[TDS_SAMPLE_COUNT];
int analogBufferIndex = 0;
float averageVoltage = 0;
float tdsOffset = 0; 
float calibrationFactor = 2.8; 
float tdsValue = 0;
float tdsValueCalibrated = 0;
float temperature = 29.0;       

int analog_PH_Value;
float PH_Voltage, PH_Step, PH_Value;
float PH7 = 2.5;
float PH9 = 2.18;

String pumpStatusText = " "; 

unsigned long dataMillis = 0;
unsigned long tdsSampleMillis = 0;

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  lcd.init();
  lcd.backlight();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    lcd.setCursor(0, 0);
    lcd.print(".");
    Serial.print(".");
  }

  lcd.clear();
  Serial.println();
  lcd.setCursor(0, 0); lcd.print("WiFi Connected!");
  lcd.setCursor(0, 1); lcd.print(WiFi.localIP());

  Serial.print("Setting up time");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  time_t now = time(nullptr);
  while (now < 1700000000) { 
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\nTime synchronized!");

  setenv("TZ", "GMT-8", 1); 
  tzset();
  Serial.println("Timezone set to WITA (UTC+8)");

  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback;

  Firebase.reconnectNetwork(true);
  fbdo.setBSSLBufferSize(4096, 1024);
  fbdo.setResponseSize(2048);
  Firebase.begin(&config, &auth);
  lcd.clear();

  lcd.setCursor(0, 0); lcd.print("Air:");
  lcd.setCursor(0, 1); lcd.print("pH :");
  lcd.setCursor(8, 0); lcd.print("TDS:");
  lcd.setCursor(8, 1); lcd.print("Pump:");
}

int readWaterLevel() {
  int sensorWaterADC = analogRead(WATER_LEVEL_SENSOR_PIN);
  int waterLevel = map(sensorWaterADC, 0, 2500, 0, 5);
  if (waterLevel < 0) waterLevel = 0;
  if (waterLevel > 5) waterLevel = 5;
  return waterLevel;
}

float readPH(){
  analog_PH_Value = analogRead(PH_SENSOR_PIN);
  PH_Voltage = 3.3 / 4096.0 * analog_PH_Value;
  PH_Step = (PH7 - PH9) / 2;
  PH_Value = (7.0 + ((PH7 - PH_Voltage) / PH_Step)) - 0.7;
  if (PH_Value > 14.0) PH_Value = 14;
  if (PH_Value < 1.0) PH_Value = 1;
  return PH_Value;
}

int getMedianNum(int bArray[], int iFilterLen) {
  int bTab[iFilterLen];
  for (byte i = 0; i < iFilterLen; i++) {
    bTab[i] = bArray[i];
  }

  for (int j = 0; j < iFilterLen - 1; j++) {
    for (int i = 0; i < iFilterLen - j - 1; i++) {
      if (bTab[i] > bTab[i + 1]) {
        int temp = bTab[i];
        bTab[i] = bTab[i + 1];
        bTab[i + 1] = temp;
      }
    }
  }

  if ((iFilterLen & 1) > 0)
    return bTab[(iFilterLen - 1) / 2];
  else
    return (bTab[iFilterLen / 2] + bTab[iFilterLen / 2 - 1]) / 2;
}

void loop() {
  static unsigned long analogSampleTime = millis();
  static unsigned long printTime = millis();

  delay(1000);

  if (millis() - analogSampleTime > 40U) {
    analogSampleTime = millis();
    analogBuffer[analogBufferIndex++] = analogRead(TDS_SENSOR_PIN);
    if (analogBufferIndex >= TDS_SAMPLE_COUNT) {
      analogBufferIndex = 0;
    }
  }

  if (millis() - printTime > 800U) {
    printTime = millis();
    
    for (int i = 0; i < TDS_SAMPLE_COUNT; i++) {
      analogBufferTemp[i] = analogBuffer[i];
    }

    averageVoltage = getMedianNum(analogBufferTemp, TDS_SAMPLE_COUNT) * TDS_VOLTAGE_REFERENCE / 4096.0;

    float compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0);
    float compensationVoltage = averageVoltage / compensationCoefficient;

    tdsValue = 0.5 * compensationVoltage * (compensationVoltage * (133.42 * compensationVoltage - 255.86) + 857.39);

    tdsValueCalibrated = (tdsValue + tdsOffset)* calibrationFactor;
    if (tdsValueCalibrated < 0) tdsValueCalibrated = 0;
  }

  int currentWaterLevel = readWaterLevel();
  float currentPH = readPH();

  bool pumpStatus = currentWaterLevel < 3;
  pumpStatusText = pumpStatus ? "ON" : "OFF"; 
  digitalWrite(RELAY_PIN, pumpStatus ? LOW : HIGH);
  
  lcd.setCursor(4, 0); lcd.print("  ");
  lcd.setCursor(4, 0); lcd.print(currentWaterLevel);
  lcd.setCursor(4, 1); lcd.print("  ");
  lcd.setCursor(4, 1); lcd.print(currentPH, 1);
  lcd.setCursor(12, 0); lcd.print("    ");
  lcd.setCursor(12, 0); lcd.print(int(tdsValueCalibrated));
  lcd.setCursor(13, 1); lcd.print("    ");
  lcd.setCursor(13, 1); lcd.print(pumpStatusText);


  Serial.print("Level Air : "); Serial.print(currentWaterLevel); Serial.println(" cm");
  Serial.print("pH        : "); Serial.println(currentPH, 2);
  Serial.print("TDS       : "); Serial.print(int(tdsValueCalibrated)); Serial.println(" ppm");
  Serial.print("Pompa Air : "); Serial.print(pumpStatus);
  Serial.println("");

  if (Firebase.ready() && (millis() - dataMillis > 60000 || dataMillis == 0)) {
    dataMillis = millis();

    time_t nowUtc = time(nullptr);
    String documentId = String(nowUtc);

    struct tm tmLocal;
    localtime_r(&nowUtc, &tmLocal);

    char isoTime[30];
    strftime(isoTime, sizeof(isoTime), "%Y-%m-%dT%H:%M:%S", &tmLocal);
    strcat(isoTime, "+08:00");

    FirebaseJson content;
    content.set("fields/waterLevel/integerValue", currentWaterLevel); 
    content.set("fields/phValue/doubleValue", currentPH); 
    content.set("fields/tdsValue/integerValue", int(tdsValueCalibrated));  
    content.set("fields/pumpStatus/booleanValue", pumpStatus); 
    content.set("fields/timestamp/timestampValue", isoTime);

    String fullDocumentPath = "data/" + documentId;

    Serial.print("Sending data to Firestore: ");
    Serial.println(fullDocumentPath);

    if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", fullDocumentPath.c_str(), content.raw())) {
      Serial.println("Data stored to Firestore!");
    } else {
      Serial.print("Firestore write failed: ");
      Serial.println(fbdo.errorReason());
    }
  }
}
