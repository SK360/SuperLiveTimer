#include <Wire.h>
#include "LoRaWan_APP.h"
#include "Arduino.h"
#include "HT_SSD1306Wire.h"
#include "esp_sleep.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h> // For persistent storage

#define WIFI_LED_PIN 35 // Heltec V3 onboard white LED is on GPIO 35

// --- OLED + LoRa ---
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// LoRa Configuration
#define RF_FREQUENCY                915000000 // Hz
#define TX_OUTPUT_POWER             14        // dBm
#define LORA_BANDWIDTH              0
#define LORA_SPREADING_FACTOR       7
#define LORA_CODINGRATE             1
#define LORA_PREAMBLE_LENGTH        8
#define LORA_SYMBOL_TIMEOUT         0
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON        false

#define RX_TIMEOUT_VALUE            1000
#define BUFFER_SIZE                 80

const char* MAGIC_WORD = "NHSCC";

char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];

static RadioEvents_t RadioEvents;

int16_t txNumber;
int16_t rssi, rxSize;
int8_t snr;
int packetCount = 0;

bool lora_idle = true;

// Button and Mode Management
#define USER_BUTTON_PIN 0
const unsigned long WIFI_HOLD_DURATION = 5000;     // 5 seconds
const unsigned long SLEEP_HOLD_DURATION = 10000;   // 10 seconds
unsigned long buttonPressStartTime = 0;
bool buttonHeld = false;
bool wifiArmed = false;
bool lastButtonState = HIGH;
bool wifiEnabled = false;  // WiFi/AP is disabled by default
bool startupIgnoreActive = true; // Track if in the startup ignore period

enum DisplayMode { RACE_MODE, DIAGNOSTICS_MODE };
DisplayMode currentMode = RACE_MODE;

// Ignore button state after boot
unsigned long startupTime = 0;
unsigned long startupIgnoreTime = 5000; // 5 seconds

// --- WiFi + Web ---
const char* ap_ssid = "SuperLiveTimer-Setup";
const char* ap_password = "12345678";
WebServer server(80);

// Persistent storage for filter
Preferences prefs;

// Filter for Car ID (empty = show all)
String filteredCarID = "";

// Helper: Format Car ID for OLED
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

// OLED Power Control
void VextON(void) {
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW);
}

void VextOFF(void) {
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, HIGH);
}

// Deep Sleep
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
  page += "Car ID filter (show only results for this ID):<br>";
  page += "<input type='text' name='carid' value='" + filteredCarID + "'><br>";
  page += "<input type='submit' value='Save'>";
  page += "</form>";
  page += "<p>Current filter: <b>";
  page += (filteredCarID.length() ? filteredCarID : "None");
  page += "</b></p>";
  page += "<form action='/clear' method='POST'><input type='submit' value='Show All Cars'></form>";
  page += "</body></html>";
  return page;
}

void handleRoot() {
  server.send(200, "text/html", htmlPage());
}

void handleSave() {
  if (server.hasArg("carid")) {
    filteredCarID = server.arg("carid");
    filteredCarID.trim(); // Remove whitespace
    Serial.println("Set CarID filter to: " + filteredCarID);
    prefs.putString("carid", filteredCarID); // Save to NVS!
    server.sendHeader("Location", "/");
    server.send(303); // Redirect back to root
  } else {
    server.send(400, "text/html", "Missing carid!");
  }
}

void handleClear() {
  filteredCarID = "";
  prefs.putString("carid", ""); // Clear in NVS!
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
  server.on("/clear", HTTP_POST, handleClear);
  server.begin();
  wifiEnabled = true;
  digitalWrite(WIFI_LED_PIN, HIGH); // LED ON when WiFi ON
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
  digitalWrite(WIFI_LED_PIN, LOW); // LED OFF when WiFi OFF
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 20, "WiFi AP Disabled");
  display.display();
  delay(1000);
}

