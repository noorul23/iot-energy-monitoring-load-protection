/*
 * ================================================================
 *  IoT-ENABLED ENERGY MONITORING AND LOAD PROTECTION SYSTEM
 * ================================================================
 *
 *  Platform     : ESP32 (Arduino IDE)
 *  Purpose      : Real-time energy monitoring, IoT visualization,
 *                 GSM notifications, load protection, tamper
 *                 detection, and energy-data persistence.
 *
 *  Hardware:
 *    1. ESP32 DevKit V1
 *    2. PZEM-004T v3.0       (energy measurement, UART2)
 *    3. SIM900A GSM          (SMS, UART1)
 *    4. 16x2 LCD             (I2C, address 0x27)
 *    5. Single-channel Relay (active LOW, NC load arrangement)
 *    6. SW-420 Vibration Sensor (tamper detection)
 *    7. ESP32 EEPROM emulation (flash-backed persistence)
 *    8. Blynk IoT cloud
 *
 *  AUTHORITATIVE PIN MAP
 *    PZEM TX  -> GPIO 16 (ESP32 RX2)
 *    PZEM RX  -> GPIO 17 (ESP32 TX2)
 *    GSM TX   -> GPIO 27 (ESP32 RX1)
 *    GSM RX   -> GPIO 26 (ESP32 TX1)
 *    Relay    -> GPIO 25 (active LOW)
 *    Vibration-> GPIO 33
 *    LCD SDA  -> GPIO 21
 *    LCD SCL  -> GPIO 22
 *
 *  Core functions:
 *    - Measures voltage, current, power, energy, frequency and PF.
 *    - Sends measurements to Blynk for remote visualization.
 *    - Disconnects the load when configured power limit is exceeded.
 *    - Sends GSM SMS alerts for overload and tamper events.
 *    - Accepts a simple STATUS SMS command for system interaction.
 *    - Stores cumulative energy in EEPROM across restarts.
 *
 *  NOTE:
 *    Credentials must be supplied locally. Do not commit real
 *    Wi-Fi passwords, Blynk tokens, or phone numbers to GitHub.
 * ================================================================
 */

/* --------------------------- Blynk ----------------------------- */
#define BLYNK_TEMPLATE_ID   "YOUR_BLYNK_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "IoT Energy Monitoring"
#define BLYNK_AUTH_TOKEN    "YOUR_BLYNK_AUTH_TOKEN"

/* -------------------------- Includes ---------------------------- */
#include <WiFi.h>
#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <PZEM004Tv30.h>
#include <BlynkSimpleEsp32.h>
#include <HardwareSerial.h>

/* --------------------- User configuration ----------------------- */
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASS      "YOUR_WIFI_PASSWORD"

// Configurable load-protection threshold.
float watt_limit = 500.0;

// Alert destination / authorized STATUS sender.
#define ADMIN_PHONE   "+91XXXXXXXXXX"

#define LCD_ADDR       0x27
#define LCD_COLS       16
#define LCD_ROWS       2

/* ------------------------- GPIO map ----------------------------- */
#define PZEM_RX_PIN    16
#define PZEM_TX_PIN    17

#define GSM_RX_PIN     27
#define GSM_TX_PIN     26

#define RELAY_PIN      25
#define VIBRATION_PIN  33

/* ------------------------ EEPROM map ---------------------------- */
#define EEPROM_SIZE        64
#define EEPROM_MAGIC_ADDR  0
#define EEPROM_ENERGY_ADDR 8
#define EEPROM_MAGIC_VALUE 0xB6B6B6B6UL

/* ------------------------- Timers ------------------------------- */
#define PZEM_INTERVAL          1000UL
#define LCD_INTERVAL           2000UL
#define BLYNK_INTERVAL         2000UL
#define GSM_CHECK_INTERVAL     3000UL
#define EEPROM_SAVE_INTERVAL  15000UL
#define TAMPER_DEBOUNCE        1000UL
#define WIFI_RETRY_INTERVAL   30000UL

/* ---------------------- Hardware objects ------------------------ */
PZEM004Tv30 pzem(Serial2, PZEM_RX_PIN, PZEM_TX_PIN);
HardwareSerial gsmSerial(1);
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

