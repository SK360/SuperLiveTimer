#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <Wire.h>
#include "HT_SSD1306Wire.h"
#include <Bounce2.h>

static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

#define BUTTON_PIN 0 // USER button GPIO
#define HOLD_TIME 2000 // ms

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

unsigned long buttonPressStart = 0;
bool holdHandled = false;

Bounce debouncer = Bounce();

// Test mode data
const char* CarIDList[] = {
    "66EVX", "87EVX", "41EVX", "18CAMS", "127CAMS", "5CAMS", "83SS", "88ES", "49GST", "91XP", "9SSP", "77EST"
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
const unsigned long TEST_SEND_PERIOD = 5000; // ms

void VextON(void) {
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
}
void VextOFF(void) {
  pinMode(Vext, OUTPUT);
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
  Serial.begin(9600);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  VextON();
  delay(100);
  display.init();
  display.setFont(ArialMT_Plain_24);

  debouncer.attach(BUTTON_PIN, INPUT_PULLUP);
  debouncer.interval(25); // Debounce interval

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
  debouncer.update();
  if (debouncer.fell()) {
    buttonPressStart = millis();
    holdHandled = false;
  } else if (debouncer.read() == LOW && !holdHandled && (millis() - buttonPressStart >= HOLD_TIME)) {
    toggleMode();
    holdHandled = true;
  } else if (debouncer.rose()) {
    buttonPressStart = 0;
    holdHandled = false;
  }

  if (currentMode == MODE_SERIAL) {
    if (lora_idle && Serial.available()) {
      String inString = Serial.readStringUntil('\n');
      inString.trim();

      if (inString.length() > 0 && (strlen(MAGIC_WORD) + 1 + inString.length()) < BUFFER_SIZE) {
        int idxs[6];
        int lastIdx = -1;
        for (int i = 0; i < 6; i++) {
          idxs[i] = inString.indexOf(',', lastIdx + 1);
          if (idxs[i] == -1 && i < 5) {
            Serial.println("Invalid input format!");
            return;
          }
          lastIdx = idxs[i];
        }

        String carID = inString.substring(0, idxs[0]);
        String finishTimeStr = inString.substring(idxs[0] + 1, idxs[1]);
        String ftdStr = inString.substring(idxs[1] + 1, idxs[2]);
        String pbStr = inString.substring(idxs[2] + 1, idxs[3]);
        String ocStr = inString.substring(idxs[3] + 1, idxs[4]);
        String conesStr = inString.substring(idxs[4] + 1);

        int ftd = ftdStr.toInt();
        int personalBest = pbStr.toInt();
        int offcourse = ocStr.toInt();
        int cones = conesStr.toInt();
        float finishTime = finishTimeStr.toFloat();

        snprintf(txpacket, BUFFER_SIZE, "%s,2,%.3f,%d,%d,%d,0,0,%d,%s",
                 MAGIC_WORD,
                 finishTime,
                 personalBest,
                 ftd,
                 offcourse,
                 cones,
                 carID.c_str());

        Serial.printf("\r\nsending packet \"%s\" , length %d\r\n", txpacket, strlen(txpacket));

        display.clear();
        display.drawString(0, 0, finishTimeStr);
        display.display();

        Radio.Send((uint8_t *)txpacket, strlen(txpacket));
        lora_idle = false;
      } else {
        Serial.println("Input too long or empty! (max 79 chars)");
      }
    }
  } else if (currentMode == MODE_TEST) {
    unsigned long now = millis();
    if (lora_idle && (now - lastTestSend >= TEST_SEND_PERIOD)) {
        int carIdx = random(0, CarIDListSize);
        CarID = CarIDList[carIdx];

        ftd = false;
        personalbest = false;
        offcourse = false;
        cones = 0;
        bool dnf = false;
        bool rerun = false;

        switch (sendStep) {
            case 0: break;
            case 1: personalbest = true; break;
            case 2: ftd = true; break;
            case 3: offcourse = true; break;
            case 4: dnf = true; break;
            case 5: rerun = true; break;
            case 6: cones = 2; break;
        }

        sendStep = (sendStep + 1) % 7;

        long finishtime_raw = random(20000, 40001);
        finishtime = finishtime_raw / 1000.0;

        snprintf(txpacket, BUFFER_SIZE, "%s,2,%.3f,%d,%d,%d,%d,%d,%d,%s",
                 MAGIC_WORD,
                 finishtime,
                 personalbest ? 1 : 0,
                 ftd ? 1 : 0,
                 offcourse ? 1 : 0,
                 dnf ? 1 : 0,
                 rerun ? 1 : 0,
                 cones,
                 CarID);

        Serial.printf("\r\nsending packet \"%s\" , length %d\r\n", txpacket, strlen(txpacket));

        char ft_str[10];
        snprintf(ft_str, sizeof(ft_str), "%.3f", finishtime);

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
