#include <Wire.h>
#include "LoRaWan_APP.h"
#include "Arduino.h"
#include "HT_SSD1306Wire.h"
#include "esp_sleep.h"

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

// Button
#define USER_BUTTON_PIN 0
const unsigned long TOGGLE_HOLD_DURATION = 5000; // 5 seconds
unsigned long buttonPressStartTime = 0;
bool buttonHeld = false;
bool displayToggledDuringHold = false;

// Display state
bool displayOn = false;

// Format Car ID
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

// Toggle Display With Feedback
void toggleDisplay(bool on) {
    if (on) {
        VextON();
        delay(100);
        display.init();
        display.clear();
        display.setFont(ArialMT_Plain_16);
        display.drawString(0, 20, "Display On");
        display.display();
    } else {
        display.clear();
        display.setFont(ArialMT_Plain_16);
        display.drawString(0, 20, "Display Off");
        display.display();
        delay(1000);
        display.clear();
        display.display();
        VextOFF();
    }
}

// Setup
void setup() {
    Serial.begin(115200);
    Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
    pinMode(USER_BUTTON_PIN, INPUT_PULLUP);

    txNumber = 0;
    rssi = 0;

    // Show splash screen
    VextON();
    delay(100);
    display.init();
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 20, "SuperLiveTimer");
    display.display();
    delay(5000);
    display.clear();
    display.display();
    VextOFF();

    RadioEvents.RxDone = OnRxDone;
    Radio.Init(&RadioEvents);
    Radio.SetChannel(RF_FREQUENCY);
    Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                      LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                      LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                      0, true, 0, 0, LORA_IQ_INVERSION_ON, true);
}

// Main Loop
void loop() {
    bool buttonPressed = digitalRead(USER_BUTTON_PIN) == LOW;
    unsigned long now = millis();

    if (buttonPressed) {
        if (!buttonHeld) {
            buttonHeld = true;
            buttonPressStartTime = now;
            displayToggledDuringHold = false;
        } else if (!displayToggledDuringHold && (now - buttonPressStartTime >= TOGGLE_HOLD_DURATION)) {
            displayOn = !displayOn;
            toggleDisplay(displayOn);  // Immediate feedback during hold
            displayToggledDuringHold = true;
        }
    } else {
        buttonHeld = false;
    }

    if (lora_idle) {
        lora_idle = false;
        Radio.Rx(0);
    }

    Radio.IrqProcess();
}

// LoRa Receive Handler
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

    // Parse all expected fields
    char *heat         = strtok(NULL, ",");
    char *finishTime   = strtok(NULL, ",");
    char *personalBest = strtok(NULL, ",");
    char *ftd          = strtok(NULL, ",");
    char *offCourse    = strtok(NULL, ",");
    char *dnf          = strtok(NULL, ",");
    char *rerun        = strtok(NULL, ",");
    char *cones        = strtok(NULL, ",");
    char *carID        = strtok(NULL, ",");

    if (heat && finishTime && personalBest && ftd && offCourse && dnf && rerun && cones && carID) {
        Serial.print(heat); Serial.print(",");
        Serial.print(finishTime); Serial.print(",");
        Serial.print(personalBest); Serial.print(",");
        Serial.print(ftd); Serial.print(",");
        Serial.print(offCourse); Serial.print(",");
        Serial.print(dnf); Serial.print(",");
        Serial.print(rerun); Serial.print(",");
        Serial.print(cones); Serial.print(",");
        Serial.println(carID);
    }

    if (!displayOn) {
        lora_idle = true;
        return;
    }

    display.clear();
    display.setFont(ArialMT_Plain_16);

    if (finishTime) {
        String finishDisplay = String(finishTime);
        if (cones && atoi(cones) != 0) {
            finishDisplay += " +" + String(atoi(cones));
        }
        display.drawString(0, 0, finishDisplay);
    }

    display.drawString(0, 20, "RSSI: " + String(rssi) + " dBm");
    display.display();

    lora_idle = true;
}