/* ------------------------- Measurements ------------------------- */
float voltage   = 0.0;
float current   = 0.0;
float power     = 0.0;
float energy    = 0.0;
float frequency = 0.0;
float pf        = 0.0;

/* ---------------------------- State ----------------------------- */
bool relayState      = false;
bool overloadActive  = false;
bool tamperActive    = false;
bool pzemReadOK      = false;
bool overloadSmsSent = false;
bool tamperSmsSent   = false;

/* -------------------------- Timers ------------------------------ */
unsigned long lastPzemRead       = 0;
unsigned long lastLcdUpdate      = 0;
unsigned long lastBlynkPush      = 0;
unsigned long lastGsmCheck       = 0;
unsigned long lastEepromSave     = 0;
unsigned long lastTamperTrigger  = 0;
unsigned long lastWifiRetry      = 0;

/* -------------------------- LCD state --------------------------- */
uint8_t lcdPage = 0;
#define LCD_TOTAL_PAGES 3

/* --------------------------- GSM -------------------------------- */
String gsmBuffer = "";

/* ---------------------- Forward declarations ------------------- */
void readEnergy();
void controlRelay();
void updateLCD();
void handleSMS();
void sendToBlynk();
void saveToEEPROM();
void loadFromEEPROM();
void sendSMS(const char* number, const String& message);
void initGSM();
void connectWiFi();
void checkWiFi();
void checkTamper();
String extractSenderNumber(const String& response);
void processSMSCommand(const String& sender, const String& message);
void sendStatusSMS(const String& sender);

/* ================================================================
 *                           SETUP
 * ================================================================ */
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println(F("=============================================="));
  Serial.println(F(" IoT ENERGY MONITORING & LOAD PROTECTION"));
  Serial.println(F("=============================================="));

  /* GPIO */
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(VIBRATION_PIN, INPUT_PULLUP);

  // Original hardware arrangement: LOW energizes relay coil,
  // opening the NC load path. Start with load disconnected.
  digitalWrite(RELAY_PIN, LOW);
  relayState = false;

  /* EEPROM */
  EEPROM.begin(EEPROM_SIZE);
  loadFromEEPROM();

  /* LCD */
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Energy Monitor"));
  lcd.setCursor(0, 1);
  lcd.print(F("Starting..."));

  /* GSM */
  gsmSerial.begin(9600, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
  delay(1000);
  initGSM();

  /* Wi-Fi / Blynk */
  connectWiFi();

  delay(1000);
  lcd.clear();

  Serial.println(F("[SYS ] Ready."));
}

/* ================================================================
 *                          MAIN LOOP
 * ================================================================ */
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();
  }

  unsigned long now = millis();

  /* Energy measurement and protection */
  if (now - lastPzemRead >= PZEM_INTERVAL) {
    lastPzemRead = now;
    readEnergy();
    if (pzemReadOK) {
      controlRelay();
    }
  }

  /* Local display */
  if (now - lastLcdUpdate >= LCD_INTERVAL) {
    lastLcdUpdate = now;
    updateLCD();
  }

  /* GSM SMS interaction / alert handling */
  if (now - lastGsmCheck >= GSM_CHECK_INTERVAL) {
    lastGsmCheck = now;
    handleSMS();
  }

  /* Blynk telemetry */
  if (now - lastBlynkPush >= BLYNK_INTERVAL) {
    lastBlynkPush = now;
    sendToBlynk();
  }

  /* Persistent energy storage */
  if (now - lastEepromSave >= EEPROM_SAVE_INTERVAL) {
    lastEepromSave = now;
    saveToEEPROM();
  }

  /* Tamper monitoring */
  checkTamper();

  /* Connectivity recovery */
  if (now - lastWifiRetry >= WIFI_RETRY_INTERVAL) {
    lastWifiRetry = now;
    checkWiFi();
  }
}

/* ================================================================
 *                        PZEM MEASUREMENT
 * ================================================================ */
