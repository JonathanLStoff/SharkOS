"""
SharkOS diagnostic: reads serial from ESP32, subscribes to all sharkos/# MQTT topics,
and publishes test commands. Run with: python mqtt_serial_diag.py
"""
import threading, time, json, sys
import serial
import paho.mqtt.client as mqtt

# ── Config ────────────────────────────────────────────────────────────────────
SERIAL_PORT   = "COM4"
SERIAL_BAUD   = 115200
MQTT_HOST     = "192.168.1.112"
MQTT_PORT     = 1883
MQTT_USER     = "sharkos"
MQTT_PASS     = "no"
CMD_TOPIC     = "sharkos/cmd"

# ── Helpers ───────────────────────────────────────────────────────────────────
def ts():
    return time.strftime("%H:%M:%S")

def pub(client, payload):
    print(f"\n[{ts()}] → MQTT publish to {CMD_TOPIC}: {payload}")
    client.publish(CMD_TOPIC, payload)

# ── Serial reader thread ───────────────────────────────────────────────────────
def serial_reader(port, baud):
    try:
        ser = serial.Serial(port, baud, timeout=1)
        print(f"[{ts()}] Serial opened: {port} @ {baud}")
        while True:
            try:
                line = ser.readline().decode("utf-8", errors="replace").rstrip()
                if line:
                    print(f"[{ts()}] SERIAL | {line}")
            except Exception as e:
                print(f"[{ts()}] Serial read error: {e}")
                time.sleep(1)
    except Exception as e:
        print(f"[{ts()}] Could not open serial {port}: {e}")

# ── MQTT callbacks ─────────────────────────────────────────────────────────────
def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print(f"[{ts()}] MQTT connected to {MQTT_HOST}:{MQTT_PORT}")
        client.subscribe("sharkos/#")
        print(f"[{ts()}] Subscribed to sharkos/#")
    else:
        print(f"[{ts()}] MQTT connect failed rc={rc}")

def on_message(client, userdata, msg):
    payload = msg.payload.decode("utf-8", errors="replace")
    print(f"[{ts()}] ← MQTT [{msg.topic}] {payload[:300]}")

def on_disconnect(client, userdata, rc, properties=None):
    print(f"[{ts()}] MQTT disconnected rc={rc}")

# ── Main ───────────────────────────────────────────────────────────────────────
def main():
    # Start serial reader in background
    t = threading.Thread(target=serial_reader, args=(SERIAL_PORT, SERIAL_BAUD), daemon=True)
    t.start()

    # Connect to MQTT
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="sharkos-diag")
    client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.on_connect    = on_connect
    client.on_message    = on_message
    client.on_disconnect = on_disconnect
    client.connect(MQTT_HOST, MQTT_PORT, keepalive=30)
    client.loop_start()

    # Wait for MQTT to connect
    time.sleep(3)

    print(f"\n[{ts()}] ── TEST SEQUENCE START ────────────────────────────────")

    # 1. Request status snapshot
    print(f"\n[{ts()}] TEST 1: status.info")
    pub(client, "status.info")
    time.sleep(4)

    # 2. Request device status (radio health)
    print(f"\n[{ts()}] TEST 2: device.status")
    pub(client, "device.status")
    time.sleep(4)

    # 3. Start CC1101 sub-ghz read (100ms sweep so we see RSSI output)
    print(f"\n[{ts()}] TEST 3: subghz.read.start (433-434 MHz, OOK)")
    pub(client, json.dumps({
        "command": "subghz.read.start",
        "params": {
            "top_frequency_mhz": 434.0,
            "bottom_frequency_mhz": 433.0,
            "modulation_one": "OOK",
            "modulation_two": "OOK"
        }
    }))
    time.sleep(6)

    # 4. Stop subghz
    print(f"\n[{ts()}] TEST 4: subghz.read.stop")
    pub(client, "subghz.read.stop")
    time.sleep(2)

    # 5. NFC poll
    print(f"\n[{ts()}] TEST 5: nfc.poll.start")
    pub(client, "nfc.poll.start")
    time.sleep(4)

    pub(client, "nfc.poll.stop")
    time.sleep(1)

    print(f"\n[{ts()}] ── TEST SEQUENCE DONE — watching for 20s ─────────────")
    time.sleep(20)

    client.loop_stop()
    client.disconnect()
    print(f"[{ts()}] Done.")

if __name__ == "__main__":
    main()
