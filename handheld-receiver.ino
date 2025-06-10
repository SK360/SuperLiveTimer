#include <Wire.h>
#include "LoRaWan_APP.h"
#include "Arduino.h"
#include "HT_SSD1306Wire.h"
#include "esp_sleep.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// === Firmware Version ===
#define FIRMWARE_VERSION "v1.0.0"

// === Hardware Pins ===
#define USER_BUTTON_PIN 0
#define WIFI_LED_PIN 35

// === LoRaWAN ===
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

// === WiFi AP Config ===
const char* apSsid = "SuperLiveTimer-Setup";
const char* apPassword = "12345678";

// === Timings ===
const unsigned long wifiHoldDuration = 5000;
const unsigned long sleepHoldDuration = 10000;
const unsigned long startupIgnoreTime = 5000; // ms

// === Display ===
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// === Helper Functions (Must be defined before use in structs/classes) ===
String extractClass(const String& carId) {
    for (unsigned int i = 0; i < carId.length(); i++) {
        if (isalpha(carId[i])) {
            return carId.substring(i);
        }
    }
    return "";
}

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

// === Enums ===
enum class FilterMode : uint8_t {
    All = 0,
    MyCar = 1,
    MyClass = 2
};

// === Settings Struct ===
struct Settings {
    String carId;          ///< User's car ID (e.g. "123STS")
    String carClass;       ///< Derived from carId (e.g. "STS")
    String magicWord;      ///< Packet "magic word"
    bool diagnostics;      ///< Diagnostics mode on/off
    FilterMode filterMode; ///< All/MyCar/MyClass

    void load(Preferences& prefs) {
        carId = prefs.getString("mycarid", "");
        carClass = prefs.getString("myclass", extractClass(carId));
        magicWord = prefs.getString("magicword", "NHSCC");
        if (magicWord.length() == 0) magicWord = "NHSCC";
        diagnostics = prefs.getBool("diagnostics", false);
        filterMode = static_cast<FilterMode>(prefs.getUChar("filtermode", 0));
    }

    void save(Preferences& prefs) const {
        prefs.putString("mycarid", carId);
        prefs.putString("myclass", carClass);
        prefs.putString("magicword", magicWord);
        prefs.putBool("diagnostics", diagnostics);
        prefs.putUChar("filtermode", static_cast<uint8_t>(filterMode));
    }
};

// === Globals ===
Preferences prefs;
WebServer server(80);
Settings settings;
bool wifiEnabled = false;
bool loraIdle = true;
unsigned long startupTime = 0;
bool startupIgnoreActive = true;
unsigned long buttonPressStartTime = 0;
bool buttonHeld = false;
bool wifiArmed = false;
bool lastButtonState = HIGH;

char rxpacket[BUFFER_SIZE];
static RadioEvents_t RadioEvents;
int16_t rssi = 0;
int8_t snr = 0;
int packetCount = 0;

// === OLED Display Functions ===
void showStartupOLED() {
    display.clear();
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0, "SuperLiveTimer");
    display.drawString(0, 20, "Waiting for Data");
    display.setFont(ArialMT_Plain_16);
    if (settings.filterMode == FilterMode::MyCar && settings.carId.length() > 0) {
        display.drawString(0, 40, "Filter: " + settings.carId);
    } else if (settings.filterMode == FilterMode::MyClass && settings.carClass.length() > 0) {
        display.drawString(0, 40, "Filter: " + settings.carClass);
    } else {
        display.drawString(0, 40, "Filter: None");
    }
    display.display();
}

void showTempMessage(const String& line1, const String& line2) {
    display.clear();
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 20, line1);
    if (line2.length()) display.drawString(0, 38, line2);
    display.display();
}

// === Button, WiFi, and Power Management ===
void VextON() { pinMode(Vext, OUTPUT); digitalWrite(Vext, LOW); }
void VextOFF() { pinMode(Vext, OUTPUT); digitalWrite(Vext, HIGH); }

void goToSleep() {
    showTempMessage("Sleeping...", "");
    delay(1000);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BUTTON_PIN, 0);
    VextOFF();
    esp_deep_sleep_start();
}

void startWiFiAP() {
    WiFi.softAP(apSsid, apPassword);
    server.begin();
    wifiEnabled = true;
    digitalWrite(WIFI_LED_PIN, HIGH);
    showTempMessage("WiFi AP Enabled", "");
    delay(1000);
}

void stopWiFiAP() {
    server.stop();
    WiFi.softAPdisconnect(true);
    wifiEnabled = false;
    digitalWrite(WIFI_LED_PIN, LOW);
    showTempMessage("WiFi AP Disabled", "");
    delay(1000);
}