void readEnergy() {
  float v = pzem.voltage();
  float c = pzem.current();
  float p = pzem.power();
  float e = pzem.energy();
  float f = pzem.frequency();
  float pf_read = pzem.pf();

  // Voltage/current/power are required for a valid protection cycle.
  if (isnan(v) || isnan(c) || isnan(p)) {
    pzemReadOK = false;
    Serial.println(F("[PZEM] Read failed - retaining last values."));
    return;
  }

  voltage = v;
  current = c;
  power = p;
  if (!isnan(e))  energy = e;
  if (!isnan(f))  frequency = f;
  if (!isnan(pf_read)) pf = pf_read;

  pzemReadOK = true;

  Serial.printf("[PZEM] V=%.1fV I=%.2fA P=%.1fW E=%.3fkWh F=%.1fHz PF=%.2f\n",
                voltage, current, power, energy, frequency, pf);
}

/* ================================================================
 *                         LOAD PROTECTION
 * ================================================================ */
void controlRelay() {
  if (!pzemReadOK) return;

  /* Detect overload */
  if (power > watt_limit) {
    if (!overloadActive) {
      overloadActive = true;
      overloadSmsSent = false;
      Serial.printf("[RELAY] OVERLOAD: %.1fW > %.1fW\n", power, watt_limit);
    }

    if (!overloadSmsSent) {
      overloadSmsSent = true;
      String msg = "OVERLOAD ALERT! Load: " + String(power, 1) +
                   "W exceeds limit " + String(watt_limit, 0) +
                   "W. Supply disconnected.";
      sendSMS(ADMIN_PHONE, msg);
    }
  } else if (overloadActive && power <= (watt_limit * 0.9)) {
    // Preserve the original firmware's recovery threshold.
    overloadActive = false;
    overloadSmsSent = false;
    Serial.println(F("[RELAY] Overload cleared."));
  }

  /* Relay uses the original active-LOW / NC arrangement. */
  bool shouldBeOn = !overloadActive;

  if (shouldBeOn && !relayState) {
    digitalWrite(RELAY_PIN, HIGH); // coil OFF -> NC closed -> load ON
    relayState = true;
    Serial.println(F("[RELAY] ON - supply connected."));
  } else if (!shouldBeOn && relayState) {
    digitalWrite(RELAY_PIN, LOW);  // coil ON -> NC opens -> load OFF
    relayState = false;
    Serial.println(F("[RELAY] OFF - supply disconnected."));
  }
}

/* ================================================================
 *                            LCD
 * ================================================================ */
void updateLCD() {
  lcd.clear();

  if (tamperActive) {
    lcd.setCursor(0, 0);
    lcd.print(F("!! TAMPER !!"));
    lcd.setCursor(0, 1);
    lcd.print(F("CHECK METER"));
    return;
  }

  if (overloadActive) {
    lcd.setCursor(0, 0);
    lcd.print(F("!! OVERLOAD !!"));
    lcd.setCursor(0, 1);
    lcd.print(String(power, 1));
    lcd.print(F("W >"));
    lcd.print(String(watt_limit, 0));
    lcd.print(F("W"));
    return;
  }

  switch (lcdPage) {
    case 0:
      lcd.setCursor(0, 0);
      lcd.print(F("V:"));
      lcd.print(String(voltage, 1));
      lcd.print(F("V "));
      lcd.print(String(frequency, 1));
      lcd.print(F("Hz"));

      lcd.setCursor(0, 1);
      lcd.print(F("I:"));
      lcd.print(String(current, 2));
      lcd.print(F("A PF:"));
      lcd.print(String(pf, 2));
      break;

    case 1:
      lcd.setCursor(0, 0);
      lcd.print(F("P:"));
      lcd.print(String(power, 1));
      lcd.print(F("W Lim:"));
      lcd.print(String(watt_limit, 0));

      lcd.setCursor(0, 1);
      lcd.print(F("Protect:"));
      lcd.print(overloadActive ? F("TRIP") : F("OK"));
      break;

    case 2:
      lcd.setCursor(0, 0);
      lcd.print(F("E:"));
      lcd.print(String(energy, 3));
      lcd.print(F("kWh"));

      lcd.setCursor(0, 1);
      lcd.print(F("Load:"));
      lcd.print(relayState ? F("ON ") : F("OFF"));
      lcd.print(pzemReadOK ? F(" OK") : F(" ERR"));
      break;
  }

  lcdPage = (lcdPage + 1) % LCD_TOTAL_PAGES;
}

/* ================================================================
 *                       GSM SMS HANDLING
 * ================================================================
 * Supported user command:
 *   STATUS
 *
 * STATUS is accepted only from ADMIN_PHONE. This keeps the simple
 * interaction mechanism from becoming an unauthenticated control
 * channel.
 * ================================================================ */
