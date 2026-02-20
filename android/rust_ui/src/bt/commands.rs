use std::collections::HashMap;
use std::sync::OnceLock;

/// Global, read-only map of Bluetooth control commands the host can send to the
/// embedded device. Populated once at startup and exposed as `BT_COMMANDS`.
///
/// Each entry maps a short command key to a human-readable description and
/// the parameters the device-side implementation accepts. Keys focus on
/// long-running data-gathering/scan routines (start/stop) and one-shot
/// information queries used by the UI.
pub static BT_COMMANDS: OnceLock<HashMap<String, String>> = OnceLock::new();

fn make_bt_command_map() -> HashMap<String, String> {
    let mut m = HashMap::new();

    // BLE scanner (based on main/blescanner.ino)
    m.insert(
        "ble.scan.start".into(),
        "Start BLE advertisement scan. Params: { duration_seconds: int (default 5), active: bool (default true), name_filter: string (optional), rssi_threshold: int (optional) }".into(),
    );
    m.insert(
        "ble.scan.stop".into(),
        "Stop BLE scanning immediately. No params.".into(),
    );

    // Wi‑Fi network scanner (based on wifi-scanning.ino)
    m.insert(
        "wifi.scan.start".into(),
        "Start Wi‑Fi network scan. Params: { duration_seconds: int (device-dependent), channel: int (optional), passive: bool (optional), ssid_filter: string (optional) }".into(),
    );
    m.insert(
        "wifi.scan.stop".into(),
        "Stop any ongoing Wi‑Fi scan. No params.".into(),
    );

    // Wi‑Fi packet sniffer / promiscuous mode (based on packet-analyzer.ino)
    m.insert(
        "wifi.sniffer.start".into(),
        "Start Wi‑Fi promiscuous packet capture. Params: { channel: int | 'hop', hop_interval_ms: int (if hop), filter: ['mgmt','data','ctrl'] (optional), max_packets: int (optional) }".into(),
    );
    m.insert(
        "wifi.sniffer.stop".into(),
        "Stop Wi‑Fi packet sniffer and flush/send collected data. No params.".into(),
    );

    // nRF (2.4GHz) scanner / analyzer (based on nrf-scanner.ino / nrf-tools.ino)
    m.insert(
        "nrf.scan.start".into(),
        "Start nRF/2.4GHz channel scan. Params: { channel_start: int, channel_end: int, samples_per_channel: int (optional), dwell_ms: int (optional) }".into(),
    );
    m.insert(
        "nrf.scan.stop".into(),
        "Stop nRF scanning and return aggregated results. No params.".into(),
    );

    // Sub‑GHz receiver / CC1101 read (based on subghz_control.ino)
    m.insert(
        "subghz.read.start".into(),
        "Start sub‑GHz receiver (CC1101/LoRa). Params: { frequency_khz: int, modulation: string (eg. 'OOK','FSK','LoRa'), bandwidth_khz: int (optional), sample_rate: int (optional) }".into(),
    );
    m.insert(
        "subghz.read.stop".into(),
        "Stop sub‑GHz receiver. No params.".into(),
    );

    // Additional sub‑GHz UI actions (record/playback/packet send/disruptor)
    m.insert(
        "subghz.record.start".into(),
        "Begin recording raw sub‑GHz samples to local buffer. Params: { duration_ms: int (optional) }".into(),
    );
    m.insert(
        "subghz.record.stop".into(),
        "Stop recording and save buffer. No params.".into(),
    );
    m.insert(
        "subghz.playback.start".into(),
        "Play back a recorded sub‑GHz sample. Params: { id: int (recording id), repeats: int (optional) }".into(),
    );
    m.insert(
        "subghz.playback.stop".into(),
        "Stop playback. No params.".into(),
    );
    m.insert(
        "subghz.packet.send".into(),
        "Send a custom sub‑GHz packet. Params: { frequency_khz: int, modulation: string, payload: string }".into(),
    );
    m.insert(
        "subghz.disruptor.start".into(),
        "Start continuous noise/jamming on given frequency. Params: { frequency_khz: int, power_dbm: int (optional) }".into(),
    );
    m.insert(
        "subghz.disruptor.stop".into(),
        "Stop disruptor. No params.".into(),
    );

    // Oscilloscope / analog sampling (based on oscilloscope.ino)
    m.insert(
        "oscilloscope.start".into(),
        "Start ADC sampling stream. Params: { pin: int (ADC pin), sample_rate_hz: int, buffer_size: int (samples), trigger: object (optional) }".into(),
    );
    m.insert(
        "oscilloscope.stop".into(),
        "Stop ADC sampling/oscilloscope stream. No params.".into(),
    );

    // I2C periodic scanner (based on i2c-scanner.ino)
    m.insert(
        "i2c.scan.once".into(),
        "Perform a single I2C bus scan. Params: { sda_pin: int (optional), scl_pin: int (optional), address_start: int (optional), address_end: int (optional) }".into(),
    );
    m.insert(
        "i2c.scan.start".into(),
        "Begin periodic I2C scanning. Params: { interval_ms: int, address_range: [start,end] (optional) }".into(),
    );
    m.insert(
        "i2c.scan.stop".into(),
        "Stop periodic I2C scanning. No params.".into(),
    );

    // NFC polling (based on nfc-menu.ino)
    m.insert(
        "nfc.poll.start".into(),
        "Start polling for NFC tags. Params: { poll_interval_ms: int (optional) }".into(),
    );
    m.insert(
        "nfc.poll.stop".into(),
        "Stop NFC polling. No params.".into(),
    );

    // Sensor (accelerometer/gyroscope) streaming (matches 'start_sensor_listener' command)
    m.insert(
        "sensor.stream.start".into(),
        "Start sensor (accelerometer/gyro) streaming to host. Params: { sample_rate_hz: int (optional), sensors: ['accel','gyro','mag'] (optional) }".into(),
    );
    m.insert(
        "sensor.stream.stop".into(),
        "Stop sensor streaming. No params.".into(),
    );

    // Cell / cellular scan (if device supports it) — keep generic
    m.insert(
        "cell.scan.start".into(),
        "Start cellular scan / cell-info polling. Params: { duration_seconds: int (optional), frequency_bands: array (optional) }".into(),
    );
    m.insert(
        "cell.scan.stop".into(),
        "Stop cellular scanning. No params.".into(),
    );

    // SD / filesystem info and listings (based on files-menu.ino / sd-info.ino)
    m.insert(
        "sd.info".into(),
        "Return SD card stats once. No params.".into(),
    );
    m.insert(
        "files.list".into(),
        "Return a listing of files on SD (path param optional). Params: { path: string (optional) }".into(),
    );

    // IR receive (one-shot or continuous capture)
    m.insert(
        "ir.recv.start".into(),
        "Begin IR receive/capture mode. Params: { timeout_ms: int (optional) }".into(),
    );
    m.insert(
        "ir.recv.stop".into(),
        "Stop IR receive/capture. No params.".into(),
    );

    // Convenience / control commands
    m.insert(
        "list.paired.devices".into(),
        "Return list of currently paired/bonded Bluetooth devices. No params.".into(),
    );
    m.insert(
        "battery.info".into(),
        "Return current battery percentage / state. No params.".into(),
    );
    m.insert(
        "status.reporting.start".into(),
        "Begin periodic status reporting (battery, uptime, memory). Params: { interval_ms: int (default 5000) }".into(),
    );
    m.insert(
        "status.reporting.stop".into(),
        "Stop periodic status reporting. No params.".into(),
    );

    m
}

/// Initialize the global `BT_COMMANDS` map. Safe to call multiple times.
pub fn init_bt_commands() -> &'static HashMap<String, String> {
    BT_COMMANDS.get_or_init(make_bt_command_map)
}

/// Getter for the global commands map (returns a static reference).
pub fn bt_command_map() -> &'static HashMap<String, String> {
    init_bt_commands()
}
