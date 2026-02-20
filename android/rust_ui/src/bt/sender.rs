use crate::bt::commands::bt_command_map;
use log::{info, warn, error};
use std::collections::HashMap;
use std::sync::{Mutex, OnceLock};
use std::thread;
use std::time::Duration;
use serde_json::Value as JsonValue;

// Small global state that tracks the currently active "scan" command (if any).
static ACTIVE_SCAN: OnceLock<Mutex<Option<String>>> = OnceLock::new();

fn active_scan_lock() -> &'static Mutex<Option<String>> {
    ACTIVE_SCAN.get_or_init(|| Mutex::new(None))
}

/// Translate frontend UI action (data-action) into a standardized
/// Bluetooth command key (the keys defined in `commands.rs` / `commands.h`).
fn translate_action_to_bt(action: &str) -> Option<&'static str> {
    // Keep this mapping small and explicit — it MUST match the command keys
    // exposed by `commands.rs` (the latter is the single source of truth
    // for available BT commands).
    let map: HashMap<&str, &str> = [
        ("wifi-scanner", "wifi.scan.start"),
        ("wifi-scan", "wifi.scan.start"),
        ("wifi-scan-stop", "wifi.scan.stop"),
        ("wifi-sniffer", "wifi.sniffer.start"),
        ("wifi-sniffer-stop", "wifi.sniffer.stop"),
        ("ble-scanner", "ble.scan.start"),
        ("ble-scan-stop", "ble.scan.stop"),
        ("nrf-scanner", "nrf.scan.start"),
        ("nrf-scan-stop", "nrf.scan.stop"),
        ("subghz", "subghz.read.start"),
        ("sub-ghz-scanner", "subghz.read.start"),
        ("subghz-stop", "subghz.read.stop"),
        ("sub-ghz-playback", "subghz.playback.start"),
        ("sub-ghz-recorder", "subghz.record.start"),
        ("sub-ghz-packet-sender", "subghz.packet.send"),
        ("sub-ghz-disruptor", "subghz.disruptor.start"),
        ("oscilloscope", "oscilloscope.start"),
        ("oscilloscope-stop", "oscilloscope.stop"),
        ("i2c-scanner", "i2c.scan.once"),
        ("cell-scan", "cell.scan.start"),
        ("cell-scan-stop", "cell.scan.stop"),
        ("sd", "sd.info"),
        ("ir-recv", "ir.recv.start"),
        ("ir-stop", "ir.recv.stop"),
        ("battery", "battery.info"),
    ]
    .into_iter()
    .collect();

    map.get(action).copied()
}

/// Low-level send to the device — implemented by calling into
/// MainActivity.gattWriteCommand via JNI. Returns Ok on success or an
/// Err describing the failure. This keeps transport details on the
/// Android host while keeping command translation/validation in Rust.
fn send_to_device(mac: &str, bt_cmd: &str) -> Result<String, String> {
    // Try platform-specific transports in order of preference.

    // 1) JNI / Android transport (only compiled on Android targets)
    #[cfg(target_os = "android")]
    {
        info!("sending command '{}' to device {} (Android JNI)", bt_cmd, mac);
        let ctx = ndk_context::android_context();
        let vm = unsafe { jni::JavaVM::from_raw(ctx.vm().cast()) }
            .map_err(|e| format!("jni vm error: {:?}", e))?;
        let mut env = vm.attach_current_thread().map_err(|e| format!("attach err: {:?}", e))?;
        use jni::objects::JValue;
        use jni::objects::JObject;

        let activity = unsafe { JObject::from_raw(ctx.context().cast()) };
        let jmac = env.new_string(mac).map_err(|e| format!("jni new_string mac: {:?}", e))?;
        let jcmd = env.new_string(bt_cmd).map_err(|e| format!("jni new_string cmd: {:?}", e))?;
        let jmac_obj = JObject::from(jmac);
        let jcmd_obj = JObject::from(jcmd);

        match env
            .call_method(
                activity,
                "gattWriteCommand",
                "(Ljava/lang/String;Ljava/lang/String;)Z",
                &[JValue::Object(&jmac_obj), JValue::Object(&jcmd_obj)],
            )
            .map_err(|e| format!("jni call error: {:?}", e))?
            .z()
        {
            Ok(true) => return Ok(format!("sent:{}@{}", bt_cmd, mac)),
            Ok(false) => {
                let jni_err = format!("gatt_write_failed:{}@{}", bt_cmd, mac);
                // Fall through to Linux fallback below if available
                info!("Android GATT write reported failure, will attempt fallback: {}", jni_err);
            }
            Err(e) => {
                let jni_err = format!("jni_bool_convert_err:{:?}", e);
                info!("Android GATT write conversion error, will attempt fallback: {}", jni_err);
            }
        }
    }

    // After JNI failure: attempt the portable Linux transport implementation
    // (the function is available as a stub on non-Linux targets so calling it
    // here is always safe). Protect the call with catch_unwind so any
    // unexpected panic inside platform code is handled gracefully.
    info!("attempting fallback to Linux transport (if available)");
    match std::panic::catch_unwind(|| send_to_device_linux(mac, bt_cmd)) {
        Ok(Ok(ok)) => return Ok(ok),
        Ok(Err(err)) => info!("linux transport returned error: {}", err),
        Err(_) => info!("linux transport panicked — handled gracefully"),
    }

    // If we reach here, all transports failed or weren't supported.
    Err(format!("all_transports_failed_or_unsupported:{}@{}", bt_cmd, mac))
}

