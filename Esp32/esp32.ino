#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <MFRC522.h>
#include <time.h>
#include <vector>

// Pin Definitions
#define SCK_PIN     18 // D18
#define MISO_PIN    19 // D19
#define MOSI_PIN    23 // D23
#define RST_PIN     22 // Shared Reset Pin for all RFIDs D22

#define SS_1_PIN     5 // Reader 1: LECTURE IN                D5
#define SS_2_PIN     4 // Reader 2: LECTURE OUT               D4
#define SS_3_PIN     2 // Reader 3: MAIN GATE SUBSYSTEM       D2

#define BUZZER_PIN  26 // D26
#define GREEN_LED   16
#define RED_LED     15

#define LDR1_PIN    34
#define LDR2_PIN    35

// Main Gate Ultrasonic Sensor Pins
#define TRIG_PIN    13
#define ECHO_PIN    12

// Lecture Room PIR Sensor Pin
#define PIR_PIN     27

#define NR_OF_READERS 3

byte ssPins[] = {SS_1_PIN, SS_2_PIN, SS_3_PIN};
MFRC522 mfrc522[NR_OF_READERS];

// Wi-Fi & API Endpoints
const char* ssid             = "Galaxy S21+";
const char* password         = "Gishan2006";

const char* apiLecEndpoint    = "https://backend-ten-zeta-42.vercel.app/updateAttend";
const char* apiGateEndpoint   = "https://backend-ten-zeta-42.vercel.app/updateMainGate";
const char* apiRegisterCard   = "https://backend-ten-zeta-42.vercel.app/registerCard";
const char* apiGetMappings   = "https://backend-ten-zeta-42.vercel.app/getCardMappings";
const char* apiSystemStatus  = "https://backend-ten-zeta-42.vercel.app/updateSystemStatus";

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800; 
const int daylightOffset_sec = 0;

struct CardMapping {
  String cardUID;
  String studentID;
};

std::vector<CardMapping> registeredCards;
bool isRegisterMode = false;

// Temporary Storage Vectors
std::vector<String> mainGateScans;
std::vector<String> checkInLec1;
std::vector<String> checkInLec2;
std::vector<String> checkInLec3;
std::vector<String> checkInLec4;

String lastActiveDate = "";
bool overrideLDR1 = false;
bool overrideLDR2 = false;

// Function Declarations
void fetchCardMappingsFromDB();
void saveCardMappingToDB(String uid, String studentID);
void processSerialCommands();
void handleRegistrationProcess(uint8_t readerIndex, String scannedUID);
String getStudentIDFromCard(String scannedUID);
void printSystemStatus();
void printStoredVectors();
void printRegisteredCards();
String getCurrentDate();
String getCurrentTime();
int getCurrentLecture();
std::vector<String>* getLectureVector(int lectureNum);
bool isCardInVector(std::vector<String>& vec, String cardUID);
void removeFromVector(std::vector<String>& vec, String cardUID);
void triggerLoudSmallBeep();
void triggerLoudLongBeep();
void sendLectureAttendance(String studentID, String dateStr, int lectureNum);
void sendMainGateEntry(String studentID, String dateStr, String timeStr);
void sendSystemTelemetry(int queueDistCm, bool roomPresence);
int readUltrasonicDistance();
// void logSensorDiagnostics();  //uncomment if need to debug sensor readings
void deselectAllReaders();

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==========================================");
  Serial.println("ESP32 System with Database Card Registration");
  Serial.println("Commands: REGISTER, STATUS, LIST, CARDS, CLEAR, RESET");
  Serial.println("==========================================");

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(LDR1_PIN, INPUT);
  pinMode(LDR2_PIN, INPUT);

  // Ultrasonic Sensor Setup
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // PIR Motion Sensor Setup
  pinMode(PIR_PIN, INPUT);

  // Setup SS Pins as Output and Deselect All
  for (uint8_t i = 0; i < NR_OF_READERS; i++) {
    pinMode(ssPins[i], OUTPUT);
    digitalWrite(ssPins[i], HIGH);
  }

  // Active-LOW Buzzer initialization
  digitalWrite(BUZZER_PIN, HIGH); 
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN);

  // Initialize each RC522 reader individually with explicit SS toggling
  for (uint8_t reader = 0; reader < NR_OF_READERS; reader++) {
    digitalWrite(ssPins[reader], LOW);
    mfrc522[reader].PCD_Init(ssPins[reader], RST_PIN);
    digitalWrite(ssPins[reader], HIGH);
    Serial.printf("Reader %d Initialized on SS Pin GPIO %d\n", reader + 1, ssPins[reader]);
  }

  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWi-Fi Connected!");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.print("Synchronizing NTP Time");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\nTime Synchronized!");

  fetchCardMappingsFromDB();
}