// === Web Interface ===
String htmlPage() {
    String page = "<!DOCTYPE HTML><html><head><title>NHSCC Super Live Timer Config</title>";
    page += "<meta name='viewport' content='width=device-width, initial-scale=1'></head><body>";
    page += "<h2>NHSCC Super Live Timer Config</h2>";
    page += "<form action='/save' method='POST'>";
    page += "My Car ID (for filtering):<br>";
    page += "<input type='text' name='mycarid' value='" + settings.carId + "'><br><br>";
    page += "Magic Word:<br>";
    page += "<input type='text' name='magicword' value='" + settings.magicWord + "'><br><br>";
    page += "<input type='checkbox' id='diagnostics' name='diagnostics' value='on'";
    if (settings.diagnostics) page += " checked";
    page += "> <label for='diagnostics'>Enable Diagnostics Mode</label><br><br>";
    page += "<input type='submit' value='Save'>";
    page += "</form>";
    page += "<p>Current filter: <b>";
    if (settings.filterMode == FilterMode::MyCar && settings.carId.length() > 0)
        page += settings.carId;
    else if (settings.filterMode == FilterMode::MyClass && settings.carClass.length() > 0)
        page += settings.carClass;
    else
        page += "None";
    page += "</b></p>";
    page += "<p>Current Magic Word: <b>";
    page += settings.magicWord;
    page += "</b></p>";
    page += "<p>Diagnostics Mode: <b>";
    page += (settings.diagnostics ? "Enabled" : "Disabled");
    page += "</b></p>";
    page += "<hr>";
    page += "<div style='margin-bottom:6px;font-size:14px;'>";
    page += "SuperLiveTimer by <a href='mailto:matt.simmons@gmail.com'>Matt Simmons</a>";
    page += "</div>";
    page += "<small>Firmware: " FIRMWARE_VERSION "</small>";
    page += "</body></html>";
    return page;
}

void handleRoot() { server.send(200, "text/html", htmlPage()); }

void handleSave() {
    if (server.hasArg("mycarid")) {
        settings.carId = server.arg("mycarid");
        settings.carId.trim();
        settings.carClass = extractClass(settings.carId);
    }
    if (server.hasArg("magicword")) {
        settings.magicWord = server.arg("magicword");
        settings.magicWord.trim();
        if (settings.magicWord.length() == 0) settings.magicWord = "NHSCC";
    }
    settings.diagnostics = server.hasArg("diagnostics");
    settings.filterMode = FilterMode::All;
    settings.save(prefs);
    showStartupOLED();
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleWebRequests() {
    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
}

// === LoRa Receive Handler ===
void OnRxDone(uint8_t *payload, uint16_t size, int16_t packetRssi, int8_t packetSnr) {
    memcpy(rxpacket, payload, size);
    rxpacket[size] = '\0';

    rssi = packetRssi;
    snr = packetSnr;
    packetCount++;

    char* token = strtok(rxpacket, ",");
    if (token == NULL || strcmp(token, settings.magicWord.c_str()) != 0) {
        loraIdle = true;
        return;
    }

    // New format: heat,finishTime,personalBest,ftd,offCourse,dnf,rerun,cones,carID
    char *heat = strtok(NULL, ",");
    char *finishTime = strtok(NULL, ",");
    char *personalBest = strtok(NULL, ",");
    char *ftd = strtok(NULL, ",");
    char *offCourse = strtok(NULL, ",");
    char *dnf = strtok(NULL, ",");
    char *rerun = strtok(NULL, ",");
    char *cones = strtok(NULL, ",");
    char *carID = strtok(NULL, ",");

    bool showThisPacket = true;
    String rxCar = carID ? String(carID) : "";
    rxCar.trim();

    if (settings.filterMode == FilterMode::MyCar && settings.carId.length() > 0) {
        showThisPacket = rxCar.equalsIgnoreCase(settings.carId);
    } else if (settings.filterMode == FilterMode::MyClass && settings.carClass.length() > 0) {
        String rxClass = extractClass(rxCar);
        showThisPacket = rxClass.equalsIgnoreCase(settings.carClass);
    }

    if (carID && finishTime && ftd && personalBest && offCourse && dnf && rerun && cones && showThisPacket) {
        Serial.print(carID); Serial.print(",");
        Serial.print(finishTime); Serial.print(",");
        Serial.print(ftd); Serial.print(",");
        Serial.print(personalBest); Serial.print(",");
        Serial.print(offCourse); Serial.print(",");
        Serial.print(dnf); Serial.print(",");
        Serial.print(rerun); Serial.print(",");
        Serial.println(cones);

        display.clear();

        if (settings.diagnostics) {
            display.setFont(ArialMT_Plain_16);
            display.drawString(0, 0, "Diagnostics Mode");
            display.drawString(0, 20, "RSSI: " + String(rssi) + " dBm");
            display.drawString(0, 36, "SNR: " + String(snr) + " dB");
            display.setFont(ArialMT_Plain_10);
            display.drawString(0, 52, "Size: " + String(size) + "  Packets: " + String(packetCount));
            display.display();
            loraIdle = true;
            return;
        }

        display.setFont(ArialMT_Plain_24);
        display.drawString(0, 0, formatCarID(carID));

        if (finishTime) {
            String finishDisplay = String(finishTime);
            if (cones && atoi(cones) != 0) {
                finishDisplay += " +" + String(atoi(cones));
            }
            display.drawString(0, 20, finishDisplay);
        }

        // === Priority Status Display ===
        display.setFont(ArialMT_Plain_24);
        if (rerun && atoi(rerun)) {
            display.drawString(0, 40, "RERUN");
        } else if (dnf && atoi(dnf)) {
            display.drawString(0, 40, "DNF");
        } else if (offCourse && atoi(offCourse)) {
            display.drawString(0, 40, "Off Course");
        } else if (ftd && atoi(ftd)) {
            int textWidth = display.getStringWidth("FTD!");
            int maxX = 128 - textWidth;
            for (int x = 0; x <= maxX; x += 6) {
                display.clear();
                display.drawString(0, 0, formatCarID(carID));
                String finishDisplay = String(finishTime);
                if (cones && atoi(cones) != 0) {
                    finishDisplay += " +" + String(atoi(cones));
                }
                display.drawString(0, 20, finishDisplay);
                display.drawString(x, 40, "FTD!");
                display.display();
                delay(60);
            }
            for (int x = maxX; x >= 0; x -= 6) {
                display.clear();
                display.drawString(0, 0, formatCarID(carID));
                String finishDisplay = String(finishTime);
                if (cones && atoi(cones) != 0) {
                    finishDisplay += " +" + String(atoi(cones));
                }
                display.drawString(0, 20, finishDisplay);
                display.drawString(x, 40, "FTD!");
                display.display();
                delay(60);
            }
        } else if (personalBest && atoi(personalBest)) {
            display.drawString(0, 40, "PB");
        }

        display.display();
    }

    loraIdle = true;
}


// === Arduino Setup ===
void setup() {
    Serial.begin(115200);
    Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
    pinMode(USER_BUTTON_PIN, INPUT_PULLUP);
    pinMode(WIFI_LED_PIN, OUTPUT);
    digitalWrite(WIFI_LED_PIN, LOW);

    startupTime = millis();
    VextON();
    delay(100);
    display.init();

    prefs.begin("supertimer", false);
    settings.load(prefs);

    // Show version number at boot
    display.clear();
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0, "SuperLiveTimer");
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 24, "Firmware:");
    display.drawString(0, 38, FIRMWARE_VERSION);
    display.display();
    delay(1500);

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

    handleWebRequests();
}

