#include <Wire.h>
#include "LoRaWan_APP.h"
#include "Arduino.h"
#include "HT_DEPG0290BxS800FxX_BW.h"

// E-Ink display setup (RST, DC, CS, BUSY, SCLK, MOSI, -1=no MISO, SPI freq)
DEPG0290BxS800FxX_BW display(5, 4, 3, 6, 2, 1, -1, 6000000);

// LoRa Parameters (raw LoRa mode)
#define RF_FREQUENCY                915000000 // Hz
#define TX_OUTPUT_POWER             14        // dBm
#define LORA_BANDWIDTH              0         // 125 kHz
#define LORA_SPREADING_FACTOR       7
#define LORA_CODINGRATE             1         // 4/5
#define LORA_PREAMBLE_LENGTH        8
#define LORA_SYMBOL_TIMEOUT         0
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON        false

#define RX_TIMEOUT_VALUE            1000
#define BUFFER_SIZE                 80

char rxpacket[BUFFER_SIZE];

static RadioEvents_t RadioEvents;
int16_t rssi;
int8_t snr;
bool lora_idle = true;

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi_, int8_t snr_) {
  rssi = rssi_;
  snr = snr_;
  memcpy(rxpacket, payload, size);
  rxpacket[size] = '\0';

  // Strip the MagicWord
  char* data = rxpacket;
  char* firstComma = strchr(data, ',');
  if (firstComma != nullptr) {
    data = firstComma + 1;
  }

  // Tokenize and extract fields
  char* tokens[7] = { nullptr };
  int i = 0;
  char* token = strtok(data, ",");
  while (token != nullptr && i < 7) {
    tokens[i++] = token;
    token = strtok(nullptr, ",");
  }

  if (i < 6) {
    Serial.println("Malformed data received.");
    return;
  }

  const char* carId = tokens[0];
  const char* elapsedTime = tokens[1];
  const char* ftd = tokens[2];
  const char* personalBest = tokens[3];
  const char* offCourse = tokens[4];
  const char* cones = tokens[5];

  // Determine status with priority: Off Course > FTD > Personal Best
  const char* status = "";
  if (strcmp(offCourse, "1") == 0) {
    status = "Off Course";
  } else if (strcmp(ftd, "1") == 0) {
    status = "FTD";
  } else if (strcmp(personalBest, "1") == 0) {
    status = "Personal Best";
  }

  // Build time line without "sec" and with optional cone penalty
  String timeLine = String("Time: ") + elapsedTime;
  int coneCount = atoi(cones);
  if (coneCount > 0) {
    timeLine += " +" + String(coneCount);
  }

  Serial.printf("CarID: %s, Time: %s, Cones: %d, Status: %s\n", carId, elapsedTime, coneCount, status);
  Serial.printf("RSSI: %d, SNR: %d\n", rssi, snr);

  display.clear();
  display.drawString(0, 0, "RX:");
  display.drawString(0, 20, String("Car: ") + carId);
  display.drawString(0, 40, timeLine);
  if (strlen(status) > 0) {
    display.drawString(0, 60, status);
  }

  display.display();
}

void setup() {
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  Serial.begin(115200);

  VextON();
  delay(100);

  display.init();
  display.clear();
  display.setFont(ArialMT_Plain_24);
  display.drawString(0, 0, "SuperLiveTimer Ready");
  display.display();

  RadioEvents.RxDone = OnRxDone;
  Radio.Init(&RadioEvents);

  Radio.SetChannel(RF_FREQUENCY);

  Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                    LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                    LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                    0, true, 0, 0, LORA_IQ_INVERSION_ON, true);

  Radio.Rx(0);
  Serial.println("LoRa Receiver with E-Ink ready.");
}

void VextON(void) {
  pinMode(18, OUTPUT);
  digitalWrite(18, HIGH);
}

void loop() {
  Radio.IrqProcess();
}
