#![cfg_attr(
    all(not(debug_assertions), target_os = "windows"),
    windows_subsystem = "windows"
)]
use jni::objects::JObject;
use jni::JNIEnv;
use log::{info, error};
use serde::de;
use std::thread;
use std::time::Duration;
use tauri::Emitter;
use tauri::Manager;
use tauri_plugin_log::{Target, TargetKind};

mod bt;
use crate::bt::ensure_device_available;

// radio/algorithm helpers
mod rf {
    pub mod algorithms;
}

mod wifi_tools;
mod recorder_store;
mod pg_logger;
mod mqtt_publisher;
// Placeholder for sensor data structure
#[derive(Clone, serde::Serialize)]
struct SensorData {
    x: f32,
    y: f32,
    z: f32,
    timestamp: u64,
}

// Cell-scan result (simulated). Will be emitted to the frontend as `cell-scan-result` events.
#[derive(Clone, serde::Serialize)]
struct CellScanResult {
    mcc: u16,
    mnc: u16,
    cid: u64,
    signal_dbm: i32,
    timestamp: u64,
}

/// Helper: obtain JNIEnv + Activity JObject from tao's Android context.
/// Tauri 2 / tao stores the VM+activity in its own CONTEXTS map; ndk-context is never
/// populated by this stack, so we go directly to tao.
fn get_jni_and_activity() -> Result<(jni::JavaVM, JObject<'static>), jni::errors::Error> {
    #[cfg(target_os = "android")]
    {
        let ctx = tauri::tao::platform::android::prelude::main_android_context()
            .ok_or(jni::errors::Error::JniCall(jni::errors::JniError::Unknown))?;
        let vm = unsafe { jni::JavaVM::from_raw(ctx.java_vm.cast()) }?;
        let activity = unsafe { JObject::from_raw(ctx.context_jobject.cast()) };
        return Ok((vm, activity));
    }
    #[cfg(not(target_os = "android"))]
    Err(jni::errors::Error::JniCall(jni::errors::JniError::Unknown))
}
#[tauri::command]
fn run_action(action: &str, macaddy: &str, params: Option<String>) -> Result<String, String> {
    info!("[run_action] Received action: {} mac={} params={:?}", action, macaddy, params);

    // If params were provided, parse to JSON Value and forward to sender.
    let params_json = match params {
        Some(s) => match serde_json::from_str(&s) {
            Ok(v) => Some(v),
            Err(e) => {
                error!("[run_action] invalid params JSON: {:?}", e);
                return Err(format!("invalid_params_json:{:?}", e));
            }
        },
        None => None,
    };

    match crate::bt::sender::send_action(action, macaddy, params_json) {
        Ok(r) => Ok(r),
        Err(e) => Err(e),
    }
}

// new helper command for executing algorithms implemented in Rust
#[tauri::command]
fn run_algorithm(name: &str, params: Option<String>) -> Result<String, String> {
    use crate::rf::algorithms;
    info!("[run_algorithm] {} params={:?}", name, params);
    let result = match name {
        "algo-symbol-timing" => {
            // params might contain base64-encoded float samples or similar; ignore for now
            algorithms::symbol_timing_estimator(&[]).map(|v| v.to_string())
        }
        "algo-crc-recovery" => algorithms::crc_recovery_stub(&[]),
        "algo-burst-cluster" => algorithms::burst_clustering(&[], &[]).map(|v| v.to_string()),
        "algo-entropy" => algorithms::payload_entropy(&[]).map(|v| v.to_string()),
        "algo-fingerprint" => algorithms::protocol_fingerprint(&[]),
        "algo-fec-recover" => algorithms::fec_recovery_attempt(&[]).map(|v| base64::encode(v)),
        "algo-blind-rate" => algorithms::blind_rate_estimate(&[]).map(|v| format!("{:?}", v)),
        "algo-lora-detect" => algorithms::detect_lora_frames(&[]).map(|v| v.to_string()),
        "algo-mod-ranker" => algorithms::modulation_ranker(&[]).map(|v| format!("{:?}", v)),
        "algo-repeater-detector" => algorithms::repetition_detector(&[], &[]).map(|v| format!("{:?}", v)),
        "algo-aes128" => algorithms::aes128_ecb_decrypt(&[0u8;16], &[]).map(|v| base64::encode(v)),
        "algo-caesar" => Ok(algorithms::caesar_cipher_decrypt("hello", 3)),
        _ => Err("unknown_algorithm"),
    };
    result.map_err(|e| e.to_string())
}