// === Arduino Main Loop ===
void loop() {
    if (wifiEnabled) server.handleClient();

    unsigned long now = millis();

    // Startup button ignore
    if (startupIgnoreActive) {
        if (now - startupTime < startupIgnoreTime) {
            if (loraIdle) {
                loraIdle = false;
                Radio.Rx(0);
            }
            Radio.IrqProcess();
            return;
        } else {
            lastButtonState = digitalRead(USER_BUTTON_PIN);
            startupIgnoreActive = false;
            if (loraIdle) {
                loraIdle = false;
                Radio.Rx(0);
            }
            Radio.IrqProcess();
            return;
        }
    }

    // --- Button Handling ---
    bool buttonPressed = digitalRead(USER_BUTTON_PIN) == LOW;

    if (lastButtonState != buttonPressed) {
        lastButtonState = buttonPressed;
        if (buttonPressed) {
            buttonPressStartTime = now;
            buttonHeld = false;
            wifiArmed = false;
        } else {
            unsigned long pressDuration = now - buttonPressStartTime;
            // WiFi toggle via long press
            if (!buttonHeld && wifiArmed && pressDuration >= wifiHoldDuration && pressDuration < sleepHoldDuration) {
                if (!wifiEnabled) {
                    startWiFiAP();
                } else {
                    stopWiFiAP();
                }
                wifiArmed = false;
            }
            // Cycle filter mode (all/my car/my class)
            else if (!buttonHeld && pressDuration < wifiHoldDuration) {
                settings.filterMode = static_cast<FilterMode>((static_cast<uint8_t>(settings.filterMode) + 1) % 3);
                settings.save(prefs);
                showStartupOLED();
            }
        }
    }

    if (buttonPressed && !buttonHeld) {
        unsigned long heldTime = now - buttonPressStartTime;
        if (heldTime >= wifiHoldDuration && heldTime < sleepHoldDuration && !wifiArmed) {
            wifiArmed = true;
            if (!wifiEnabled)
                showTempMessage("Release to", "enable wifi");
            else
                showTempMessage("Release to", "disable wifi");
        }
        else if (heldTime >= sleepHoldDuration) {
            buttonHeld = true;
            goToSleep();
        }
    }

    if (loraIdle) {
        loraIdle = false;
        Radio.Rx(0);
    }

    Radio.IrqProcess();
}
