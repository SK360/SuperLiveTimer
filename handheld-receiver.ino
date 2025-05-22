#include <Wire.h>
#include "LoRaWan_APP.h"
#include "Arduino.h"
#include "HT_SSD1306Wire.h"
#include "esp_sleep.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

#define WIFI_LED_PIN 35

static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// LoRa
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

String magicWord = "NHSCC";
String myCarID = "";
String myClass = "";

enum FilterMode {
  FILTER_ALL,
  FILTER_MY_CAR,
  FILTER_MY_CLASS
};
FilterMode filterMode = FILTER_ALL;

char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];

static RadioEvents_t RadioEvents;

int16_t txNumber;
int16_t rssi, rxSize;
int8_t snr;
int packetCount = 0;

bool lora_idle = true;

#define USER_BUTTON_PIN 0
const unsigned long WIFI_HOLD_DURATION = 5000;
const unsigned long SLEEP_HOLD_DURATION = 10000;
unsigned long buttonPressStartTime = 0;
bool buttonHeld = false;
bool wifiArmed = false;
bool lastButtonState = HIGH;
bool wifiEnabled = false;
bool startupIgnoreActive = true;
bool diagnosticsMode = false;

unsigned long startupTime = 0;
unsigned long startupIgnoreTime = 5000;

const char* ap_ssid = "SuperLiveTimer-Setup";
const char* ap_password = "12345678";
WebServer server(80);

Preferences prefs;

// Helpers
String formatCarID(const char* carID) {
    String id(carID);
    for (unsigned int i = 0; i < id.length(); i++) {
        if (isalpha(id[i])) {
            id = id.substring(0, i) + " " + id.substring(i);
            break;
        }
    }
    return id;
}

String extractClass(const String& carid) {
    for (unsigned int i = 0; i < carid.length(); i++) {
        if (isalpha(carid[i])) {
            return carid.substring(i);
        }
    }
    return "";
}

void VextON(void) { pinMode(Vext, OUTPUT); digitalWrite(Vext, LOW);}
void VextOFF(void) { pinMode(Vext, OUTPUT); digitalWrite(Vext, HIGH);}

void goToSleep() {
    display.clear();
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 20, "Sleeping...");
    display.display();
    delay(1000);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BUTTON_PIN, 0);
    VextOFF();
    esp_deep_sleep_start();
}

// ---- Web Interface ----

String htmlPage() {
  String page = "<!DOCTYPE HTML><html><head><title>NHSCC Super Live Timer Config</title><meta name='viewport' content='width=device-width, initial-scale=1'></head><body>";
  page += "<h2>NHSCC Super Live Timer Config</h2>";
  page += "<form action='/save' method='POST'>";
  page += "My Car ID (for filtering):<br>";
  page += "<input type='text' name='mycarid' value='" + myCarID + "'><br><br>";
  page += "Magic Word:<br>";
  page += "<input type='text' name='magicword' value='" + magicWord + "'><br><br>";
  page += "<input type='checkbox' id='diagnostics' name='diagnostics' value='on'";
  if (diagnosticsMode) page += " checked";
  page += "> <label for='diagnostics'>Enable Diagnostics Mode</label><br><br>";
  page += "<input type='submit' value='Save'>";
  page += "</form>";
  page += "<p>Current filter: <b>";
  if (filterMode == FILTER_MY_CAR && myCarID.length() > 0)
    page += myCarID;
  else if (filterMode == FILTER_MY_CLASS && myClass.length() > 0)
    page += myClass;
  else
    page += "None";
  page += "</b></p>";
  page += "<p>Current Magic Word: <b>";
  page += magicWord;
  page += "</b></p>";
  page += "<p>Diagnostics Mode: <b>";
  page += (diagnosticsMode ? "Enabled" : "Disabled");
  page += "</b></p>";
  // No "Show All Cars" button here!
  page += "</body></html>";
  return page;
}

void handleRoot() {
  server.send(200, "text/html", htmlPage());
}

void handleSave() {
  if (server.hasArg("mycarid")) {
    myCarID = server.arg("mycarid");
    myCarID.trim();
    myClass = extractClass(myCarID);
    prefs.putString("mycarid", myCarID);
    prefs.putString("myclass", myClass);
  }
  if (server.hasArg("magicword")) {
    magicWord = server.arg("magicword");
    magicWord.trim();
    if (magicWord.length() == 0) magicWord = "NHSCC";
    prefs.putString("magicword", magicWord);
  }
  diagnosticsMode = server.hasArg("diagnostics");
  prefs.putBool("diagnostics", diagnosticsMode);

  // Always reset to show all cars on config change
  filterMode = FILTER_ALL;
  prefs.putUChar("filtermode", filterMode);

  showStartupOLED();

  server.sendHeader("Location", "/");
  server.send(303);
}

