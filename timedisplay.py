import serial

def parse_lora_data(line):
    """Parses a line in format: carid, time, personalbest, ftd, offcourse, coneshit"""
    try:
        parts = line.strip().split(',')
        if len(parts) != 6:
            raise ValueError("Incorrect number of fields.")
        # Ignore carid (parts[0])
        time_val = parts[1].strip()
        personalbest = int(parts[2].strip())
        ftd = int(parts[3].strip())
        offcourse = int(parts[4].strip())
        coneshit = int(parts[5].strip())
        return time_val, personalbest, ftd, offcourse, coneshit
    except ValueError as e:
        print(f"[WARN] Invalid data: {line} ({e})")
        return None

def determine_color(personalbest, ftd, offcourse):
    if offcourse:
        return 'red'
    elif ftd:
        return 'purple'
    elif personalbest:
        return 'green'
    else:
        return 'yellow'

def echo_to_terminal(time_val, coneshit, color):
    print(f"[ECHO] Time: {time_val} | Cones Hit: {coneshit} | Color: {color}")

def listen_to_lora(port='/dev/ttyUSB0', baudrate=115200):
    try:
        with serial.Serial(port, baudrate, timeout=1) as ser:
            print(f"[INFO] Listening for FinishTime data on {port}...")
            while True:
                if ser.in_waiting:
                    line = ser.readline().decode('utf-8').strip()
                    data = parse_lora_data(line)
                    if data:
                        time_val, personalbest, ftd, offcourse, coneshit = data
                        color = determine_color(personalbest, ftd, offcourse)
                        echo_to_terminal(time_val, coneshit, color)
    except serial.SerialException as e:
        print(f"[ERROR] Serial error: {e}")
    except KeyboardInterrupt:
        print("\n[INFO] Stopped by user.")

if __name__ == '__main__':
    listen_to_lora()
