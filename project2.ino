#define BLYNK_TEMPLATE_ID "TMPL6NVMO5_ll"
#define BLYNK_TEMPLATE_NAME "gas level1"
#define BLYNK_AUTH_TOKEN "p_7oyPVraU9kb1Iu0746qwX5mj7ChVKT"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP32Servo.h>
#include <BlynkSimpleEsp32.h>

// ==== Wi-Fi Credentials ====
char ssid[] = "TP-Link_6371";
char pass[] = "68863623";

// ==== Telegram Bots & Chat IDs ====
String botToken1 = "7994554684:AAGHWAGlhfbDa4rJvylamzJBF3MvX5lrSSI";
String chatID1 = "1453721808";

String botToken2 = "8155895427:AAFMh20JVeX58RDn40SwaLL1WQwlWLrNeRc";
String chatID2 = "5751595318";

// ==== Pins ====
const int gasSensorPin = 34;
const int buzzerPin = 26;
const int fanPin = 27;
const int servoPin = 25;
const int emergencyBtnPin = 33;  // Emergency OFF button (active LOW)
const int systemOnBtnPin = 32;   // System ON button (active LOW)

// ==== Threshold ====
const int threshold = 600;

// ==== States ====
bool gasAboveThreshold = false;
bool emergencyActive = false;
unsigned long alertStartTime = 0;

Servo regulatorServo;
BlynkTimer timer;

// ==== BLYNK EMERGENCY (V33) ====
BLYNK_WRITE(V33) {
  int val = param.asInt();
  if (val == 1 && !emergencyActive) {
    activateEmergency("⚠ Emergency shutdown activated! (via Blynk)");
  }
}

// ==== BLYNK SYSTEM RESET (V32) ====
BLYNK_WRITE(V32) {
  int val = param.asInt();
  if (val == 1 && emergencyActive) {
    deactivateEmergency("✅ System resumed normal operation (via Blynk)");
  }
}

// ==== Setup ====
void setup() {
  Serial.begin(115200);

  pinMode(buzzerPin, OUTPUT);
  pinMode(fanPin, OUTPUT);
  pinMode(emergencyBtnPin, INPUT_PULLUP);
  pinMode(systemOnBtnPin, INPUT_PULLUP);

  regulatorServo.attach(servoPin);
  regulatorServo.write(0); // Valve open initially

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  timer.setInterval(1000L, checkGasLevel);

  Serial.println("✅ System Ready");
}

// ==== Loop ====
void loop() {
  Blynk.run();
  timer.run();

  checkEmergencyButton();
  checkSystemOnButton();

  if (emergencyActive) return;
}

// ==== Activate Emergency ====
void activateEmergency(String message) {
  emergencyActive = true;
  digitalWrite(buzzerPin, LOW);
  digitalWrite(fanPin, LOW);
  regulatorServo.write(0);  // Valve open (cut off regulator)
  sendTelegramWithRetry(message);
  Serial.println(message);
}

// ==== Deactivate Emergency (Modified) ====
void deactivateEmergency(String message) {
  emergencyActive = false;
  sendTelegramWithRetry(message);
  Serial.println(message);

  // Immediately check gas level on resume
  int gasLevel = analogRead(gasSensorPin);
  Serial.print("Gas Level after resume: ");
  Serial.println(gasLevel);

  if (gasLevel > threshold) {
    gasAboveThreshold = true;
    alertStartTime = millis();

    digitalWrite(buzzerPin, HIGH);
    digitalWrite(fanPin, HIGH);
    regulatorServo.write(90);  // Close valve

    sendTelegramWithRetry("🚨 LPG GAS LEAK DETECTED!\nGas Level: " + String(gasLevel) + "\nAuto shut-off regulator activated (after system resume).");
  } else {
    gasAboveThreshold = false;

    digitalWrite(buzzerPin, LOW);
    digitalWrite(fanPin, LOW);
    regulatorServo.write(0);  // Open valve
  }
}