void loop() {
  processSerialCommands();

  logSensorDiagnostics();

  String currentDate = getCurrentDate();
  if (currentDate != lastActiveDate) {
    mainGateScans.clear();
    checkInLec1.clear();
    checkInLec2.clear();
    checkInLec3.clear();
    checkInLec4.clear();
    lastActiveDate = currentDate;
    Serial.println("\n[SYSTEM] New day detected. Temporary lists cleared.");
  }

  int currentLecture = getCurrentLecture();

  // Iterate through each connected MFRC522 RFID reader
  for (uint8_t reader = 0; reader < NR_OF_READERS; reader++) {
    // Force SPI arbitration on active reader
    deselectAllReaders();
    digitalWrite(ssPins[reader], LOW);
    delayMicroseconds(50);

    if (mfrc522[reader].PICC_IsNewCardPresent() && mfrc522[reader].PICC_ReadCardSerial()) {
      
      String scannedUID = "";
      for (byte i = 0; i < mfrc522[reader].uid.size; i++) {
        if (mfrc522[reader].uid.uidByte[i] < 0x10) scannedUID += "0";
        scannedUID += String(mfrc522[reader].uid.uidByte[i], HEX);
      }
      scannedUID.toUpperCase();

      if (isRegisterMode) {
        handleRegistrationProcess(reader, scannedUID);
        mfrc522[reader].PICC_HaltA();
        mfrc522[reader].PCD_StopCrypto1();
        digitalWrite(ssPins[reader], HIGH);
        delay(1000);
        continue;
      }

      String studentID = getStudentIDFromCard(scannedUID);

      // Reader 3: Main Gate Subsystem
      if (reader == 2) {
        String currentTimeStr = getCurrentTime();

        if (isCardInVector(mainGateScans, studentID)) {
          Serial.printf("\n[MAIN GATE ALERT] Student ID: %s (UID: %s) already scanned!\n", studentID.c_str(), scannedUID.c_str());
          triggerLoudLongBeep();
        } 
        else {
          mainGateScans.push_back(studentID);
          Serial.printf("\n[MAIN GATE SCAN] Student ID: %s (UID: %s) | Time: %s\n", studentID.c_str(), scannedUID.c_str(), currentTimeStr.c_str());
          sendMainGateEntry(studentID, currentDate, currentTimeStr);
        }
      }
      // Readers 1 & 2: Lecture Hall IN/OUT
      else {
        Serial.printf("\n[Lecture Reader %d] Student ID: %s | Active Lecture: %d\n", reader + 1, studentID.c_str(), currentLecture);

        if (currentLecture == -1) {
          Serial.println("[REJECTED] Outside active lecture periods!");
          triggerLoudLongBeep();
        } 
        else {
          std::vector<String>* activeVec = getLectureVector(currentLecture);

          int rawAnalogLDR1 = analogRead(LDR1_PIN);
          int rawAnalogLDR2 = analogRead(LDR2_PIN);

          // LDR unblocks whenever light hits sensor (analog reading != 0)
          bool ldr1Blocked = (rawAnalogLDR1 == 0) && !overrideLDR1;
          bool ldr2Blocked = (rawAnalogLDR2 == 0) && !overrideLDR2;

          if (reader == 0) {
            if (ldr1Blocked) {
              Serial.printf("[REJECTED] LDR 1 Blocked (Raw Analog: %d)! Tap IN blocked on Reader 1.\n", rawAnalogLDR1);
              triggerLoudLongBeep();
            } 
            else if (isCardInVector(*activeVec, studentID)) {
              Serial.println("[LOCAL ALERT] Student already checked-in!");
              triggerLoudLongBeep();
            } 
            else {
              activeVec->push_back(studentID);
              Serial.printf("[CHECK-IN SUCCESS] Student %s stored in Temp List for Lecture %d.\n", studentID.c_str(), currentLecture);
              triggerLoudSmallBeep();
            }
          } 
          else if (reader == 1) {
            if (ldr2Blocked) {
              Serial.printf("[REJECTED] LDR 2 Blocked (Raw Analog: %d)! Tap OUT blocked on Reader 2.\n", rawAnalogLDR2);
              triggerLoudLongBeep();
            } 
            else if (!isCardInVector(*activeVec, studentID)) {
              Serial.println("[REJECTED] Student did NOT Tap IN for this lecture!");
              triggerLoudLongBeep();
            } 
            else {
              Serial.println("[TAP OUT VERIFIED] Uploading Attendance Record to Server...");
              sendLectureAttendance(studentID, currentDate, currentLecture);
              removeFromVector(*activeVec, studentID);
            }
          }
        }
      }

      mfrc522[reader].PICC_HaltA();
      mfrc522[reader].PCD_StopCrypto1();
      digitalWrite(ssPins[reader], HIGH);
      delay(1200);
    }
    digitalWrite(ssPins[reader], HIGH);
  }
}