void handleSMS() {
  gsmSerial.println("AT+CMGL=\"REC UNREAD\"");
  delay(500);

  gsmBuffer = "";
  unsigned long timeout = millis() + 2000;
  while (millis() < timeout) {
    while (gsmSerial.available()) {
      gsmBuffer += (char)gsmSerial.read();
    }
  }

  if (gsmBuffer.length() == 0) return;

  Serial.println(F("[GSM ] Incoming SMS data:"));
  Serial.println(gsmBuffer);

  String sender = extractSenderNumber(gsmBuffer);
  String upper = gsmBuffer;
  upper.toUpperCase();

  if (upper.indexOf("STATUS") >= 0) {
    processSMSCommand(sender, "STATUS");
  }

  // Remove messages that have been processed/read.
  gsmSerial.println("AT+CMGDA=\"DEL READ\"");
  delay(500);
  while (gsmSerial.available()) gsmSerial.read();
}

void processSMSCommand(const String& sender, const String& message) {
  if (sender.length() == 0) {
    Serial.println(F("[GSM ] Could not identify SMS sender."));
    return;
  }

  String authorized = String(ADMIN_PHONE);
  String normalizedSender = sender;
  normalizedSender.trim();
  authorized.trim();

  if (normalizedSender != authorized) {
    Serial.println(F("[GSM ] STATUS rejected: unauthorized sender."));
    return;
  }

  if (message == "STATUS") {
    sendStatusSMS(sender);
  }
}

void sendStatusSMS(const String& sender) {
  String status = "SYSTEM STATUS\n";
  status += "V: " + String(voltage, 1) + "V\n";
  status += "I: " + String(current, 2) + "A\n";
  status += "P: " + String(power, 1) + "W\n";
  status += "E: " + String(energy, 3) + "kWh\n";
  status += "Limit: " + String(watt_limit, 0) + "W\n";
  status += "Load: ";
  status += relayState ? "ON\n" : "OFF\n";
  status += "Protection: ";
  status += overloadActive ? "TRIPPED" : "NORMAL";

  sendSMS(sender.c_str(), status);
}

String extractSenderNumber(const String& response) {
  int cmglIdx = response.indexOf("+CMGL:");
  if (cmglIdx < 0) return "";

  int firstQuote = response.indexOf('"', cmglIdx);
  if (firstQuote < 0) return "";
  int secondQuote = response.indexOf('"', firstQuote + 1);
  if (secondQuote < 0) return "";
  int thirdQuote = response.indexOf('"', secondQuote + 1);
  if (thirdQuote < 0) return "";
  int fourthQuote = response.indexOf('"', thirdQuote + 1);
  if (fourthQuote < 0) return "";

  String number = response.substring(thirdQuote + 1, fourthQuote);
  number.trim();
  return number;
}

void sendSMS(const char* number, const String& message) {
  Serial.print(F("[GSM ] Sending SMS to "));
  Serial.println(number);

  gsmSerial.println("AT+CMGS=\"" + String(number) + "\"");
  delay(300);
  gsmSerial.print(message);
  delay(100);
  gsmSerial.write(0x1A);
  delay(3000);

  while (gsmSerial.available()) {
    Serial.print((char)gsmSerial.read());
  }
  Serial.println();
  Serial.println(F("[GSM ] SMS transmission complete."));
}

void initGSM() {
  Serial.println(F("[GSM ] Initialising SIM900A..."));

  gsmSerial.println("AT");
  delay(1000);
  while (gsmSerial.available()) Serial.print((char)gsmSerial.read());

  gsmSerial.println("ATE0");
  delay(500);
  while (gsmSerial.available()) gsmSerial.read();

  gsmSerial.println("AT+CMGF=1");
  delay(500);
  while (gsmSerial.available()) gsmSerial.read();

  gsmSerial.println("AT+CPMS=\"SM\",\"SM\",\"SM\"");
  delay(500);
  while (gsmSerial.available()) gsmSerial.read();

  gsmSerial.println("AT+CSCS=\"GSM\"");
  delay(500);
  while (gsmSerial.available()) gsmSerial.read();

  gsmSerial.println("AT+CMGDA=\"DEL READ\"");
  delay(1000);
  while (gsmSerial.available()) gsmSerial.read();

  Serial.println(F("[GSM ] SIM900A ready."));
}