// ==== Check Emergency Physical Button ====
void checkEmergencyButton() {
  if (digitalRead(emergencyBtnPin) == LOW && !emergencyActive) {
    activateEmergency("⚠ Emergency shutdown activated! (via button)");
    delay(500); // debounce
  }
}

// ==== Check System ON Physical Button ====
void checkSystemOnButton() {
  if (digitalRead(systemOnBtnPin) == LOW && emergencyActive) {
    deactivateEmergency("✅ System resumed normal operation (via button)");
    delay(500); // debounce
  }
}

// ==== Gas Level Monitoring ====
void checkGasLevel() {
  if (emergencyActive) return;

  int gasLevel = analogRead(gasSensorPin);
  Serial.print("Gas Level: ");
  Serial.println(gasLevel);

  Blynk.virtualWrite(V34, gasLevel);

  unsigned long currentTime = millis();

  if (gasLevel > threshold) {
    if (!gasAboveThreshold) {
      gasAboveThreshold = true;
      alertStartTime = currentTime;

      digitalWrite(buzzerPin, HIGH);
      digitalWrite(fanPin, HIGH);
      regulatorServo.write(90);  // Close valve

      sendTelegramWithRetry("🚨 LPG GAS LEAK DETECTED!\nAuto shut-off regulator activated.");
    }
  } else {
    if (gasAboveThreshold) {
      gasAboveThreshold = false;

      digitalWrite(buzzerPin, LOW);
      digitalWrite(fanPin, LOW);
      regulatorServo.write(0);  // Open valve

      unsigned long duration = (currentTime - alertStartTime) / 1000;
      String timeString = formatDuration(duration);

      sendTelegramWithRetry("✅ Gas level normalized.\nLeak lasted: " + timeString);
    }
  }
}

// ==== Telegram Send with Retry to Both Chats ====
void sendTelegramWithRetry(String message) {
  const int maxTries = 3;
  for (int i = 0; i < maxTries; i++) {
    bool sent1 = sendTelegram(message, botToken1, chatID1);
    bool sent2 = sendTelegram(message, botToken2, chatID2);
    if (sent1 && sent2) return;
    Serial.println("❌ Telegram failed, retrying...");
    delay(2000);
  }
  Serial.println("❌ Failed to send Telegram message.");
}

// ==== Send Telegram to single Bot and Chat ====
bool sendTelegram(String message, String botToken, String chatID) {
  WiFiClientSecure client;
  client.setInsecure();

  String url = "https://api.telegram.org/bot" + botToken +
               "/sendMessage?chat_id=" + chatID +
               "&text=" + urlencode(message) +
               "&parse_mode=Markdown";

  Serial.print("📤 Sending Telegram alert to chat ID: ");
  Serial.println(chatID);

  if (!client.connect("api.telegram.org", 443)) {
    Serial.println("❌ Connection failed");
    return false;
  }

  client.println("GET " + url + " HTTP/1.1");
  client.println("Host: api.telegram.org");
  client.println("Connection: close");
  client.println();

  while (client.connected()) {
    if (client.readStringUntil('\n') == "\r") break;
  }

  Serial.println("✅ Telegram alert sent to " + chatID);
  return true;
}

// ==== Format time (HH:MM:SS) ====
String formatDuration(unsigned long seconds) {
  unsigned long hrs = seconds / 3600;
  unsigned long mins = (seconds % 3600) / 60;
  unsigned long secs = seconds % 60;
  char buf[9];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", hrs, mins, secs);
  return String(buf);
}

// ==== URL Encode for Telegram ====
String urlencode(String str) {
  String encoded = "";
  for (int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (isalnum(c)) {
      encoded += c;
    } else {
      char code0 = ((c >> 4) & 0xF) + '0';
      char code1 = (c & 0xF) + '0';
      if (code0 > '9') code0 += 7;
      if (code1 > '9') code1 += 7;
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
  }
  return encoded;
}