void deselectAllReaders() {
  for (uint8_t i = 0; i < NR_OF_READERS; i++) {
    digitalWrite(ssPins[i], HIGH);
  }
}

// Read Distance in CM from HC-SR04 Ultrasonic Sensor
int readUltrasonicDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout
  if (duration == 0) return 400; // Return max out of range distance if no ping
  int distanceCm = duration * 0.034 / 2;
  return distanceCm;
}

// Send live queue length and room presence data to server
void sendSystemTelemetry(int queueDistCm, bool roomPresence) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    if (http.begin(client, apiSystemStatus)) {
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      http.addHeader("Content-Type", "application/json");

      String jsonBody = "{\"queue_cm\":" + String(queueDistCm) + ",\"presence\":" + String(roomPresence ? "true" : "false") + "}";
      http.POST(jsonBody);
      http.end();
    }
  }
}

// Logs diagnostic values of all connected hardware sensors periodically
void logSensorDiagnostics() {
  static unsigned long lastLogTime = 0;
  const unsigned long LOG_INTERVAL = 3000;

  if (millis() - lastLogTime >= LOG_INTERVAL) {
    lastLogTime = millis();

    int rawLdr1 = analogRead(LDR1_PIN);
    int rawLdr2 = analogRead(LDR2_PIN);

    // Read Ultrasonic Distance and PIR Motion State
    int gateQueueDist = readUltrasonicDistance();
    bool isLecRoomOccupied = (digitalRead(PIR_PIN) == HIGH);

    // Read firmware registers with explicit SS pin toggling
    byte fwVers[NR_OF_READERS];
    for (uint8_t r = 0; r < NR_OF_READERS; r++) {
      deselectAllReaders();
      digitalWrite(ssPins[r], LOW);
      delayMicroseconds(50);
      fwVers[r] = mfrc522[r].PCD_ReadRegister(mfrc522[r].VersionReg);
      digitalWrite(ssPins[r], HIGH);
    }

    Serial.println("\n============ [SENSOR TELEMETRY LOG] ============");
    Serial.printf("Time / Date : %s %s | Active Lecture: %d\n", getCurrentTime().c_str(), getCurrentDate().c_str(), getCurrentLecture());
    Serial.printf("Wi-Fi Signal: RSSI %d dBm | Status: %s\n", WiFi.RSSI(), (WiFi.status() == WL_CONNECTED) ? "ONLINE" : "OFFLINE");
    
    Serial.printf("LDR1 (Pin 34): Raw Analog = %4d | Status = %s %s\n", 
                  rawLdr1, 
                  (rawLdr1 != 0) ? "INTACT / UNBLOCKED" : "BLOCKED / TRIGGERED (0)",
                  overrideLDR1 ? "[OVERRIDDEN]" : "");
    Serial.printf("LDR2 (Pin 35): Raw Analog = %4d | Status = %s %s\n", 
                  rawLdr2, 
                  (rawLdr2 != 0) ? "INTACT / UNBLOCKED" : "BLOCKED / TRIGGERED (0)",
                  overrideLDR2 ? "[OVERRIDDEN]" : "");

    Serial.printf("Main Gate Ultrasonic Queue Dist: %d cm\n", gateQueueDist);
    Serial.printf("Lecture Hall Motion / PIR Sensor: %s\n", isLecRoomOccupied ? "MOTION DETECTED (OCCUPIED)" : "NO MOTION (EMPTY)");

    Serial.printf("RFID 1 (Lec IN)  : SS Pin 05 | FW Reg: 0x%02X (%s)\n", fwVers[0], (fwVers[0] == 0x91 || fwVers[0] == 0x92) ? "OK" : "NO COMM / CHECK SPI");
    Serial.printf("RFID 2 (Lec OUT) : SS Pin 04 | FW Reg: 0x%02X (%s)\n", fwVers[1], (fwVers[1] == 0x91 || fwVers[1] == 0x92) ? "OK" : "NO COMM / CHECK SPI");
    Serial.printf("RFID 3 (MainGate): SS Pin 02 | FW Reg: 0x%02X (%s)\n", fwVers[2], (fwVers[2] == 0x91 || fwVers[2] == 0x92) ? "OK" : "NO COMM / CHECK SPI");
    Serial.println("=================================================");

    // Send updated telemetry payload to web API
    sendSystemTelemetry(gateQueueDist, isLecRoomOccupied);
  }
}

