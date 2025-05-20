#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <Wire.h>
#include "HT_SSD1306Wire.h"

static SSD1306Wire  display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

#define RF_FREQUENCY                                915000000 // Hz
#define TX_OUTPUT_POWER                             21        // dBm

#define LORA_BANDWIDTH                              0
#define LORA_SPREADING_FACTOR                       7
#define LORA_CODINGRATE                             1
#define LORA_PREAMBLE_LENGTH                        8
#define LORA_SYMBOL_TIMEOUT                         0
#define LORA_FIX_LENGTH_PAYLOAD_ON                  false
#define LORA_IQ_INVERSION_ON                        false

#define RX_TIMEOUT_VALUE                            1000
#define BUFFER_SIZE                                 80 // Increased buffer to allow longer strings

const char* MAGIC_WORD = "NHSCC";

char txpacket[BUFFER_SIZE];

bool lora_idle = true;

static RadioEvents_t RadioEvents;
void OnTxDone(void);
void OnTxTimeout(void);

void setup() {
    Serial.begin(115200);
    Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

    VextON();
    delay(100);
    display.init();

    display.setFont(ArialMT_Plain_24);

    RadioEvents.TxDone = OnTxDone;
    RadioEvents.TxTimeout = OnTxTimeout;

    Radio.Init(&RadioEvents);
    Radio.SetChannel(RF_FREQUENCY);
    Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                      LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                      LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                      true, 0, 0, LORA_IQ_INVERSION_ON, 3000);
}

void VextON(void)
{
  pinMode(Vext,OUTPUT);
  digitalWrite(Vext, LOW);
}

void VextOFF(void)
{
  pinMode(Vext,OUTPUT);
  digitalWrite(Vext, HIGH);
}

void loop()
{
    // --- NEW: Check for input from Serial ---
    if (lora_idle && Serial.available()) {
        // Read serial string (until newline)
        String inString = Serial.readStringUntil('\n');
        inString.trim(); // Remove whitespace/newline

        // Make sure magic word + comma + input fits in buffer
        int totalLen = strlen(MAGIC_WORD) + 1 + inString.length(); // +1 for comma
        if (inString.length() > 0 && totalLen < BUFFER_SIZE) {
            // Build the LoRa packet with magic word prepended
            snprintf(txpacket, BUFFER_SIZE, "%s,%s", MAGIC_WORD, inString.c_str());

            // Serial output does NOT show magic word
            Serial.printf("\r\nsending packet \"%s\" , length %d\r\n", inString.c_str(), inString.length());

            int idx1 = inString.indexOf(','); // first comma
            int idx2 = inString.indexOf(',', idx1 + 1); // second comma
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

    Radio.IrqProcess();
}

void OnTxDone(void)
{
    Serial.println("TX done......");
    display.drawString(0, 20, "Sent");
    display.display();
    lora_idle = true;
}

void OnTxTimeout(void)
{
    Radio.Sleep();
    Serial.println("TX Timeout......");
    lora_idle = true;
}
