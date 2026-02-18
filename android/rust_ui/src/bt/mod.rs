use jni::objects::{JObject, JObjectArray, JString, JValue};
use jni::JNIEnv;
use log::{info, error};
use std::sync::{Arc, Condvar, Mutex};
use std::time::Duration;
pub mod addresses;
#[derive(Debug, Clone)]
pub struct DeviceInfo {
    pub name: String,
    pub address: String, // MAC address
    pub vendor: Option<String>,
    pub bonded: bool,
}
fn wait_for_device_bond(
    _env: &mut JNIEnv,
    _target_mac: &str,
    timeout_ms: u64,
) -> Result<bool, jni::errors::Error> {
    // Create a shared state for signaling
    let pair = Arc::new((Mutex::new(false), Condvar::new()));
    let pair_clone = pair.clone();

    // 1. Create a BroadcastReceiver in Java that filters for the target MAC
    //    and checks bond state changes.
    //    This requires creating a custom Java class or using a dynamic proxy.
    //    For brevity, we outline the steps:

    // - Create an IntentFilter for BluetoothDevice.ACTION_BOND_STATE_CHANGED
    // - Register the receiver with the activity's context
    // - In the receiver's onReceive, check if the device's address matches target_mac
    //   and the new bond state is BluetoothDevice.BOND_BONDED (i.e., 12).
    // - If so, set the shared flag and notify the condition variable.

    // 2. Wait on the condition variable with timeout.
    let (lock, cvar) = &*pair_clone;
    let started = lock.lock().unwrap();
    let result = cvar
        .wait_timeout(started, Duration::from_millis(timeout_ms))
        .unwrap();

    // 3. Unregister the receiver.

    Ok(*result.0) // returns true if bonded, false if timeout
}
fn get_bonded_devices(env: &mut JNIEnv) -> Result<Vec<DeviceInfo>, jni::errors::Error> {
    // Get BluetoothAdapter
    info!("Checking bonded Bluetooth devices...");
    let adapter_class = env.find_class("android/bluetooth/BluetoothAdapter")?;
    info!("Found BluetoothAdapter class");
    let adapter = env
        .call_static_method(
            adapter_class,
            "getDefaultAdapter",
            "()Landroid/bluetooth/BluetoothAdapter;",
            &[],
        )?
        .l()?;
    info!("Obtained BluetoothAdapter object (is_null={})", adapter.is_null());
    if adapter.is_null() {
        return Ok(vec![]); // Bluetooth not supported
    }
    info!("BluetoothAdapter is available, retrieving bonded devices...");
    // Call getBondedDevices() which returns a Set<BluetoothDevice>
    let result = env.call_method(adapter, "getBondedDevices", "()Ljava/util/Set;", &[]);
    
    if let Err(e) = &result {
        // If we hit a fast failure (like SecurityException), log it and return empty
        error!("Failed to call getBondedDevices (likely missing BLUETOOTH_CONNECT permission): {:?}", e);
        // We can check if it's a Java exception
        if env.exception_check().unwrap_or(false) {
             env.exception_describe().ok();
             env.exception_clear().ok();
        }
        return Ok(vec![]);
    }

    let bonded_set = result?.l()?;
    info!(
        "Retrieved bonded devices set (is_null={})",
        bonded_set.is_null()
    );
    // Convert Set to array
    let device_array = env
        .call_method(bonded_set, "toArray", "()[Ljava/lang/Object;", &[])?
        .l()?;
    info!(
        "Converted bonded devices set to array (is_null={})",
        device_array.is_null()
    );
    // wrap into a JObjectArray so we can call array helpers
    let device_array = JObjectArray::from(device_array);

    let array_len = env.get_array_length(&device_array)?;

    // Iterate over array
    let mut devices = Vec::new();
    for i in 0..array_len {
        let device_obj = env.get_object_array_element(&device_array, i)?; // handle array properly
        let device = JObject::from(device_obj);

        // Get device name
        let name_jstring = env
            .call_method(&device, "getName", "()Ljava/lang/String;", &[])?
            .l()?;
        let name: String = if !name_jstring.is_null() {
            env.get_string(&JString::from(name_jstring))?.into()
        } else {
            "Unknown".to_string()
        };

        // Get device address
        let addr_jstring = env
            .call_method(&device, "getAddress", "()Ljava/lang/String;", &[])?
            .l()?;
        let address = env.get_string(&JString::from(addr_jstring)).map(|s| s.into()).unwrap_or_else(|_| "Unknown".to_string());
        info!("Found bonded device: {} ({})", name, address);

        let vendor = crate::bt::addresses::vendor_for_mac(&address).map(|s| s.to_string());

        devices.push(DeviceInfo { name, address: address, vendor, bonded: true });
    }

    Ok(devices)
}
fn is_device_bonded(env: &mut JNIEnv, target_mac: &str) -> Result<bool, jni::errors::Error> {
    info!("Checking if device with MAC {} is already bonded...", target_mac);
    let devices = get_bonded_devices(env)?;
    let bonded = devices.iter().any(|d| d.address == target_mac);
    if bonded {
        info!("Device with MAC {} is already bonded.", target_mac);
    } else {
        info!("Device with MAC {} is not bonded.", target_mac);
    }
    Ok(bonded)
}
fn ensure_bluetooth_enabled(
    env: &mut JNIEnv,
    activity: &JObject,
) -> Result<(), jni::errors::Error> {
    // Get BluetoothAdapter
    info!("ensure_bluetooth_enabled: locating BluetoothAdapter class");
    let adapter_class = env.find_class("android/bluetooth/BluetoothAdapter")?;
    info!("ensure_bluetooth_enabled: found adapter class");

    let adapter = env
        .call_static_method(
            &adapter_class,
            "getDefaultAdapter",
            "()Landroid/bluetooth/BluetoothAdapter;",
            &[],
        )?
        .l()?;
    info!(
        "ensure_bluetooth_enabled: obtained adapter object (is_null={})",
        adapter.is_null()
    );

    if adapter.is_null() {
        info!("ensure_bluetooth_enabled: adapter is null -> no Bluetooth available");
        return Ok(()); // no Bluetooth
    }

    // Check if enabled
    info!("ensure_bluetooth_enabled: checking if adapter is enabled");
    let enabled = env.call_method(adapter, "isEnabled", "()Z", &[])?.z()?;
    info!("ensure_bluetooth_enabled: adapter.isEnabled() -> {}", enabled);

    if !enabled {
        info!("ensure_bluetooth_enabled: adapter not enabled, creating intent to request enabling");
        // Create intent to request enabling
        let intent_class = env.find_class("android/content/Intent")?;
        info!("ensure_bluetooth_enabled: found Intent class");
        let action_field = env.get_static_field(
            &adapter_class,
            "ACTION_REQUEST_ENABLE",
            "Ljava/lang/String;",
        )?;
        info!("ensure_bluetooth_enabled: retrieved ACTION_REQUEST_ENABLE field");
        let action = action_field.l()?;
        info!(
            "ensure_bluetooth_enabled: ACTION_REQUEST_ENABLE object obtained (is_null={})",
            action.is_null()
        );
        let intent = env.new_object(
            intent_class,
            "(Ljava/lang/String;)V",
            &[JValue::from(&action)],
        )?;
        info!("ensure_bluetooth_enabled: created Intent object");

        // Start activity
        info!("ensure_bluetooth_enabled: calling startActivity(...)");
        env.call_method(
            activity,
            "startActivity",
            "(Landroid/content/Intent;)V",
            &[JValue::from(&intent)],
        )?;
        info!("ensure_bluetooth_enabled: startActivity returned");

        // Note: this returns immediately; the user must enable Bluetooth manually.
        info!("ensure_bluetooth_enabled: returned after startActivity; user must enable Bluetooth");
        // For true synchronous behavior, you'd need to wait for result (see next sections).
    } else {
        info!("ensure_bluetooth_enabled: adapter already enabled, nothing to do");
    }

    info!("ensure_bluetooth_enabled: completed");
    Ok(())
}
fn show_bluetooth_settings(env: &mut JNIEnv, activity: &JObject) -> Result<(), jni::errors::Error> {
    let settings_class = env.find_class("android/provider/Settings")?;
    let action_field = env.get_static_field(
        &settings_class,
        "ACTION_BLUETOOTH_SETTINGS",
        "Ljava/lang/String;",
    )?;
    let action = action_field.l()?;

    let intent_class = env.find_class("android/content/Intent")?;
    let intent = env.new_object(
        intent_class,
        "(Ljava/lang/String;)V",
        &[JValue::from(&action)],
    )?;

    env.call_method(
        activity,
        "startActivity",
        "(Landroid/content/Intent;)V",
        &[JValue::from(&intent)],
    )?;

    Ok(())
}
/// Check whether Bluetooth runtime permissions are granted (Android 12+ / API 31+).
/// Returns `Ok(true)` if both BLUETOOTH_CONNECT and BLUETOOTH_SCAN are granted,
/// `Ok(false)` otherwise.
/// Actual permission *requesting* is done on the frontend via the AndroidBridge
/// JS interface before this Rust code is ever called.
pub fn request_bt_permissions(
    env: &mut JNIEnv,
    activity: &JObject,
) -> Result<bool, jni::errors::Error> {
    info!("request_bt_permissions: checking runtime BT permissions");

    let helper = env.find_class("com/sharkos/PermissionHelper")?;

    let perms_json = env.new_string("[\"android.permission.BLUETOOTH_CONNECT\",\"android.permission.BLUETOOTH_SCAN\"]")?;
    // Parse JSON to String[] in Kotlin would be complex; instead just check each one individually via ContextCompat
    let context_compat = env.find_class("androidx/core/content/ContextCompat")?;
    let bt_connect = env.new_string("android.permission.BLUETOOTH_CONNECT")?;
    let bt_scan = env.new_string("android.permission.BLUETOOTH_SCAN")?;

    // checkSelfPermission returns 0 (GRANTED) or -1 (DENIED)
    let connect_status = env
        .call_static_method(
            &context_compat,
            "checkSelfPermission",
            "(Landroid/content/Context;Ljava/lang/String;)I",
            &[JValue::from(activity), JValue::from(&bt_connect)],
        )?
        .i()?;
    info!("request_bt_permissions: BLUETOOTH_CONNECT status = {}", connect_status);

    let scan_status = env
        .call_static_method(
            &context_compat,
            "checkSelfPermission",
            "(Landroid/content/Context;Ljava/lang/String;)I",
            &[JValue::from(activity), JValue::from(&bt_scan)],
        )?
        .i()?;
    info!("request_bt_permissions: BLUETOOTH_SCAN status = {}", scan_status);

    let granted = connect_status == 0 && scan_status == 0;
    info!("request_bt_permissions: all granted = {}", granted);
    Ok(granted)
}