// HTTPS decryption helper command
#[tauri::command]
fn decrypt_https(data_b64: &str, key_b64: &str) -> Result<String, String> {
    // data and key are base64 encoded strings from the frontend
    let data = base64::decode(data_b64).map_err(|e| format!("data decode: {:?}", e))?;
    let key = base64::decode(key_b64).map_err(|e| format!("key decode: {:?}", e))?;
    let out = crate::wifi_tools::decrypt_https_packets(&data, &key);
    Ok(base64::encode(out))
}

#[tauri::command]
fn trigger_bluetooth_connection_screen(macaddy: &str) -> Vec<Vec<String>> {
    info!(
        "[trigger_bluetooth_connection_screen] Requesting Bluetooth connection for MAC: {}",
        macaddy
    );
    let (vm, activity) = match get_jni_and_activity() {
        Ok(pair) => pair,
        Err(e) => {
            error!("[trigger_bluetooth_connection_screen] Failed to get JNI/activity: {:?}", e);
            return Vec::new();
        }
    };
    let mut env = vm.attach_current_thread().unwrap();
    info!("[trigger_bluetooth_connection_screen] got env and activity");

    // Request runtime BT permissions first (Android 12+)
    match bt::request_bt_permissions(&mut env, &activity) {
        Ok(true) => info!("[trigger_bluetooth_connection_screen] BT permissions already granted"),
        Ok(false) => {
            info!("[trigger_bluetooth_connection_screen] BT permissions requested – user must grant, then retry");
            return Vec::new(); // return early; front-end should retry after grant
        }
        Err(e) => {
            error!("[trigger_bluetooth_connection_screen] Permission check failed: {:?}", e);
            // continue anyway – maybe older Android
        }
    }

    let mac_opt = if macaddy.is_empty() { None } else { Some(macaddy) };
    let mut output = Vec::new();
    match ensure_device_available(&mut env, &activity, mac_opt) {
        Ok(devices) => {
            for device in devices {
                info!("Device: {} - {}", device.name, device.address);
                output.push(vec![
                    device.name,
                    device.address,
                    device.vendor.unwrap_or_else(|| "Unknown".to_string()),
                    device.bonded.to_string(),
                ]);
            }
            output
        }
        Err(e) => {
            error!("Error in Bluetooth connection flow: {:?}", e);
            if env.exception_check().unwrap_or(false) {
                env.exception_describe().ok();
                env.exception_clear().ok();
            }
            Vec::new()
        }
    }
}
// Command macro to start listening
#[tauri::command]
fn start_sensor_listener(window: tauri::Window) {
    // Determine if we are on Android
    #[cfg(target_os = "android")]
    {
        // On Android, we would initialize NDK sensors here.
        // For now, let's simulate sensor data pumping.
        // In a real app, you'd use `ndk::sensor` or JNI.
        // See https://docs.rs/ndk/latest/ndk/sensor/index.html

        let window_clone = window.clone();
        thread::spawn(move || {
            let mut x: f32 = 0.0;
            loop {
                // Simulate data
                x += 0.1_f32;
                let data = SensorData {
                    x: x.sin(),
                    y: (x * 0.5_f32).cos(),
                    z: x.tan().max(-1.0_f32).min(1.0_f32),
                    timestamp: std::time::SystemTime::now()
                        .duration_since(std::time::UNIX_EPOCH)
                        .unwrap_or_default()
                        .as_millis() as u64,
                };

                // Emit event to frontend
                if let Err(_) = window_clone.emit("sensor-update", &data) {
                    break;
                }

                thread::sleep(Duration::from_millis(100)); // 10Hz
            }
        });
    }

    #[cfg(not(target_os = "android"))]
    {
        println!("Not running on Android, simulating sensors anyway.");
        let window_clone = window.clone();
        thread::spawn(move || {
            let mut x: f32 = 0.0;
            loop {
                x += 0.1_f32;
                let data = SensorData {
                    x: x.sin(),
                    y: (x * 0.5_f32).cos(),
                    z: 0.0,
                    timestamp: std::time::SystemTime::now()
                        .duration_since(std::time::UNIX_EPOCH)
                        .unwrap_or_default()
                        .as_millis() as u64,
                };
                if let Err(_) = window_clone.emit("sensor-update", &data) {
                    break;
                }
                thread::sleep(Duration::from_millis(100));
            }
        });
    }
}

