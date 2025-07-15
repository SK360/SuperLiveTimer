#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <Wire.h>
#include "HT_SSD1306Wire.h"
#include <Bounce2.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// === Display ===
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// === Constants ===
constexpr const char* FIRMWARE_VERSION = "v1.0";
constexpr uint8_t BUTTON_PIN = 0;
constexpr uint8_t WIFI_LED_PIN = 35;
constexpr unsigned long HOLD_TIME = 5000; // 5s
constexpr uint32_t RF_FREQUENCY = 915000000; // Hz
constexpr int TX_OUTPUT_POWER = 21;
constexpr uint32_t RX_TIMEOUT_VALUE = 1000;
constexpr int LORA_BANDWIDTH_CFG = 0;          // 0 = 125 kHz
constexpr int LORA_SPREADING_FACTOR_CFG = 7;
constexpr int LORA_CODING_RATE_CFG = 1;
constexpr int LORA_PREAMBLE_LEN_CFG = 8;
constexpr bool LORA_FIX_LEN_CFG = false;
constexpr bool LORA_CRC_ENABLED = true;
constexpr int LORA_FREQ_HOP_ON_CFG = 0;
constexpr int LORA_HOP_PERIOD_CFG = 0;
constexpr bool LORA_IQ_INVERTED_CFG = false;
constexpr uint32_t LORA_TX_TIMEOUT_CFG = 3000;
constexpr size_t BUFFER_SIZE = 160;
constexpr unsigned long TEST_SEND_PERIOD = 5000; // ms

// === Globals ===
char txpacket[BUFFER_SIZE];
bool lora_idle = true;

Bounce debouncer = Bounce();
Preferences preferences;
WebServer server(80);
static RadioEvents_t RadioEvents;

HardwareSerial TimerSerial(1);
static String timerBuffer = "";

void toggleMode();

enum Mode { MODE_APP, MODE_SERIAL, MODE_TEST, MODE_WIFI };
Mode currentMode = MODE_APP;

unsigned long buttonPressStart = 0;
bool holdHandled = false;

String magicWord;

const char* CarIDList[] = {
  "66EVX", "87EVX", "41EVX", "18CAMS", "127CAMS",
  "5CAMS", "83SS", "88ES", "49GST", "91XP", "9SSP", "77EST"
};
const int CarIDListSize = sizeof(CarIDList) / sizeof(CarIDList[0]);

static int sendStep = 0;
unsigned long lastTestSend = 0;

// === Utility ===
void VextON() { pinMode(Vext, OUTPUT); digitalWrite(Vext, LOW); }
void VextOFF() { pinMode(Vext, OUTPUT); digitalWrite(Vext, HIGH); }

void updateDisplay(const String& line1, const String& line2 = "") {
  display.clear();
  display.drawString(0, 0, line1);
  if (!line2.isEmpty()) display.drawString(0, 20, line2);
  display.display();
}

// === Mode UI ===
void showMode() {
  switch (currentMode) {
    case MODE_APP:    updateDisplay(F("Mode: App"), F("Waiting for App")); break;
    case MODE_SERIAL: updateDisplay(F("Mode: Serial"), F("Listening...")); break;
    case MODE_TEST:   updateDisplay(F("Mode: Test")); break;
    case MODE_WIFI:   updateDisplay(F("Mode: WiFi"), WiFi.softAPSSID()); break;
  }
}