pub fn ensure_device_available(
    env: &mut JNIEnv,
    activity: &JObject,
    target_mac: Option<&str>,
) -> Result<Vec<DeviceInfo>, jni::errors::Error> {
    // Step 1: Ensure Bluetooth is enabled (optional)
    info!("Ensuring Bluetooth is enabled...");
    ensure_bluetooth_enabled(env, activity)?;
    info!("Bluetooth enabled check complete.");
    // Step 2: Check if target is already bonded
    if let Some(mac) = target_mac {
        info!("Checking if device with MAC {} is already bonded...", mac);
        if is_device_bonded(env, mac)? {
            info!("Device with MAC {} is already bonded.", mac);
            return Ok(vec![]); // Already bonded, silent return
        }
    }
    info!("Target device not bonded yet (or no target specified), prompting user...");
    // Step 3: We need to prompt the user
    show_bluetooth_settings(env, activity)?;
    info!("User prompted to enable Bluetooth and pair devices. Waiting for bonding if target MAC specified...");
    // Step 4: If a specific MAC was provided, wait for it to become bonded
    if let Some(mac) = target_mac {
        // Wait for up to 60 seconds for the user to pair the device
        info!("Waiting for device with MAC {} to become bonded (timeout 60s)...", mac);
        let bonded = wait_for_device_bond(env, mac, 60000)?;
        if !bonded {
            // Timeout or user didn't pair the target; return all bonded devices
            info!("Device with MAC {} did not become bonded within timeout.", mac);
            return get_bonded_devices(env);
        }
    }

    // Step 5: Return all bonded devices (after prompt)
    get_bonded_devices(env)
}