void fetchCardMappingsFromDB() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    Serial.println("[DATABASE Sync] Fetching registered card mappings from Firebase...");

    if (http.begin(client, apiGetMappings)) {
      int httpCode = http.GET();
      if (httpCode == 200) {
        String payload = http.getString();
        registeredCards.clear();

        int dataIndex = payload.indexOf("\"data\":{");
        if (dataIndex != -1) {
          String dataStr = payload.substring(dataIndex + 7);
          dataStr.replace("}", "");
          dataStr.replace("{", "");

          int start = 0;
          while (start < dataStr.length()) {
            int keyStart = dataStr.indexOf("\"", start);
            if (keyStart == -1) break;
            int keyEnd = dataStr.indexOf("\"", keyStart + 1);
            if (keyEnd == -1) break;

            int valStart = dataStr.indexOf("\"", keyEnd + 2);
            if (valStart == -1) break;
            int valEnd = dataStr.indexOf("\"", valStart + 1);
            if (valEnd == -1) break;

            String uid = dataStr.substring(keyStart + 1, keyEnd);
            String studentID = dataStr.substring(valStart + 1, valEnd);

            registeredCards.push_back({uid, studentID});
            start = valEnd + 1;
          }
        }
        Serial.printf("[DATABASE Sync] Successfully loaded %d card mappings!\n", registeredCards.size());
      } else {
        Serial.printf("[DATABASE Sync Error] HTTP %d\n", httpCode);
      }
      http.end();
    }
  }
}

void saveCardMappingToDB(String uid, String studentID) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    if (http.begin(client, apiRegisterCard)) {
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      http.addHeader("Content-Type", "application/json");

      String jsonBody = "{\"uid\":\"" + uid + "\",\"student_id\":\"" + studentID + "\"}";
      int httpCode = http.POST(jsonBody);

      if (httpCode == 200 || httpCode == 201) {
        Serial.println("[DATABASE] Card mapping saved permanently to Firebase!");
      } else {
        Serial.printf("[DATABASE ERROR] Failed to save card mapping. HTTP: %d\n", httpCode);
      }
      http.end();
    }
  }
}

void handleRegistrationProcess(uint8_t readerIndex, String scannedUID) {
  triggerLoudSmallBeep();
  Serial.println("\n--------------------------------------------------");
  Serial.printf("[REGISTRATION] RFID Card Detected on Reader %d!\n", readerIndex + 1);
  Serial.printf("[REGISTRATION] Hardware Card UID: %s\n", scannedUID.c_str());
  Serial.println("[REGISTRATION] Enter Student ID in Serial Monitor (e.g. CODCSD-001):");
  Serial.println("--------------------------------------------------");

  while (Serial.available()) Serial.read();

  String customID = "";
  unsigned long timeout = millis();

  while (millis() - timeout < 20000) {
    if (Serial.available() > 0) {
      char c = (char)Serial.read();
      if (c == '\n' || c == '\r') {
        customID.trim();
        if (customID.length() > 0) break;
      } else {
        customID += c;
      }
    }
    delay(10);
  }

  if (customID.length() > 0) {
    bool updated = false;
    for (size_t i = 0; i < registeredCards.size(); i++) {
      if (registeredCards[i].cardUID == scannedUID) {
        registeredCards[i].studentID = customID;
        updated = true;
        break;
      }
    }

    if (!updated) {
      registeredCards.push_back({scannedUID, customID});
    }

    saveCardMappingToDB(scannedUID, customID);

    Serial.println("\n[SUCCESS] CARD REGISTERED AND SAVED TO DATABASE!");
    Serial.printf(" Hardware UID : %s\n", scannedUID.c_str());
    Serial.printf(" Student ID   : %s\n", customID.c_str());
    triggerLoudSmallBeep();
  } else {
    Serial.println("\n[TIMEOUT / CANCELLED] Registration cancelled due to no input.");
    triggerLoudLongBeep();
  }

  isRegisterMode = false;
  Serial.println("[REGISTRATION] Registration Mode Exited. Normal System Resumed.");
}

