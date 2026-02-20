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
use tauri_plugin_log::{Target, TargetKind};

mod bt;
use crate::bt::ensure_device_available;
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

/// Helper: obtain JNIEnv + Activity JObject from ndk_context.
/// The Activity pointer comes from `ndk_context::android_context().context()`,
/// which in Tauri Android is the real MainActivity – not Application context.
fn get_jni_and_activity() -> Result<(jni::JavaVM, JObject<'static>), jni::errors::Error> {
    let ctx = ndk_context::android_context();
    let vm = unsafe { jni::JavaVM::from_raw(ctx.vm().cast()) }?;
    // ctx.context() is the Activity pointer in Tauri Android
    let activity = unsafe { JObject::from_raw(ctx.context().cast()) };
    Ok((vm, activity))
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

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    // Ensure the global BT command table is populated at startup
    crate::bt::commands::init_bt_commands();

    info!("Starting Rust UI application");
    tauri::Builder::default()
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
            run_action
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
