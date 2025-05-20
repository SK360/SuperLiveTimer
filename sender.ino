#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <Wire.h>
#include "HT_SSD1306Wire.h"

static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

#define BUTTON_PIN 0 // USER button GPIO
#define HOLD_TIME 1000 // ms: how long to hold to switch modes

#define RF_FREQUENCY    915000000 // Hz
#define TX_OUTPUT_POWER 21        // dBm
#define LORA_BANDWIDTH  0
#define LORA_SPREADING_FACTOR 7
#define LORA_CODINGRATE 1
#define LORA_PREAMBLE_LENGTH 8
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false
#define RX_TIMEOUT_VALUE 1000
#define BUFFER_SIZE 80

const char* MAGIC_WORD = "NHSCC";
char txpacket[BUFFER_SIZE];

bool lora_idle = true;

static RadioEvents_t RadioEvents;
void OnTxDone(void);
void OnTxTimeout(void);

enum Mode {
  MODE_SERIAL = 0,
  MODE_TEST = 1
};
Mode currentMode = MODE_SERIAL;

// Button handling
unsigned long buttonPressStart = 0;
bool buttonWasPressed = false;
bool holdHandled = false;

// For test mode
const char* CarIDList[] = {
    "66EVX", "18CAMS", "83SS", "88ES", "49GST", "91XP"
};
const int CarIDListSize = sizeof(CarIDList)/sizeof(CarIDList[0]);
const char* CarID = "66EVX";
float finishtime = 24.345;
bool ftd = true;
bool personalbest = false;
bool offcourse = false;
int cones = 0;
static int sendStep = 0;
unsigned long lastTestSend = 0;
const unsigned long TEST_SEND_PERIOD = 3000; // ms

void VextON(void) {
  pinMode(Vext,OUTPUT);
  digitalWrite(Vext, LOW);
}
void VextOFF(void) {
  pinMode(Vext,OUTPUT);
  digitalWrite(Vext, HIGH);
}

void showMode() {
  display.clear();
  display.setFont(ArialMT_Plain_16);
  if (currentMode == MODE_SERIAL) {
    display.drawString(0, 0, "Mode: Serial");
    display.drawString(0, 20, "Waiting for data");
  } else {
    display.drawString(0, 0, "Mode: Test");
  }
  display.display();
}

void setup() {
    Serial.begin(115200);
    Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

    VextON();
    delay(100);
    display.init();
    display.setFont(ArialMT_Plain_24);

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    RadioEvents.TxDone = OnTxDone;
    RadioEvents.TxTimeout = OnTxTimeout;

    Radio.Init(&RadioEvents);
    Radio.SetChannel(RF_FREQUENCY);
    Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                      LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                      LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                      true, 0, 0, LORA_IQ_INVERSION_ON, 3000);

    showMode();
    randomSeed(analogRead(0));
}

void loop() {
    handleButton();

    if (currentMode == MODE_SERIAL) {
        // --- Serial input to LoRa ---
        if (lora_idle && Serial.available()) {
            String inString = Serial.readStringUntil('\n');
            inString.trim();

            int totalLen = strlen(MAGIC_WORD) + 1 + inString.length();
            if (inString.length() > 0 && totalLen < BUFFER_SIZE) {
                snprintf(txpacket, BUFFER_SIZE, "%s,%s", MAGIC_WORD, inString.c_str());

                Serial.printf("\r\nsending packet \"%s\" , length %d\r\n", inString.c_str(), inString.length());

                int idx1 = inString.indexOf(',');
                int idx2 = inString.indexOf(',', idx1 + 1);
                String timeStr = (idx1 >= 0 && idx2 > idx1) ? inString.substring(idx1 + 1, idx2) : "?";
                display.clear();
                display.drawString(0, 0, timeStr);
                display.display();

                Radio.Send((uint8_t *)txpacket, strlen(txpacket));
                lora_idle = false;
            } else {
                Serial.println("Input too long or empty! (max 79 chars)");
            }
        }
    } else if (currentMode == MODE_TEST) {
        // --- Periodically send test packet ---
        unsigned long now = millis();
        if (lora_idle && (now - lastTestSend >= TEST_SEND_PERIOD)) {
            int carIdx = random(0, CarIDListSize);
            CarID = CarIDList[carIdx];

            switch(sendStep) {
                case 0: ftd = true; personalbest = false; offcourse = false; cones = 0; break;
                case 1: ftd = false; personalbest = true; offcourse = false; cones = 0; break;
                case 2: ftd = false; personalbest = false; offcourse = true; cones = 0; break;
                case 3: ftd = false; personalbest = false; offcourse = false; cones = 2; break;
                case 4: ftd = false; personalbest = false; offcourse = false; cones = 0; break;
            }
            sendStep = (sendStep + 1) % 5;

            long finishtime_raw = random(20000, 40001);
            finishtime = finishtime_raw / 1000.0;

            char ft_str[10];
            snprintf(ft_str, sizeof(ft_str), "%.3f", finishtime);

            snprintf(txpacket, BUFFER_SIZE, "%s,%s,%.3f,%d,%d,%d,%d",
                 MAGIC_WORD,
                 CarID,
                 finishtime,
                 ftd ? 1 : 0,
                 personalbest ? 1 : 0,
                 offcourse ? 1 : 0,
                 cones);

            Serial.printf("\r\nsending packet \"%s\" , length %d\r\n", txpacket, strlen(txpacket));

            display.clear();
            display.drawString(0, 0, "Mode: Test");
            display.drawString(0, 20, ft_str);
            display.display();

            Radio.Send((uint8_t *)txpacket, strlen(txpacket));
            lora_idle = false;
            lastTestSend = now;
        }
    }
    Radio.IrqProcess();
}

// Button handling for 1s hold to toggle mode
void handleButton() {
    bool buttonPressed = (digitalRead(BUTTON_PIN) == LOW);

    if (buttonPressed && !buttonWasPressed) {
        // Button just pressed
        buttonPressStart = millis();
        holdHandled = false;
    }
    else if (buttonPressed && !holdHandled && (millis() - buttonPressStart >= HOLD_TIME)) {
        // Button held for at least HOLD_TIME ms, switch mode
        toggleMode();
        holdHandled = true;  // Prevent multiple toggles on one hold
    }
    else if (!buttonPressed && buttonWasPressed) {
        // Button released
        buttonPressStart = 0;
        holdHandled = false;
    }
    buttonWasPressed = buttonPressed;
}

void toggleMode() {
    currentMode = (currentMode == MODE_SERIAL) ? MODE_TEST : MODE_SERIAL;
    showMode();
    Serial.printf("Mode switched to %s\n", (currentMode == MODE_SERIAL) ? "Serial" : "Test");
}

void OnTxDone(void) {
    Serial.println("TX done......");
    display.drawString(0, 40, "Sent");
    display.display();
    lora_idle = true;
}

void OnTxTimeout(void) {
    Radio.Sleep();
    Serial.println("TX Timeout......");
    lora_idle = true;
}