String getStudentIDFromCard(String scannedUID) {
  for (size_t i = 0; i < registeredCards.size(); i++) {
    if (registeredCards[i].cardUID == scannedUID) {
      return registeredCards[i].studentID;
    }
  }
  return scannedUID;
}

void sendMainGateEntry(String studentID, String dateStr, String timeStr) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    if (http.begin(client, apiGateEndpoint)) {
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      http.addHeader("Content-Type", "application/json");

      String jsonBody = "{\"id\":\"" + studentID + "\",\"date\":\"" + dateStr + "\",\"time\":\"" + timeStr + "\"}";
      int httpCode = http.POST(jsonBody);

      if (httpCode == 200 || httpCode == 201) {
        Serial.println("[SUCCESS] Main Gate access logged on server!");
        triggerLoudSmallBeep();
      } else {
        Serial.printf("[ALERT] Server returned error: %d\n", httpCode);
        triggerLoudLongBeep();
      }
      http.end();
    }
  } else {
    Serial.println("Wi-Fi disconnected!");
    triggerLoudLongBeep();
  }
}

void sendLectureAttendance(String studentID, String dateStr, int lectureNum) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    if (http.begin(client, apiLecEndpoint)) {
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      http.addHeader("Content-Type", "application/json");

      String jsonBody = "{\"id\":\"" + studentID + "\",\"date\":\"" + dateStr + "\",\"lecture\":" + String(lectureNum) + "}";
      int httpCode = http.POST(jsonBody);

      if (httpCode == 200 || httpCode == 201) {
        Serial.println("[SUCCESS] Lecture attendance updated on server!");
        triggerLoudSmallBeep();
      } else {
        Serial.printf("[ALERT] Server returned error: %d\n", httpCode);
        triggerLoudLongBeep();
      }
      http.end();
    }
  } else {
    Serial.println("Wi-Fi disconnected!");
    triggerLoudLongBeep();
  }
}

void processSerialCommands() {
  static String inputBuffer = "";
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      inputBuffer.trim();
      inputBuffer.toUpperCase();
      if (inputBuffer.length() > 0) {
        if (inputBuffer == "REGISTER" || inputBuffer == "REG") {
          isRegisterMode = true;
          Serial.println("\n[SYSTEM] === CARD REGISTRATION MODE ACTIVE ===");
          Serial.println("[SYSTEM] Tap ANY RFID card on Reader 1, 2, or 3...");
        }
        else if (inputBuffer == "STATUS" || inputBuffer == "S") printSystemStatus();
        else if (inputBuffer == "LIST" || inputBuffer == "L") printStoredVectors();
        else if (inputBuffer == "CARDS") printRegisteredCards();
        else if (inputBuffer == "CLEAR" || inputBuffer == "C") {
          mainGateScans.clear();
          checkInLec1.clear(); checkInLec2.clear(); checkInLec3.clear(); checkInLec4.clear();
          Serial.println("[COMMAND] All temporary vectors (including Main Gate) cleared.");
        }
        else if (inputBuffer == "TEST1") { overrideLDR1 = !overrideLDR1; Serial.printf("[COMMAND] LDR1 Override: %s\n", overrideLDR1 ? "ON" : "OFF"); }
        else if (inputBuffer == "TEST2") { overrideLDR2 = !overrideLDR2; Serial.printf("[COMMAND] LDR2 Override: %s\n", overrideLDR2 ? "ON" : "OFF"); }
        else if (inputBuffer == "RESET" || inputBuffer == "R") { ESP.restart(); }
      }
      inputBuffer = "";
    } else {
      inputBuffer += c;
    }
  }
}

