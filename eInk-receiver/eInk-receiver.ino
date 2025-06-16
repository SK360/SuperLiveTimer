#include <heltec-eink-modules.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include "LoRaWan_APP.h"

#define RF_FREQUENCY 915000000
#define TX_OUTPUT_POWER 14
#define LORA_BANDWIDTH 0
#define LORA_SPREADING_FACTOR 7
#define LORA_CODINGRATE 1
#define LORA_PREAMBLE_LENGTH 8
#define LORA_SYMBOL_TIMEOUT 0
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false
#define RX_TIMEOUT_VALUE 1000
#define BUFFER_SIZE 80

char rxpacket[BUFFER_SIZE];

EInkDisplay_VisionMasterE290 display;
#include "Fonts/FreeSansBold24pt7b.h"
#include "Fonts/FreeSansBold18pt7b.h"
#include "Fonts/FreeSansBold9pt7b.h"

#define BUTTON_PIN 21
WebServer server(80);
Preferences prefs;

unsigned long buttonPressStart = 0;
bool waitingForRelease = false;
bool wifiStarted = false;
int filterMode = 1; // 0 = off, 1 = by CarID, 2 = by class
bool lastButtonState = HIGH;
String filterValue = "None";

static RadioEvents_t RadioEvents;
int16_t rssi;
int8_t snr;

void drawCenteredText(const char* text, int y, const GFXfont* font) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setFont(font);
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int16_t x = (display.width() - w) / 2;
  display.setCursor(x, y);
  display.println(text);
}

void showBootScreen() {
  display.clearMemory();
  drawCenteredText("SuperLiveTimer", 50, &FreeSansBold18pt7b);

  String line = "Filter: ";
  if (filterMode == 0 || filterValue.equalsIgnoreCase("None")) {
    line += "None";
  } else if (filterMode == 1) {
    line += filterValue;
  } else if (filterMode == 2) {
    int splitPos = 0;
    while (splitPos < filterValue.length() && isDigit(filterValue[splitPos])) {
      splitPos++;
    }
    String classOnly = filterValue.substring(splitPos);
    line += classOnly;
  }

  drawCenteredText(line.c_str(), 100, &FreeSansBold9pt7b);
  display.update();
}

void handleRoot() {
  String html = "<html><body><h1>SuperLiveTimer Config</h1>"
                "<form method='POST' action='/set'>"
                "CarID/Class: <input type='text' name='carid' value='" + filterValue + "'><br><br>"
                "<input type='submit' value='Save'>"
                "</form></body></html>";
  server.send(200, "text/html", html);
}

void handleSet() {
  if (server.hasArg("carid")) {
    filterValue = server.arg("carid");
    prefs.putString("filter", filterValue);
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void startWiFiAP() {
  WiFi.softAP("VisionMasterAP", "password123");
  IPAddress ip = WiFi.softAPIP();
  server.on("/", handleRoot);
  server.on("/set", HTTP_POST, handleSet);
  server.begin();

  display.clearMemory();
  drawCenteredText("WiFi Enabled", 40, &FreeSansBold24pt7b);
  drawCenteredText(ip.toString().c_str(), 100, &FreeSansBold9pt7b);
  display.update();

  wifiStarted = true;
}

void stopWiFiAP() {
  server.stop();
  WiFi.softAPdisconnect(true);
  display.clearMemory();
  drawCenteredText("WiFi Disabled", 50, &FreeSansBold24pt7b);
  display.update();
  wifiStarted = false;
}

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi_, int8_t snr_) {
  rssi = rssi_;
  snr = snr_;
  memcpy(rxpacket, payload, size);
  rxpacket[size] = '\0';

  char* data = rxpacket;
  char* firstComma = strchr(data, ',');
  if (firstComma != nullptr) {
    data = firstComma + 1;
  }

  char* tokens[7] = { nullptr };
  int i = 0;
  char* token = strtok(data, ",");
  while (token != nullptr && i < 7) {
    tokens[i++] = token;
    token = strtok(nullptr, ",");
  }

  if (i < 6) return;

  const char* carId = tokens[0];
  const char* elapsedTime = tokens[1];
  const char* ftd = tokens[2];
  const char* personalBest = tokens[3];
  const char* offCourse = tokens[4];
  const char* cones = tokens[5];

  String carStr = String(carId);
  int splitPos = 0;
  while (splitPos < carStr.length() && isDigit(carStr[splitPos])) {
    splitPos++;
  }
  String carClass = carStr.substring(splitPos);

  String classFilter = filterValue;
  if (filterMode == 2) {
    int filterSplit = 0;
    while (filterSplit < classFilter.length() && isDigit(classFilter[filterSplit])) {
      filterSplit++;
    }
    classFilter = classFilter.substring(filterSplit);
  }

  if ((filterMode == 1 && !filterValue.equalsIgnoreCase(carStr)) ||
      (filterMode == 2 && !classFilter.equalsIgnoreCase(carClass))) return;

  const char* status = "";
  if (strcmp(offCourse, "1") == 0) status = "OC";
  else if (strcmp(ftd, "1") == 0) status = "FTD";
  else if (strcmp(personalBest, "1") == 0) status = "PB";

  String timeLine = String(elapsedTime);
  int coneCount = atoi(cones);
  if (coneCount > 0) {
    timeLine += " +" + String(coneCount);
  }
  if (strlen(status) > 0) {
    timeLine += " (" + String(status) + ")";
  }

  display.clearMemory();
  // Add space between number and class
  String carDisplay = "";
  for (int i = 0; i < carStr.length(); ++i) {
    if (isDigit(carStr[i])) carDisplay += carStr[i];
    else {
      carDisplay += " ";
      carDisplay += carStr.substring(i);
      break;
    }
  }
  drawCenteredText(carDisplay.c_str(), 50, &FreeSansBold24pt7b);

  const GFXfont* timeFont = &FreeSansBold24pt7b;
  int16_t x1, y1;
  uint16_t w, h;
  display.setFont(timeFont);
  display.getTextBounds(timeLine.c_str(), 0, 0, &x1, &y1, &w, &h);
  if (w > display.width()) timeFont = &FreeSansBold18pt7b;

  drawCenteredText(timeLine.c_str(), 110, timeFont);
  display.update();
}

void VextON(void) {
  pinMode(18, OUTPUT);
  digitalWrite(18, HIGH);
}

void setup() {
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  Serial.begin(115115);
  VextON();
  delay(100);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  display.landscape();
  prefs.begin("config", false);
  filterValue = prefs.getString("filter", "None");
  filterMode = prefs.getInt("filterMode", 1);
  showBootScreen();

  RadioEvents.RxDone = OnRxDone;
  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                    LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                    LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                    0, true, 0, 0, LORA_IQ_INVERSION_ON, true);
  Radio.Rx(0);
}

void loop() {
  bool currentButtonState = digitalRead(BUTTON_PIN);

  if (lastButtonState == LOW && currentButtonState == HIGH) {
    filterMode = (filterMode + 1) % 3;  // Cycle through 0 -> 1 -> 2 -> 0
    prefs.putInt("filterMode", filterMode);
    showBootScreen();
    delay(300);
  }

  lastButtonState = currentButtonState;

  if (wifiStarted) {
    server.handleClient();
  }

  Radio.IrqProcess();
  delay(10);
}