void startWiFiAP() {
  WiFi.softAP(ap_ssid, ap_password);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  wifiEnabled = true;
  digitalWrite(WIFI_LED_PIN, HIGH);
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 20, "WiFi AP Enabled");
  display.display();
  delay(1000);
}

void stopWiFiAP() {
  server.stop();
  WiFi.softAPdisconnect(true);
  wifiEnabled = false;
  digitalWrite(WIFI_LED_PIN, LOW);
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 20, "WiFi AP Disabled");
  display.display();
  delay(1000);
}

// OLED startup status -- uses your preferred font and Y=40
void showStartupOLED() {
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 0, "FinishTime");
  display.drawString(0, 20, "Waiting for Data");
  display.setFont(ArialMT_Plain_16);
  if (filterMode == FILTER_MY_CAR && myCarID.length() > 0) {
    display.drawString(0, 40, "Filter: " + myCarID);
  } else if (filterMode == FILTER_MY_CLASS && myClass.length() > 0) {
    display.drawString(0, 40, "Filter: " + myClass);
  } else {
    display.drawString(0, 40, "Filter: None");
  }
  display.display();
}

// --- LoRa Receive Handler ---
void OnRxDone(uint8_t *payload, uint16_t size, int16_t packetRssi, int8_t packetSnr) {
    memcpy(rxpacket, payload, size);
    rxpacket[size] = '\0';

    rssi = packetRssi;
    snr = packetSnr;
    packetCount++;

    char* token = strtok(rxpacket, ",");
    if (token == NULL || strcmp(token, magicWord.c_str()) != 0) {
        lora_idle = true;
        return;
    }

    char *carID = strtok(NULL, ",");
    char *finishTime = strtok(NULL, ",");
    char *ftd = strtok(NULL, ",");
    char *personalBest = strtok(NULL, ",");
    char *offCourse = strtok(NULL, ",");
    char *cones = strtok(NULL, ",");

    bool showThisPacket = true;
    String rxCar = carID ? String(carID) : "";
    rxCar.trim();

    if (filterMode == FILTER_MY_CAR && myCarID.length() > 0) {
      showThisPacket = rxCar.equalsIgnoreCase(myCarID);
    } else if (filterMode == FILTER_MY_CLASS && myClass.length() > 0) {
      String rxClass = extractClass(rxCar);
      showThisPacket = (rxClass.equalsIgnoreCase(myClass));
    }

    if (carID && finishTime && ftd && personalBest && offCourse && cones && showThisPacket) {
        Serial.print(carID); Serial.print(",");
        Serial.print(finishTime); Serial.print(",");
        Serial.print(ftd); Serial.print(",");
        Serial.print(personalBest); Serial.print(",");
        Serial.print(offCourse); Serial.print(",");
        Serial.println(cones);

        display.clear();

        if (diagnosticsMode) {
            display.setFont(ArialMT_Plain_16);
            display.drawString(0, 0, "Diagnostics Mode");
            display.drawString(0, 20, "RSSI: " + String(rssi) + " dBm");
            display.drawString(0, 36, "SNR: " + String(snr) + " dB");
            display.setFont(ArialMT_Plain_10);
            display.drawString(0, 52, "Size: " + String(size) + "  Packets: " + String(packetCount));
            display.display();
            lora_idle = true;
            return;
        }

        display.setFont(ArialMT_Plain_24);
        if (carID) display.drawString(0, 0, formatCarID(carID));

        if (finishTime) {
            String finishDisplay = String(finishTime);
            if (cones && atoi(cones) != 0) {
                finishDisplay += " +" + String(atoi(cones));
            }
            display.drawString(0, 20, finishDisplay);
        }

        if (offCourse && atoi(offCourse)) {
            display.drawString(0, 40, "Off Course");
            display.display();
        } else if (ftd && atoi(ftd)) {
            display.setFont(ArialMT_Plain_24);
            int textWidth = display.getStringWidth("FTD!");
            int maxX = 128 - textWidth;
            for (int x = 0; x <= maxX; x += 6) {
                display.clear();
                if (carID) display.drawString(0, 0, formatCarID(carID));
                if (finishTime) {
                    String finishDisplay = String(finishTime);
                    if (cones && atoi(cones) != 0) {
                        finishDisplay += " +" + String(atoi(cones));
                    }
                    display.drawString(0, 20, finishDisplay);
                }
                display.drawString(x, 40, "FTD!");
                display.display();
                delay(60);
            }
            for (int x = maxX; x >= 0; x -= 6) {
                display.clear();
                if (carID) display.drawString(0, 0, formatCarID(carID));
                if (finishTime) {
                    String finishDisplay = String(finishTime);
                    if (cones && atoi(cones) != 0) {
                        finishDisplay += " +" + String(atoi(cones));
                    }
                    display.drawString(0, 20, finishDisplay);
                }
                display.drawString(x, 40, "FTD!");
                display.display();
                delay(60);
            }
        } else if (personalBest && atoi(personalBest)) {
            display.drawString(0, 40, "PB");
            display.display();
        } else {
            display.display();
        }
    }

    lora_idle = true;
}

