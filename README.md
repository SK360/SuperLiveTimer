
# Heltec V3 LoRa: Wireless Finish Time System

This project contains Arduino sketches for a **LoRa-based wireless finish time display system** using the [Heltec Wireless Stick Lite V3](https://heltec.org/project/wifi-lora-32-v3/) (ESP32 with LoRa and OLED display).

The system is designed for **motorsports or time trial events** where finish times and results need to be transmitted wirelessly from a sender to one or more receivers, and displayed on an OLED screen in real-time.

## Sketches Overview

- **receiver.ino**: Listens for incoming LoRa packets and displays parsed results on the Heltec OLED.
- **sender.ino**: Reads result strings from the Serial port, sends them via LoRa, and briefly displays the transmitted finish time on its OLED.
- **sender-testdata.ino**: A test/demo version of the sender that generates and sends *simulated* race results every few seconds with random values for easy testing.

---

## Hardware Requirements

- [Heltec WiFi LoRa 32 (V3)](https://heltec.org/project/wifi-lora-32-v3/) for both sender and receiver (at least 2 boards required).
- USB cable for programming.
- Arduino IDE (1.8.x or 2.x) with Heltec board support and required libraries.

---

## Library Dependencies

Install these via **Library Manager**:
- `LoRaWan_APP.h` (from Heltec ESP32 board package)
- `HT_SSD1306Wire.h` (included with Heltec board package)

Add Heltec board support using [these instructions](https://heltec-automation-docs.readthedocs.io/en/latest/general/establish_serial_connection.html).

---

## Data Format

All communication is in **CSV (comma-separated value) strings**, e.g.:

```
CarID,FinishTime,FTD,PersonalBest,OffCourse,Cones
```

- **CarID**: String (e.g. `66EVX`)
- **FinishTime**: Float (e.g. `23.512`)
- **FTD**: `1` if fastest time of the day, else `0`
- **PersonalBest**: `1` if personal best, else `0`
- **OffCourse**: `1` if run was off course, else `0`
- **Cones**: Integer, number of cones hit

**Example:**

```
66EVX,23.512,1,0,0,0
```

---

## Usage

### 1. receiver.ino

- **Purpose:** Receives LoRa packets and displays parsed results on the OLED (Car ID, finish time, and status).
- **Display:** Car ID, Finish Time (+cone penalties if any), and a status line ("Off Course", "FTD!", "PB") in priority order.
- **Setup:**
  1. Flash `receiver.ino` to your Heltec V3.
  2. Power it up. The display will show "Waiting for Data" until a valid packet is received.
  3. When data is received, results are shown for each run.

### 2. sender.ino

- **Purpose:** Sends result strings typed/piped over Serial to the LoRa receiver.
- **Display:** Shows the finish time briefly on its OLED when a packet is sent.
- **Setup:**
  1. Flash `sender.ino` to another Heltec V3.
  2. Open Serial Monitor/Serial Plotter at 115200 baud.
  3. Type or pipe a result line in the specified CSV format (see above) and press Enter.
  4. Each valid line is transmitted wirelessly via LoRa.

### 3. sender-testdata.ino

- **Purpose:** Sends *random, simulated* result data every 3 seconds for receiver testing.
- **Behavior:** Cycles through different scenarios:
    - FTD
    - Personal Best
    - Off Course
    - Cones penalty
    - Normal run
- **Setup:**
  1. Flash `sender-testdata.ino` to your sender Heltec V3.
  2. Power on: The board will start transmitting test data automatically, cycling through different results.
  3. The OLED shows "Test Mode" and the random finish time being sent.

---

## Configuration

- **LoRa Frequency:** Set to 915MHz (for North America). Change `RF_FREQUENCY` in all sketches for your region (e.g. 868MHz for EU).
- **Transmission Power:** Default set for typical use, but can be adjusted in the sketch.

---

## Display Logic

`receiver.ino` parses each incoming CSV, and displays:
- **Line 1:** Car ID (formatted with a space between numbers and letters for readability)
- **Line 2:** Finish Time (plus "+[cones]" if any were hit)
- **Line 3:** Status message, in this priority:
    - "Off Course" if offcourse flag is set
    - "FTD!" if FTD flag is set
    - "PB" if personal best flag is set

---

## Serial Example for sender.ino

Send a result from the Serial Monitor (or via a script):

```
18CAMS,24.451,0,1,0,2
```

This means Car `18CAMS` finished with a time of `24.451`, did **not** get FTD, achieved personal best, was not off course, and hit **2 cones**.

---

## Troubleshooting

- Make sure both sender and receiver are set to the same LoRa frequency and use matching settings.
- Check antennas are attached before transmitting.
- Use the testdata sketch to verify your receiver is working.

---

## Credits

Created by Matt Simmons.  

---