#[tauri::command]
fn cell_scan_start(window: tauri::Window) -> Result<(), String> {
    // On Android: placeholder native stub (TODO: implement real TelephonyManager/JNI scan).
    // Emits a single marker result immediately so the frontend can switch to native flow.
    #[cfg(target_os = "android")]
    {
        let window_clone = window.clone();
        thread::spawn(move || {
            let now = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_millis() as u64;
            // Emit a single marker result to indicate native stub was invoked.
            let res = CellScanResult {
                mcc: 0,
                mnc: 0,
                cid: 0,
                signal_dbm: -999,
                timestamp: now,
            };
            let _ = window_clone.emit("cell-scan-result", &res);
            let _ = window_clone.emit("cell-scan-status", &"native-stub-started");
        });
        return Ok(());
    }

    // Non-Android: keep existing simulated behavior for desktop/dev.
    #[cfg(not(target_os = "android"))]
    {
        let window_clone = window.clone();
        thread::spawn(move || {
            for i in 0..8u64 {
                let res = CellScanResult {
                    mcc: 310,
                    mnc: 260,
                    cid: 1000 + i,
                    signal_dbm: -60 - (i as i32),
                    timestamp: std::time::SystemTime::now()
                        .duration_since(std::time::UNIX_EPOCH)
                        .unwrap_or_default()
                        .as_millis() as u64,
                };
                let _ = window_clone.emit("cell-scan-result", &res);
                thread::sleep(Duration::from_millis(500));
            }
        });
        return Ok(());
    }
}

#[tauri::command]
fn request_permissions() -> Result<bool, String> {
    info!("[request_permissions] Requesting all BT permissions via JNI");
    let (vm, activity) = get_jni_and_activity().map_err(|e| format!("JNI init: {:?}", e))?;
    let mut env = vm.attach_current_thread().map_err(|e| format!("attach: {:?}", e))?;
    bt::request_bt_permissions(&mut env, &activity).map_err(|e| format!("perm req: {:?}", e))
}

#[tauri::command]
fn sniffer_session_append(entries: Vec<recorder_store::SnifferPacketInput>) -> Result<recorder_store::SnifferSessionStats, String> {
    recorder_store::append_packets(entries)
}

#[tauri::command]
fn sniffer_session_page(offset: i64, limit: i64, min_rssi: i64, min_len: i64) -> Result<recorder_store::SnifferPage, String> {
    recorder_store::page_packets(offset, limit, min_rssi, min_len)
}

#[tauri::command]
fn sniffer_session_get(id: i64) -> Result<Option<recorder_store::SnifferRecord>, String> {
    recorder_store::get_packet(id)
}

#[tauri::command]
fn sniffer_session_clear() -> Result<(), String> {
    recorder_store::clear_packets()
}

#[tauri::command]
fn sniffer_session_export_csv(path: &str, min_rssi: i64, min_len: i64) -> Result<recorder_store::SnifferSessionStats, String> {
    recorder_store::export_packets_csv(path, min_rssi, min_len)
}

#[tauri::command]
fn sniffer_session_stats() -> Result<recorder_store::SnifferSessionStats, String> {
    recorder_store::session_stats()
}

/// Connect to a PostgreSQL server and create the three signal tables.
/// Called from the Settings menu when the user saves PG credentials.
#[tauri::command]
async fn set_pg_settings(
    host: String,
    port: u16,
    database: String,
    username: String,
    password: String,
) -> Result<String, String> {
    pg_logger::connect(&host, port, &database, &username, &password).await?;
    Ok(format!("Connected to PostgreSQL at {host}:{port}/{database}"))
}

/// Connect to an MQTT broker.
/// Called from the Settings menu when the user saves MQTT credentials.
#[tauri::command]
async fn set_mqtt_settings(
    host: String,
    port: u16,
    username: String,
    password: String,
    client_id: String,
) -> Result<String, String> {
    let user = if username.is_empty() { None } else { Some(username.as_str()) };
    let pass = if password.is_empty() { None } else { Some(password.as_str()) };
    let cid = if client_id.is_empty() { "sharkos" } else { &client_id };
    mqtt_publisher::connect(&host, port, cid, user, pass).await?;
    Ok(format!("Connected to MQTT broker at {host}:{port}"))
}

