import serial
import time
import sys

port = "/dev/cu.usbmodem1101"
baud = 115200

try:
    ser = serial.Serial(port, baud, timeout=1)
    print(f"Listening on {port}...")
    
    # Toggle DTR/RTS to reset
    ser.dtr = False
    ser.rts = False
    time.sleep(0.1)
    ser.dtr = True
    ser.rts = True
    time.sleep(0.1)
    ser.dtr = False
    ser.rts = False
    
    start_time = time.time()
    while time.time() - start_time < 10:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            print(line)
except Exception as e:
    print(f"Error: {e}")