/* ================================================================
 *                           BLYNK
 * ================================================================
 * V0 -> Voltage
 * V1 -> Current
 * V2 -> Power
 * V3 -> Energy
 * V4 -> Overload status
 * V5 -> Frequency
 * V6 -> Power factor
 * V7 -> Relay state
 * ================================================================ */
void sendToBlynk() {
  if (WiFi.status() != WL_CONNECTED) return;

  Blynk.virtualWrite(V0, voltage);
  Blynk.virtualWrite(V1, current);
  Blynk.virtualWrite(V2, power);
  Blynk.virtualWrite(V3, energy);
  Blynk.virtualWrite(V4, overloadActive ? 1 : 0);
  Blynk.virtualWrite(V5, frequency);
  Blynk.virtualWrite(V6, pf);
  Blynk.virtualWrite(V7, relayState ? 1 : 0);
}

/* ================================================================
 *                       TAMPER DETECTION
 * ================================================================ */
void checkTamper() {
  int vibrationState = digitalRead(VIBRATION_PIN);

  if (vibrationState == HIGH) {
    unsigned long now = millis();

    if (now - lastTamperTrigger >= TAMPER_DEBOUNCE) {
      lastTamperTrigger = now;

      if (!tamperActive) {
        tamperActive = true;
        Serial.println(F("[TAMP] Vibration/tamper detected."));

        if (!tamperSmsSent) {
          tamperSmsSent = true;
          sendSMS(ADMIN_PHONE,
                  "TAMPER ALERT! Vibration detected on energy monitoring system. Inspect the meter.");
        }
      }
    }
  } else if (tamperActive) {
    unsigned long now = millis();
    if (now - lastTamperTrigger >= (TAMPER_DEBOUNCE * 3)) {
      tamperActive = false;
      tamperSmsSent = false;
      Serial.println(F("[TAMP] Tamper condition cleared."));
    }
  }
}

/* ================================================================
 *                       EEPROM PERSISTENCE
 * ================================================================
 * Only cumulative energy is persisted in the reconstructed system.
 * The original energy address is retained for compatibility.
 * ================================================================ */
void saveToEEPROM() {
  unsigned long magic = EEPROM_MAGIC_VALUE;
  EEPROM.put(EEPROM_MAGIC_ADDR, magic);
  EEPROM.put(EEPROM_ENERGY_ADDR, energy);
  EEPROM.commit();

  Serial.printf("[EEPR] Saved energy: %.3f kWh\n", energy);
}

void loadFromEEPROM() {
  unsigned long magic;
  EEPROM.get(EEPROM_MAGIC_ADDR, magic);

  if (magic == EEPROM_MAGIC_VALUE) {
    EEPROM.get(EEPROM_ENERGY_ADDR, energy);

    if (isnan(energy) || energy < 0.0) {
      energy = 0.0;
    }

    Serial.printf("[EEPR] Restored energy: %.3f kWh\n", energy);
  } else {
    energy = 0.0;
    Serial.println(F("[EEPR] No valid energy record; starting at 0.000 kWh."));
    saveToEEPROM();
  }
}

/* ================================================================
 *                         WI-FI / BLYNK
 * ================================================================ */
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print('.');
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print(F("[WiFi] Connected. IP: "));
    Serial.println(WiFi.localIP());

    Blynk.config(BLYNK_AUTH_TOKEN);
    Blynk.connect(5000);

    if (Blynk.connected()) {
      Serial.println(F("[BLNK] Connected."));
    } else {
      Serial.println(F("[BLNK] Connection unavailable; will retry."));
    }
  } else {
    Serial.println();
    Serial.println(F("[WiFi] Connection failed; background retry enabled."));
  }
}

void checkWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[WiFi] Disconnected - attempting reconnect..."));
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < 3000) {
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.print(F("[WiFi] Reconnected. IP: "));
      Serial.println(WiFi.localIP());
      Blynk.connect(3000);
    }
  }

  if (WiFi.status() == WL_CONNECTED && !Blynk.connected()) {
    Blynk.connect(3000);
  }
}

/* ================================================================
 *  END OF FIRMWARE
 * ================================================================ */