/// Log a captured signal to PostgreSQL and publish it to MQTT.
/// `signal_type` is one of "wifi", "subghz", or "bluetooth".
/// `signal_json` is a JSON string whose fields depend on the type:
///   wifi     – frequency_mhz, channel, rssi, ssid, extra, payload_b64
///   subghz   – frequency_mhz, modulation, rssi, data_length, raw_data, module_id
///   bluetooth– frequency_mhz, channel, rssi, device_name, extra, payload_b64
#[tauri::command]
async fn log_signal_external(signal_type: String, signal_json: String) -> Result<(), String> {
    // Parse JSON – if it fails just silently drop (non-critical path)
    let v: serde_json::Value = match serde_json::from_str(&signal_json) {
        Ok(v) => v,
        Err(_) => return Ok(()),
    };

    let freq      = v["frequency_mhz"].as_f64().unwrap_or(0.0);
    let rssi      = v["rssi"].as_i64().unwrap_or(0) as i32;

    match signal_type.as_str() {
        "wifi" => {
            let channel     = v["channel"].as_i64().unwrap_or(0) as i32;
            let ssid        = v["ssid"].as_str().unwrap_or("");
            let extra       = v["extra"].as_str().unwrap_or("");
            let payload_b64 = v["payload_b64"].as_str().unwrap_or("");
            let _ = pg_logger::log_wifi(freq, channel, rssi, ssid, extra, payload_b64).await;
            let _ = mqtt_publisher::publish("sharkos/wifi/packets", &signal_json).await;
        }
        "subghz" => {
            let modulation  = v["modulation"].as_str().unwrap_or("unknown");
            let data_length = v["data_length"].as_i64().unwrap_or(0) as i32;
            let raw_data    = v["raw_data"].as_str().unwrap_or("");
            let module_id   = v["module_id"].as_i64().unwrap_or(0) as i32;
            let _ = pg_logger::log_subghz(freq, modulation, rssi, data_length, raw_data, module_id).await;
            let _ = mqtt_publisher::publish("sharkos/subghz/packets", &signal_json).await;
        }
        "bluetooth" => {
            let channel     = v["channel"].as_i64().unwrap_or(0) as i32;
            let device_name = v["device_name"].as_str().unwrap_or("");
            let extra       = v["extra"].as_str().unwrap_or("");
            let payload_b64 = v["payload_b64"].as_str().unwrap_or("");
            let _ = pg_logger::log_bluetooth(freq, channel, rssi, device_name, extra, payload_b64).await;
            let _ = mqtt_publisher::publish("sharkos/bluetooth/packets", &signal_json).await;
        }
        _ => {}
    }
    Ok(())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    // Ensure the global BT command table is populated at startup
    crate::bt::commands::init_bt_commands();

    info!("Starting Rust UI application");
    tauri::Builder::default()
        .setup(|app| {
            // initialize BT listener on startup so it can accept appended signals
            crate::bt::listener::init(&app.handle());
            match app.path().app_cache_dir() {
                Ok(cache_dir) => {
                    let recorder_dir = cache_dir.join("sniffer-session");
                    if let Err(err) = crate::recorder_store::init_session_store(recorder_dir) {
                        error!("failed to initialize recorder store: {}", err);
                    }
                }
                Err(err) => {
                    error!("failed to resolve app cache dir for recorder store: {}", err);
                }
            }
            Ok(())
        })
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .plugin(
            tauri_plugin_log::Builder::new()
                .level(tauri_plugin_log::log::LevelFilter::Info)
                .build(),
        )
        .plugin(tauri_plugin_shell::init())
        .invoke_handler(tauri::generate_handler![
            start_sensor_listener,
            cell_scan_start,
            trigger_bluetooth_connection_screen,
            request_permissions,
            run_action,
            run_algorithm,
            decrypt_https,
            sniffer_session_append,
            sniffer_session_page,
            sniffer_session_get,
            sniffer_session_clear,
            sniffer_session_export_csv,
            sniffer_session_stats,
            crate::bt::listener::bt_listener_append,
            set_pg_settings,
            set_mqtt_settings,
            log_signal_external,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
