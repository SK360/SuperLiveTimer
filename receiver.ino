#include <Wire.h>
#include "LoRaWan_APP.h"
#include "Arduino.h"
#include "HT_SSD1306Wire.h"

static SSD1306Wire  display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

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
#define BUFFER_SIZE                                 80 // Increased size for magic word

const char* MAGIC_WORD = "NHSCC"; // <-- Magic word must match sender!

char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];

static RadioEvents_t RadioEvents;

int16_t txNumber;
int16_t rssi,rxSize;

bool lora_idle = true;

// ---- FORMAT CAR ID FUNCTION ----
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
// --------------------------------

void setup() {
    Serial.begin(115200);
    Mcu.begin(HELTEC_BOARD,SLOW_CLK_TPYE);
    
    txNumber=0;
    rssi=0;
    VextON();
    delay(100);
    display.init();

    display.setFont(ArialMT_Plain_16);

    display.drawString(0, 0, "FinishTime");
    display.drawString(0, 20, "Waiting for Data");
    display.display();
  
    RadioEvents.RxDone = OnRxDone;
    Radio.Init( &RadioEvents );
    Radio.SetChannel( RF_FREQUENCY );
    Radio.SetRxConfig( MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                               LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                               LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                               0, true, 0, 0, LORA_IQ_INVERSION_ON, true );
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
  if(lora_idle)
  {
    lora_idle = false;
    Radio.Rx(0);
  }
  Radio.IrqProcess( );
}

void OnRxDone( uint8_t *payload, uint16_t size, int16_t packetRssi, int8_t snr )
{
    memcpy(rxpacket, payload, size );
    rxpacket[size] = '\0';

    // Check if the message starts with the magic word
    char* token = strtok(rxpacket, ",");
    if (token == NULL) {
        lora_idle = true;
        return;
    }

    if (strcmp(token, MAGIC_WORD) != 0) {
        // Magic word does not match, ignore this packet
        lora_idle = true;
        return;
    }

    // From here, rxpacket's first token is the magic word, so move on to the next tokens
    char *carID = strtok(NULL, ",");
    char *finishTime = strtok(NULL, ",");
    char *ftd = strtok(NULL, ",");
    char *personalBest = strtok(NULL, ",");
    char *offCourse = strtok(NULL, ",");
    char *cones = strtok(NULL, ",");

    // Only print out valid packets (without the magic word)
    if (carID && finishTime && ftd && personalBest && offCourse && cones) {
        // Print the message to serial WITHOUT the magic word
        Serial.print(carID); Serial.print(",");
        Serial.print(finishTime); Serial.print(",");
        Serial.print(ftd); Serial.print(",");
        Serial.print(personalBest); Serial.print(",");
        Serial.print(offCourse); Serial.print(",");
        Serial.println(cones);
    }

    display.clear();
    display.setFont(ArialMT_Plain_24);

    // Line 1: Car ID (now formatted with space between numbers and letters)
    if (carID != NULL) {
        display.drawString(0, 0, formatCarID(carID));
    }

    // Line 2: Finish Time (+cones if not zero)
    String finishDisplay = "";
    if (finishTime != NULL) {
        finishDisplay = String(finishTime);
        if (cones != NULL && atoi(cones) != 0) {
            finishDisplay += " +" + String(atoi(cones));
        }
        display.drawString(0, 20, finishDisplay);
    }

    // Line 3: Status Message, priority: Off Course > FTD > Personal Best
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
