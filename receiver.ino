#include <Wire.h>
#include "LoRaWan_APP.h"
#include "Arduino.h"
#include "HT_SSD1306Wire.h"
#include "esp_sleep.h"

static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

#define RF_FREQUENCY                                915000000 // Hz
#define TX_OUTPUT_POWER                             14        // dBm
#define LORA_BANDWIDTH                              0
#define LORA_SPREADING_FACTOR                       7
#define LORA_CODINGRATE                             1
#define LORA_PREAMBLE_LENGTH                        8
#define LORA_SYMBOL_TIMEOUT                         0
#define LORA_FIX_LENGTH_PAYLOAD_ON                  false
#define LORA_IQ_INVERSION_ON                        false

#define RX_TIMEOUT_VALUE                            1000
#define BUFFER_SIZE                                 80

const char* MAGIC_WORD = "NHSCC";
char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];

static RadioEvents_t RadioEvents;

int16_t txNumber;
int16_t rssi, rxSize;
int8_t snr;
int packetCount = 0;
bool lora_idle = true;

#define USER_BUTTON_PIN 0
const unsigned long LONG_PRESS_DURATION = 2000;

enum DisplayMode { RACE_MODE, DIAGNOSTICS_MODE };
DisplayMode currentMode = RACE_MODE;

bool lastButtonState = HIGH;
unsigned long buttonPressStartTime = 0;
bool buttonHeld = false;

unsigned long startupIgnoreTime = 3000;
unsigned long startupTime = 0;

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

void VextON(void) {
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW);
}

void VextOFF(void) {
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, HIGH);
}

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
    display.drawString(0, 0, "FinishTime");
    display.drawString(0, 20, "Waiting for Data");
    display.display();

    RadioEvents.RxDone = OnRxDone;
    Radio.Init(&RadioEvents);
    Radio.SetChannel(RF_FREQUENCY);
    Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                      LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                      LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                      0, true, 0, 0, LORA_IQ_INVERSION_ON, true);
}

void loop() {
    // Ignore button during initial boot
    if (millis() - startupTime < startupIgnoreTime) {
        if (lora_idle) {
            lora_idle = false;
            Radio.Rx(0);
        }
        Radio.IrqProcess();
        return;
    }

    bool buttonPressed = digitalRead(USER_BUTTON_PIN) == LOW;
    unsigned long now = millis();

    // Detect button press change
    if (lastButtonState != buttonPressed) {
        lastButtonState = buttonPressed;

        if (buttonPressed) {
            buttonPressStartTime = now;
            buttonHeld = false;
        } else {
            unsigned long pressDuration = now - buttonPressStartTime;
            if (!buttonHeld && pressDuration < LONG_PRESS_DURATION) {
                // Short press: toggle mode
                currentMode = (currentMode == RACE_MODE) ? DIAGNOSTICS_MODE : RACE_MODE;

                display.clear();
                display.setFont(ArialMT_Plain_16);
                display.drawString(0, 20, currentMode == DIAGNOSTICS_MODE ? "Diagnostics On" : "Race Mode");
                display.display();
                delay(500);
            }
        }
    }

    // Long press for sleep
    if (buttonPressed && !buttonHeld && (now - buttonPressStartTime > LONG_PRESS_DURATION)) {
        buttonHeld = true;
        goToSleep();
    }

    if (lora_idle) {
        lora_idle = false;
        Radio.Rx(0);
    }

    Radio.IrqProcess();
}

void OnRxDone(uint8_t *payload, uint16_t size, int16_t packetRssi, int8_t packetSnr) {
    memcpy(rxpacket, payload, size);
    rxpacket[size] = '\0';

    rssi = packetRssi;
    snr = packetSnr;
    packetCount++;

    display.clear();
    display.setFont(ArialMT_Plain_16);

    if (currentMode == DIAGNOSTICS_MODE) {
        display.drawString(0, 0, "Diagnostics Mode");
        display.drawString(0, 20, "RSSI: " + String(rssi) + " dBm");
        display.drawString(0, 36, "SNR: " + String(snr) + " dB");
        display.setFont(ArialMT_Plain_10);
        display.drawString(0, 52, "Size: " + String(size) + "  Packets: " + String(packetCount));
        display.display();
        lora_idle = true;
        return;
    }

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

    if (carID && finishTime && ftd && personalBest && offCourse && cones) {
        Serial.print(carID); Serial.print(",");
        Serial.print(finishTime); Serial.print(",");
        Serial.print(ftd); Serial.print(",");
        Serial.print(personalBest); Serial.print(",");
        Serial.print(offCourse); Serial.print(",");
        Serial.println(cones);
    }

    display.setFont(ArialMT_Plain_24);
    if (carID != NULL) {
        display.drawString(0, 0, formatCarID(carID));
    }

    if (finishTime != NULL) {
        String finishDisplay = String(finishTime);
        if (cones != NULL && atoi(cones) != 0) {
            finishDisplay += " +" + String(atoi(cones));
        }
        display.drawString(0, 20, finishDisplay);
    }

    String statusMsg = "";
    if (offCourse != NULL && atoi(offCourse)) {
        statusMsg = "Off Course";
    } else if (ftd != NULL && atoi(ftd)) {
        statusMsg = "FTD!";
    } else if (personalBest != NULL && atoi(personalBest)) {
        statusMsg = "PB";
    }

    if (statusMsg.length() > 0) {
        display.drawString(0, 40, statusMsg);
    }

    display.display();
    lora_idle = true;
}