// --- LoRa Receive Handler ---
void OnRxDone(uint8_t *payload, uint16_t size, int16_t packetRssi, int8_t packetSnr) {
    memcpy(rxpacket, payload, size);
    rxpacket[size] = '\0';

    rssi = packetRssi;
    snr = packetSnr;
    packetCount++;

    char* token = strtok(rxpacket, ",");
    if (token == NULL || strcmp(token, MAGIC_WORD) != 0) {
        lora_idle = true;
        return;
    }

    char *carID = strtok(NULL, ",");
    char *finishTime = strtok(NULL, ",");
    char *ftd = strtok(NULL, ",");
    char *personalBest = strtok(NULL, ",");
    char *offCourse = strtok(NULL, ",");
    char *cones = strtok(NULL, ",");

    // Filter by Car ID
    bool showThisPacket = true;
    if (filteredCarID.length() > 0 && carID) {
      String rxCar = String(carID);
      rxCar.trim();
      showThisPacket = rxCar.equalsIgnoreCase(filteredCarID);
    }

    if (carID && finishTime && ftd && personalBest && offCourse && cones && showThisPacket) {
        Serial.print(carID); Serial.print(",");
        Serial.print(finishTime); Serial.print(",");
        Serial.print(ftd); Serial.print(",");
        Serial.print(personalBest); Serial.print(",");
        Serial.print(offCourse); Serial.print(",");
        Serial.println(cones);

        display.clear();

        if (currentMode == DIAGNOSTICS_MODE) {
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
            // FTD animation: scroll right and back to left
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
    } else {
        if (carID && !showThisPacket) {
            Serial.println("Filtered out packet for CarID: " + String(carID));
        }
    }

    lora_idle = true;
}

// --- Setup ---
void setup() {
    Serial.begin(115200);
    currentMode = RACE_MODE;

    Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
    pinMode(USER_BUTTON_PIN, INPUT_PULLUP);

    startupTime = millis();

    txNumber = 0;
    rssi = 0;
    VextON();
    delay(100);
    display.init();
    display.setFont(ArialMT_Plain_16);

    // --- Load CarID Filter from NVS ---
    prefs.begin("supertimer", false); // Open Preferences namespace
    filteredCarID = prefs.getString("carid", "");

    display.drawString(0, 0, "FinishTime");
    display.drawString(0, 20, "Waiting for Data");

    if (filteredCarID.length() > 0) {
      display.setFont(ArialMT_Plain_10);
      display.drawString(0, 44, "Filter: " + filteredCarID);
    }

    display.display();

    pinMode(WIFI_LED_PIN, OUTPUT);      // White LED as output
    digitalWrite(WIFI_LED_PIN, LOW);    // Ensure LED is OFF at boot

    RadioEvents.RxDone = OnRxDone;
    Radio.Init(&RadioEvents);
    Radio.SetChannel(RF_FREQUENCY);
    Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                      LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                      LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                      0, true, 0, 0, LORA_IQ_INVERSION_ON, true);

    wifiEnabled = false;
    startupIgnoreActive = true; // Activate ignore at boot
}

// --- Main Loop ---
void loop() {
    if (wifiEnabled) server.handleClient();

    unsigned long now = millis();

    // Handle startup ignore period
    if (startupIgnoreActive) {
        if (now - startupTime < startupIgnoreTime) {
            if (lora_idle) {
                lora_idle = false;
                Radio.Rx(0);
            }
            Radio.IrqProcess();
            return;
        } else {
            // Just leaving the ignore period: sync the lastButtonState
            lastButtonState = digitalRead(USER_BUTTON_PIN);
            startupIgnoreActive = false;
            // Don't run any button logic this loop
            if (lora_idle) {
                lora_idle = false;
                Radio.Rx(0);
            }
            Radio.IrqProcess();
            return;
        }
    }

    // ---- Button and WiFi logic below only runs after 5 sec ----
    bool buttonPressed = digitalRead(USER_BUTTON_PIN) == LOW;

    if (lastButtonState != buttonPressed) {
        lastButtonState = buttonPressed;

        if (buttonPressed) {
            buttonPressStartTime = now;
            buttonHeld = false;
            wifiArmed = false;
        } else {
            unsigned long pressDuration = now - buttonPressStartTime;
            // Released during WiFi arm window
            if (!buttonHeld && wifiArmed && pressDuration >= WIFI_HOLD_DURATION && pressDuration < SLEEP_HOLD_DURATION) {
                if (!wifiEnabled) {
                    startWiFiAP();
                } else {
                    stopWiFiAP();
                }
                wifiArmed = false;
            }
            // Released too soon: treat as normal short press (toggle display mode)
            else if (!buttonHeld && pressDuration < WIFI_HOLD_DURATION) {
                currentMode = (currentMode == RACE_MODE) ? DIAGNOSTICS_MODE : RACE_MODE;
                display.clear();
                display.setFont(ArialMT_Plain_16);
                display.drawString(0, 20, currentMode == DIAGNOSTICS_MODE ? "Diagnostics On" : "Race Mode");
                display.display();
                delay(500);
            }
        }
    }

    if (buttonPressed && !buttonHeld) {
        unsigned long heldTime = now - buttonPressStartTime;
        // At 5 seconds: show message
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
        // At 10 seconds: go to sleep (WiFi state doesn't change)
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