void printRegisteredCards() {
  Serial.println("\n---------------- REGISTERED CARDS MAPPING ----------------");
  if (registeredCards.empty()) {
    Serial.println("No custom student IDs registered yet. Use command 'REGISTER'.");
  } else {
    for (size_t i = 0; i < registeredCards.size(); i++) {
      Serial.printf(" [%d] Hardware UID: %s  ==>  Student ID: %s\n", i + 1, registeredCards[i].cardUID.c_str(), registeredCards[i].studentID.c_str());
    }
  }
  Serial.println("----------------------------------------------------------");
}

void printSystemStatus() {
  Serial.println("\n----------------- SYSTEM STATUS -----------------");
  Serial.printf("Wi-Fi Status    : %s (IP: %s)\n", (WiFi.status() == WL_CONNECTED) ? "CONNECTED" : "DISCONNECTED", WiFi.localIP().toString().c_str());
  Serial.printf("Current Date    : %s | Time: %s\n", getCurrentDate().c_str(), getCurrentTime().c_str());
  Serial.printf("Active Lecture  : %d\n", getCurrentLecture());
  Serial.printf("Registered Cards: %d\n", registeredCards.size());
  Serial.printf("Vector Sizes    : Gate: %d | Lec1: %d | Lec2: %d | Lec3: %d | Lec4: %d\n", mainGateScans.size(), checkInLec1.size(), checkInLec2.size(), checkInLec3.size(), checkInLec4.size());
  Serial.println("--------------------------------------------------");
}

void printStoredVectors() {
  Serial.println("\n---------------- STORED TEMPORARY LISTS ----------------");
  Serial.printf("Main Gate Scanned Student IDs (%d total):\n", mainGateScans.size());
  for (size_t j = 0; j < mainGateScans.size(); j++) {
    Serial.printf("  [%d] %s\n", j + 1, mainGateScans[j].c_str());
  }

  std::vector<String>* vecs[] = {&checkInLec1, &checkInLec2, &checkInLec3, &checkInLec4};
  for (int i = 0; i < 4; i++) {
    Serial.printf("Lecture %d Checked-In Student IDs (%d total):\n", i + 1, vecs[i]->size());
    for (size_t j = 0; j < vecs[i]->size(); j++) {
      Serial.printf("  [%d] %s\n", j + 1, (*vecs[i])[j].c_str());
    }
  }
  Serial.println("--------------------------------------------------------");
}

std::vector<String>* getLectureVector(int lectureNum) {
  switch (lectureNum) {
    case 1: return &checkInLec1;
    case 2: return &checkInLec2;
    case 3: return &checkInLec3;
    case 4: return &checkInLec4;
    default: return &checkInLec1;
  }
}

bool isCardInVector(std::vector<String>& vec, String cardUID) {
  for (size_t i = 0; i < vec.size(); i++) if (vec[i] == cardUID) return true;
  return false;
}

void removeFromVector(std::vector<String>& vec, String cardUID) {
  for (size_t i = 0; i < vec.size(); i++) {
    if (vec[i] == cardUID) { vec.erase(vec.begin() + i); break; }
  }
}

String getCurrentDate() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  if (!localtime_r(&now, &timeinfo)) return "2026-10-17";
  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
  return String(buf);
}

String getCurrentTime() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  if (!localtime_r(&now, &timeinfo)) return "00:00:00";
  char buf[9];
  strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
  return String(buf);
}

int getCurrentLecture() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  if (!localtime_r(&now, &timeinfo)) return -1;
  int currentMinutes = (timeinfo.tm_hour * 60) + timeinfo.tm_min;

  if (currentMinutes >= 540 && currentMinutes <= 600) return 1;       // 09:00 - 10:00
  else if (currentMinutes >= 630 && currentMinutes <= 720) return 2;  // 10:30 - 12:00
  else if (currentMinutes >= 780 && currentMinutes <= 870) return 3;  // 13:00 - 14:30
  else if (currentMinutes >= 900 && currentMinutes <= 960) return 4;  // 15:00 - 16:00
  return -1;
}

void triggerLoudSmallBeep() {
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(BUZZER_PIN, LOW);
  delay(120);
  digitalWrite(BUZZER_PIN, HIGH);
  digitalWrite(GREEN_LED, LOW);
}

void triggerLoudLongBeep() {
  digitalWrite(RED_LED, HIGH);
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(80);
  }
  digitalWrite(RED_LED, LOW);
}