/// Linux-specific implementation using btleplug (async runtime).
#[cfg(target_os = "linux")]
#[cfg(target_os = "linux")]
fn send_to_device_linux(mac: &str, bt_cmd: &str) -> Result<String, String> {
    use btleplug::api::{Central, Manager as _, Peripheral as _, WriteType};
    use btleplug::platform::Manager;
    use uuid::Uuid;

    let mac_lc = mac.to_lowercase();

    // Build a small single-threaded runtime to run async btleplug APIs.
    let rt = tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()
        .map_err(|e| format!("tokio build error: {:?}", e))?;

    rt.block_on(async move {
        let manager = Manager::new().await.map_err(|e| format!("manager error: {:?}", e))?;
        let adapters = manager.adapters().await.map_err(|e| format!("adapters error: {:?}", e))?;
        let central = adapters.into_iter().next().ok_or_else(|| "no_ble_adapters_found".to_string())?;

        central
            .start_scan()
            .await
            .map_err(|e| format!("start_scan error: {:?}", e))?;
        // short delay to allow discovery
        tokio::time::sleep(Duration::from_millis(500)).await;

        let peripherals = central.peripherals().await;
        let maybe = peripherals
            .into_iter()
            .find(|p| p.address().to_string().to_lowercase() == mac_lc);
        let periph = maybe.ok_or_else(|| format!("peripheral_not_found:{}", mac))?;

        if !periph.is_connected().await.map_err(|e| format!("is_connected err:{:?}", e))? {
            periph.connect().await.map_err(|e| format!("connect err:{:?}", e))?;
        }
        periph.discover_services().await.map_err(|e| format!("discover err:{:?}", e))?;

        let chars = periph.characteristics();
        let target_uuid = Uuid::parse_str("2a73f2c0-1fbd-459e-8fcc-c5c9c331914c").map_err(|e| format!("uuid parse: {:?}", e))?;
        let maybe_char = chars.into_iter().find(|c| c.uuid == target_uuid);
        let ch = maybe_char.ok_or_else(|| "char_not_found".to_string())?;

        periph
            .write(&ch, bt_cmd.as_bytes(), WriteType::WithResponse)
            .await
            .map_err(|e| format!("write error:{:?}", e))?;

        Ok(format!("sent:{}@{}", bt_cmd, mac))
    })
}

#[cfg(not(target_os = "linux"))]
fn send_to_device_linux(_mac: &str, _bt_cmd: &str) -> Result<String, String> {
    Err("linux_bluetooth_not_supported_on_this_target".to_string())
}



/// Public API used by `run_action` — translates, validates, stops any
/// active scan, and sends the requested command. If a different scan is
/// active it will be stopped first (stop command sent) and we wait 5ms
/// before sending the new start command.
pub fn send_action(action: &str, mac: &str, params: Option<JsonValue>) -> Result<String, String> {
    info!("send_action: action='{}' mac='{}' params={:?}", action, mac, params);

    // Translate UI action -> BT command key
    let bt_cmd = match translate_action_to_bt(action) {
        Some(c) => c.to_string(),
        None => {
            warn!("Unknown UI action: {}", action);
            return Err(format!("unknown_action:{}", action));
        }
    };

    // Validate the translated command exists in the canonical map
    let canonical = bt_command_map();
    if !canonical.contains_key(&bt_cmd) {
        error!("Translated BT command '{}' not present in canonical commands map", bt_cmd);
        return Err(format!("unsupported_bt_command:{}", bt_cmd));
    }

    let is_start_cmd = bt_cmd.ends_with(".start") || bt_cmd.ends_with(".start\"");

    // If there's an active scan and it's different from the requested start,
    // first send a stop for that scan
    if is_start_cmd {
        let mut guard = active_scan_lock().lock().unwrap();
        if let Some(active) = guard.as_ref() {
            if active != &bt_cmd {
                // compute stop command by replacing `.start` with `.stop` when possible
                let stop_cmd = if active.ends_with(".start") {
                    active.replacen(".start", ".stop", 1)
                } else {
                    // fallback: try append .stop
                    format!("{}.stop", active)
                };
                info!("Active scan '{}' detected — sending stop ('{}') before new start", active, stop_cmd);
                // send stop (via JNI-backed helper)
                let _ = send_to_device(mac, &stop_cmd);
                // clear active state
                *guard = None;
                // small delay per spec
                thread::sleep(Duration::from_millis(5));
            }
        }
        drop(guard);
    }

    // If params were supplied, send a JSON-style command payload so the
    // firmware can deserialize `command` + `params` (events_process_one
    // understands that format). Otherwise send the plain canonical key.
    let payload_to_send = if let Some(p) = params {
        let wrapper = serde_json::json!({ "command": bt_cmd, "params": p });
        serde_json::to_string(&wrapper).map_err(|e| format!("json_serialize_err:{:?}", e))?
    } else {
        bt_cmd.clone()
    };

    // Send requested command via JNI-backed helper
    let result = send_to_device(mac, &payload_to_send)?;

    // Update active scan state if necessary (use canonical key)
    if is_start_cmd {
        let mut guard = active_scan_lock().lock().unwrap();
        *guard = Some(bt_cmd.clone());
    } else if bt_cmd.ends_with(".stop") {
        // if stop corresponds to active, clear it
        let mut guard = active_scan_lock().lock().unwrap();
        if let Some(active) = guard.as_ref() {
            let active_stop = if active.ends_with(".start") { active.replacen(".start", ".stop", 1) } else { format!("{}.stop", active) };
            if active_stop == bt_cmd {
                *guard = None;
            }
        }
    }

    Ok(result)
}
