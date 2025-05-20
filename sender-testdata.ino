#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <Wire.h>
#include "HT_SSD1306Wire.h"

static SSD1306Wire  display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED); // addr , freq , i2c group , resolution , rst

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
#define BUFFER_SIZE                                 80 // Increased for magic word and data

const char* MAGIC_WORD = "NHSCC"; // <-- Your magic word!

char txpacket[BUFFER_SIZE];

bool lora_idle = true;

static RadioEvents_t RadioEvents;
void OnTxDone(void);
void OnTxTimeout(void);

// -------- CarID List ---------
const char* CarIDList[] = {
    "66EVX",
    "18CAMS",
    "83SS",
    "88ES",
    "49GST",
    "91XP"
};
const int CarIDListSize = sizeof(CarIDList)/sizeof(CarIDList[0]);
// -----------------------------

// Example data (default state)
const char* CarID = "66EVX";
float finishtime = 24.345;
bool ftd = true;
bool personalbest = false;
bool offcourse = false;
int cones = 0;

// --------- Step tracking variable ----------
static int sendStep = 0;  // 0-4 for 5 steps
// -------------------------------------------

void setup() {
    Serial.begin(115200);
    Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

    VextON();
    delay(100);
    display.init();

    display.setFont(ArialMT_Plain_24);

    randomSeed(analogRead(0));  // Seed random number generator

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

void VextOFF(void) //Vext default OFF
{
  pinMode(Vext,OUTPUT);
  digitalWrite(Vext, HIGH);
}

void loop()
{
    if (lora_idle == true)
    {
        delay(3000);

        // -------------- Random CarID --------------------
        int carIdx = random(0, CarIDListSize);
        CarID = CarIDList[carIdx];
        // ------------------------------------------------

        // -------------- Cycle through flag settings --------------------
        switch(sendStep) {
            case 0: // FTD only
                ftd = true;
                personalbest = false;
                offcourse = false;
                cones = 0;
                break;
            case 1: // PersonalBest only
                ftd = false;
                personalbest = true;
                offcourse = false;
                cones = 0;
                break;
            case 2: // OffCourse only
                ftd = false;
                personalbest = false;
                offcourse = true;
                cones = 0;
                break;
            case 3: // Cones=2, all flags false
                ftd = false;
                personalbest = false;
                offcourse = false;
                cones = 2;
                break;
            case 4: // All flags false, cones=0
                ftd = false;
                personalbest = false;
                offcourse = false;
                cones = 0;
                break;
        }
        sendStep = (sendStep + 1) % 5;  // 0-4 for 5 steps, then loop
        // ----------------------------------------------------------------

        // Generate random finishtime between 20.000 and 40.000
        long finishtime_raw = random(20000, 40001);  // 20000 to 40000 inclusive
        finishtime = finishtime_raw / 1000.0;

        char ft_str[10];
        snprintf(ft_str, sizeof(ft_str), "%.3f", finishtime);

        // ----------- PREPEND MAGIC WORD ---------------
        snprintf(txpacket, BUFFER_SIZE, "%s,%s,%.3f,%d,%d,%d,%d",
                 MAGIC_WORD,   // <-- magic word first
                 CarID,
                 finishtime,
                 ftd ? 1 : 0,
                 personalbest ? 1 : 0,
                 offcourse ? 1 : 0,
                 cones);
        // -----------------------------------------------

        Serial.printf("\r\nsending packet \"%s\" , length %d\r\n", txpacket, strlen(txpacket));

        display.clear();
        display.drawString(0, 0, "Test Mode");
        display.drawString(0, 20, ft_str);
        display.display();

        Radio.Send((uint8_t *)txpacket, strlen(txpacket));
        lora_idle = false;
    }
    Radio.IrqProcess();
}

void OnTxDone(void)
{
    Serial.println("TX done......");
    display.drawString(0, 40, "Sent");
    display.display();
    lora_idle = true;
}

void OnTxTimeout(void)
{
    Radio.Sleep();
    Serial.println("TX Timeout......");
    lora_idle = true;
}