void setup() {
    Serial.begin(115200);
    Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
    pinMode(USER_BUTTON_PIN, INPUT_PULLUP);

    startupTime = millis();
    txNumber = 0;
    rssi = 0;
    VextON();
    delay(100);
    display.init();
    pinMode(WIFI_LED_PIN, OUTPUT);
    digitalWrite(WIFI_LED_PIN, LOW);

    // Load settings
    prefs.begin("supertimer", false);
    myCarID = prefs.getString("mycarid", "");
    myClass = prefs.getString("myclass", extractClass(myCarID));
    magicWord = prefs.getString("magicword", "NHSCC");
    if (magicWord.length() == 0) magicWord = "NHSCC";
    diagnosticsMode = prefs.getBool("diagnostics", false);
    filterMode = (FilterMode)prefs.getUChar("filtermode", 0);

    showStartupOLED();

    RadioEvents.RxDone = OnRxDone;
    Radio.Init(&RadioEvents);
    Radio.SetChannel(RF_FREQUENCY);
    Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                      LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                      LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                      0, true, 0, 0, LORA_IQ_INVERSION_ON, true);

    wifiEnabled = false;
    startupIgnoreActive = true;
}

void loop() {
    if (wifiEnabled) server.handleClient();

    unsigned long now = millis();

    if (startupIgnoreActive) {
        if (now - startupTime < startupIgnoreTime) {
            if (lora_idle) {
                lora_idle = false;
                Radio.Rx(0);
            }
            Radio.IrqProcess();
            return;
        } else {
            lastButtonState = digitalRead(USER_BUTTON_PIN);
            startupIgnoreActive = false;
            if (lora_idle) {
                lora_idle = false;
                Radio.Rx(0);
            }
            Radio.IrqProcess();
            return;
        }
    }

    bool buttonPressed = digitalRead(USER_BUTTON_PIN) == LOW;

    if (lastButtonState != buttonPressed) {
        lastButtonState = buttonPressed;
        if (buttonPressed) {
            buttonPressStartTime = now;
            buttonHeld = false;
            wifiArmed = false;
        } else {
            unsigned long pressDuration = now - buttonPressStartTime;
            if (!buttonHeld && wifiArmed && pressDuration >= WIFI_HOLD_DURATION && pressDuration < SLEEP_HOLD_DURATION) {
                if (!wifiEnabled) {
                    startWiFiAP();
                } else {
                    stopWiFiAP();
                }
                wifiArmed = false;
            }
            // Released too soon: cycle filter mode (all/my car/my class)
            else if (!buttonHeld && pressDuration < WIFI_HOLD_DURATION) {
                filterMode = (FilterMode)((filterMode + 1) % 3);
                prefs.putUChar("filtermode", filterMode);
                showStartupOLED();
            }
        }
    }

    if (buttonPressed && !buttonHeld) {
        unsigned long heldTime = now - buttonPressStartTime;
        if (heldTime >= WIFI_HOLD_DURATION && heldTime < SLEEP_HOLD_DURATION && !wifiArmed) {
            wifiArmed = true;
            display.clear();
            display.setFont(ArialMT_Plain_16);
            if (!wifiEnabled) {
                display.drawString(0, 20, "Release to");
                display.drawString(0, 38, "enable wifi");
            } else {
                display.drawString(0, 20, "Release to");
                display.drawString(0, 38, "disable wifi");
            }
            display.display();
        }
        else if (heldTime >= SLEEP_HOLD_DURATION) {
            buttonHeld = true;
            goToSleep();
        }
    }

    if (lora_idle) {
        lora_idle = false;
        Radio.Rx(0);
    }

    Radio.IrqProcess();
}