// === Web Server Handlers ===
void handleRoot() {
  String page = R"rawliteral(
<!DOCTYPE html><html><head><title>SLT Sender Config</title>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<style>
body { font-family: sans-serif; max-width: 500px; margin: auto; padding: 20px; background: #f4f4f4; color: #333; }
h2 { text-align: center; }
form { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }
label { display: block; margin-top: 15px; font-weight: bold; }
input[type='text'], input[type='submit'] { width: 100%; padding: 10px; margin-top: 5px; border-radius: 5px; border: 1px solid #ccc; }
input[type='submit'] { background: #2196f3; color: white; border: none; margin-top: 20px; cursor: pointer; }
input[type='submit']:hover { background: #1976d2; }
</style>
</head><body>
<h2>SLT Sender Config</h2>
<form action='/set' method='POST'>
<label for='magic'>Magic Word</label>
<input type='text' id='magic' name='magic' value=')rawliteral";

  page += magicWord;
  page += R"rawliteral('>
<input type='submit' value='Save'>
</form>
<hr>
<p>SLT Sender Config Page</p>
<small>Firmware )rawliteral";

  page += FIRMWARE_VERSION;
  page += R"rawliteral(</small>
</body></html>
)rawliteral";

  server.send(200, "text/html", page);
}

void handleSetMagic() {
  if (server.hasArg("magic")) {
    magicWord = server.arg("magic");
    preferences.begin("settings", false);
    preferences.putString("magicWord", magicWord);
    preferences.end();
    server.sendHeader("Location", "/");
    server.send(303);
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

// === WiFi Mode ===
void startWiFi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("SLT-Sender", "12345678");
  server.on("/", handleRoot);
  server.on("/set", HTTP_POST, handleSetMagic);
  server.begin();
  digitalWrite(WIFI_LED_PIN, HIGH);
  currentMode = MODE_WIFI;
  showMode();
}

void stopWiFi() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  digitalWrite(WIFI_LED_PIN, LOW);
}

// === Setup and Loop ===
void setup() {
  Serial.begin(9600);
  TimerSerial.begin(9600, SERIAL_8N1, 45, 46);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  VextON(); delay(100);

  display.init();
  display.setFont(ArialMT_Plain_16);

  pinMode(WIFI_LED_PIN, OUTPUT);
  digitalWrite(WIFI_LED_PIN, LOW);

  debouncer.attach(BUTTON_PIN, INPUT_PULLUP);
  debouncer.interval(25);

  RadioEvents.TxDone = []() {
    Serial.println(F("TX done......"));
    display.drawString(0, 40, F("Sent"));
    display.display();
    lora_idle = true;
  };

  RadioEvents.TxTimeout = []() {
    Radio.Sleep();
    Serial.println(F("TX Timeout......"));
    lora_idle = true;
  };

  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH_CFG,
                    LORA_SPREADING_FACTOR_CFG, LORA_CODING_RATE_CFG, LORA_PREAMBLE_LEN_CFG,
                    LORA_FIX_LEN_CFG, LORA_CRC_ENABLED, LORA_FREQ_HOP_ON_CFG, LORA_HOP_PERIOD_CFG,
                    LORA_IQ_INVERTED_CFG, LORA_TX_TIMEOUT_CFG);

  preferences.begin("settings", true);
  magicWord = preferences.getString("magicWord", "NHSCC");
  preferences.end();

  showMode();
  randomSeed(esp_random());
}

void loop() {
  debouncer.update();
  if (debouncer.fell()) {
    buttonPressStart = millis();
    holdHandled = false;
  } else if (debouncer.read() == LOW && !holdHandled && millis() - buttonPressStart >= HOLD_TIME) {
    startWiFi();
    holdHandled = true;
  } else if (debouncer.rose()) {
    if (!holdHandled) {
      toggleMode();
    }
    buttonPressStart = 0;
    holdHandled = false;
  }

  if (currentMode == MODE_WIFI) server.handleClient();

  if (currentMode == MODE_APP && lora_idle && Serial.available()) {
    String inString = Serial.readStringUntil('\n');
    inString.trim();

    if (inString == "PING") {
      Serial.println(F("PONG"));
      updateDisplay(F("Mode: App"), F("App Connected"));
      return;
    }

    int idx[6], pos = -1;
    for (int i = 0; i < 6; ++i) {
      idx[i] = inString.indexOf(',', pos + 1);
      if (idx[i] == -1 && i < 5) return;
      pos = idx[i];
    }

    String carID = inString.substring(0, idx[0]);
    float finishTime = inString.substring(idx[0] + 1, idx[1]).toFloat();
    int ftd = inString.substring(idx[1] + 1, idx[2]).toInt();
    int pb = inString.substring(idx[2] + 1, idx[3]).toInt();
    int oc = inString.substring(idx[3] + 1, idx[4]).toInt();
    int cones = inString.substring(idx[4] + 1).toInt();

    snprintf(txpacket, BUFFER_SIZE, "%s,2,%.3f,%d,%d,%d,0,0,%d,%s",
             magicWord.c_str(), finishTime, pb, ftd, oc, cones, carID.c_str());

    Serial.printf("\r\nsending packet \"%s\" , length %d\r\n", txpacket, strlen(txpacket));
    updateDisplay(String(finishTime, 3));
    Radio.Send((uint8_t *)txpacket, strlen(txpacket));
    lora_idle = false;
  }

  if (currentMode == MODE_SERIAL && lora_idle && TimerSerial.available()) {
    while (TimerSerial.available()) {
      char c = TimerSerial.read();
      if (c == '\n' || c == '\r') {
        if (timerBuffer.length() > 0) {
          String input = timerBuffer;
          timerBuffer = "";
          input.trim();

          String digits = "";
          for (char ch : input) {
            if (isdigit(ch)) digits += ch;
          }

          if (digits.length() == 6 && digits != "000000") {
            String reversed = "";
            for (int i = 5; i >= 0; --i) reversed += digits[i];

            float time = reversed.toFloat() / 1000.0;
            String timeStr = String(time, 3);
            if (timeStr.startsWith("0")) timeStr = timeStr.substring(1);

            updateDisplay("Time:", timeStr);

            snprintf(txpacket, BUFFER_SIZE, "%s,2,%.3f,0,0,0,0,0,0,00TIME",
                     magicWord.c_str(), time);

            Serial.printf("\r\nsending packet \"%s\" , length %d\r\n", txpacket, strlen(txpacket));
            Radio.Send((uint8_t *)txpacket, strlen(txpacket));
            lora_idle = false;
          }
        }
      } else {
        timerBuffer += c;
      }
    }
  }

  if (currentMode == MODE_TEST && lora_idle && millis() - lastTestSend >= TEST_SEND_PERIOD) {
    const char* CarID = CarIDList[random(0, CarIDListSize)];
    bool pb = false, ftd = false, oc = false, dnf = false, rerun = false;
    int cones = 0;

    switch (sendStep++) {
      case 1: pb = true; break;
      case 2: ftd = true; break;
      case 3: oc = true; break;
      case 4: dnf = true; break;
      case 5: rerun = true; break;
      case 6: cones = 2; break;
    }
    if (sendStep > 6) sendStep = 0;

    float ft = random(20000, 40001) / 1000.0;
    snprintf(txpacket, BUFFER_SIZE, "%s,2,%.3f,%d,%d,%d,%d,%d,%d,%s",
             magicWord.c_str(), ft, pb, ftd, oc, dnf, rerun, cones, CarID);

    Serial.printf("\r\nsending packet \"%s\" , length %d\r\n", txpacket, strlen(txpacket));
    updateDisplay(F("Mode: Test"), String(ft, 3));
    Radio.Send((uint8_t *)txpacket, strlen(txpacket));
    lora_idle = false;
    lastTestSend = millis();
  }

  Radio.IrqProcess();
}

void toggleMode() {
  if (currentMode == MODE_WIFI) stopWiFi();
  currentMode = (Mode)((currentMode + 1) % 3);
  showMode();
  Serial.printf("Mode switched to %s\n", currentMode == MODE_APP ? "App" :
                currentMode == MODE_SERIAL ? "Serial" : "Test");
}
