use std::fs::{File, OpenOptions};
use std::io::Write;
use std::sync::{mpsc::{channel, Sender}, OnceLock};
use std::thread;
use std::time::{SystemTime, UNIX_EPOCH};

use serde_json::Value;
use tauri::{AppHandle, Manager};

static APP_SENDER: OnceLock<Sender<String>> = OnceLock::new();

/// Initialize the background listener. Call once during app setup.
pub fn init(app: &AppHandle) {
  // If already initialized, skip
  if APP_SENDER.get().is_some() {
    log::info!("bt::listener already initialized");
    return;
  }

  let (tx, rx) = channel::<String>();
  if APP_SENDER.set(tx).is_err() {
    log::error!("failed to set APP_SENDER");
    return;
  }

  // Spawn background thread that receives JSON payload strings and writes CSVs
  let app_clone = app.clone();
  thread::spawn(move || {
    log::info!("bt::listener thread started");

    // state for CSV batching
    let mut current_module: Option<i64> = None;
    let mut current_file: Option<File> = None;
    let mut current_path: Option<std::path::PathBuf> = None;

    while let Ok(msg) = rx.recv() {
      // Try JSON first, falling back to a framed protobuf base64 form.
      let parsed_json: Result<Value, _> = serde_json::from_str(&msg);
      let v_opt = if parsed_json.is_ok() {
        Some(parsed_json.unwrap())
      } else {
        // Not JSON: check for PROTO:<base64> framing (sent by firmware)
        if msg.starts_with("PROTO:") {
          let b64 = &msg[6..];
          match base64::decode(b64) {
            Ok(bytes) => {
              // strip optional 0xAA55 + u16 length framing
              let mut cursor = 0usize;
              if bytes.len() >= 4 && bytes[0] == 0xAA && bytes[1] == 0x55 {
                let len = (bytes[2] as usize) | ((bytes[3] as usize) << 8);
                cursor = 4;
                if cursor + len > bytes.len() { cursor = 4; }
              }
              let body = &bytes[cursor..];
              // Parse minimal protobuf fields for RadioSignal
              let mut i = 0usize;
              let mut obj = serde_json::Map::new();
              while i < body.len() {
                // read varint tag
                let mut shift = 0u32;
                let mut tag: u64 = 0;
                loop {
                  if i >= body.len() { break; }
                  let b = body[i]; i += 1;
                  tag |= ((b & 0x7F) as u64) << shift;
                  if (b & 0x80) == 0 { break; }
                  shift += 7;
                }
                let field = (tag >> 3) as u32;
                let wire = (tag & 0x7) as u8;
                match wire {
                  0 => { // varint
                    // read varint value
                    let mut shift = 0u32; let mut val: u64 = 0;
                    loop {
                      if i >= body.len() { break; }
                      let b = body[i]; i += 1;
                      val |= ((b & 0x7F) as u64) << shift;
                      if (b & 0x80) == 0 { break; }
                      shift += 7;
                    }
                    match field {
                      1 => { obj.insert("timestamp_ms".into(), Value::from(val)); }
                      2 => { obj.insert("module".into(), Value::from(val as i64)); }
                      4 => {
                        // ZigZag-decode RSSI (sint) encoded as varint
                        let rssi = ((val >> 1) as i64) ^ -((val & 1) as i64);
                        obj.insert("rssi".into(), Value::from(rssi));
                      }
                      _ => {}
                    }
                  }
                  5 => { // 32-bit
                    if i + 4 <= body.len() {
                      let b0 = body[i]; let b1 = body[i+1]; let b2 = body[i+2]; let b3 = body[i+3];
                      i += 4;
                      let u = ((b3 as u32) << 24) | ((b2 as u32) << 16) | ((b1 as u32) << 8) | (b0 as u32);
                      let f = f32::from_bits(u);
                      if field == 3 { obj.insert("frequency_mhz".into(), Value::from(f as f64)); }
                    }
                  }
                  2 => { // length-delimited
                    // read length varint
                    let mut shift = 0u32; let mut l: u64 = 0;
                    loop {
                      if i >= body.len() { break; }
                      let b = body[i]; i += 1;
                      l |= ((b & 0x7F) as u64) << shift;
                      if (b & 0x80) == 0 { break; }
                      shift += 7;
                    }
                    let li = l as usize;
                    if i + li <= body.len() {
                      let slice = &body[i..i+li];
                      i += li;
                      if field == 5 {
                        // payload bytes -> base64
                        let b64 = base64::encode(slice);
                        obj.insert("payload".into(), Value::from(b64));
                      } else if field == 6 {
                        if let Ok(s) = std::str::from_utf8(slice) { obj.insert("extra".into(), Value::from(s)); }
                      }
                    }
                  }
                  _ => {
                    // unsupported wire type: bail out to avoid infinite loop
                    break;
                  }
                }
              }
              Some(Value::Object(obj))
            }
            Err(e) => {
              log::warn!("bt::listener: base64 decode failed: {}", e);
              None
            }
          }
        } else {
          // Not JSON: accept either PROTO:<base64> or raw base64 payloads
          let mut decoded: Option<Vec<u8>> = None;
          if msg.starts_with("PROTO:") {
            let b64 = &msg[6..];
            match base64::decode(b64) {
              Ok(bytes) => decoded = Some(bytes),
              Err(e) => log::warn!("bt::listener: base64 decode failed for PROTO: {}", e),
            }
          } else {
            // attempt to decode the entire string as base64 (some transports may strip the PROTO: prefix)
            match base64::decode(&msg) {
              Ok(bytes) => decoded = Some(bytes),
              Err(_) => {
                log::warn!("bt::listener: invalid JSON payload and not base64: {}", msg);
              }
            }
          }

          if let Some(bytes) = decoded {
            // strip optional 0xAA55 + u16 length framing
            let mut cursor = 0usize;
            if bytes.len() >= 4 && bytes[0] == 0xAA && bytes[1] == 0x55 {
              let len = (bytes[2] as usize) | ((bytes[3] as usize) << 8);
              cursor = 4;
              if cursor + len > bytes.len() { cursor = 4; }
            }
            let body = &bytes[cursor..];
            // Parse minimal protobuf fields for RadioSignal
            let mut i = 0usize;
            let mut obj = serde_json::Map::new();
            while i < body.len() {
              // read varint tag
              let mut shift = 0u32;
              let mut tag: u64 = 0;
              loop {
                if i >= body.len() { break; }
                let b = body[i]; i += 1;
                tag |= ((b & 0x7F) as u64) << shift;
                if (b & 0x80) == 0 { break; }
                shift += 7;
              }
              let field = (tag >> 3) as u32;
              let wire = (tag & 0x7) as u8;
              match wire {
                0 => { // varint
                  // read varint value
                  let mut shift = 0u32; let mut val: u64 = 0;
                  loop {
                    if i >= body.len() { break; }
                    let b = body[i]; i += 1;
                    val |= ((b & 0x7F) as u64) << shift;
                    if (b & 0x80) == 0 { break; }
                    shift += 7;
                  }
                  match field {
                    1 => { obj.insert("timestamp_ms".into(), Value::from(val)); }
                    2 => { obj.insert("module".into(), Value::from(val as i64)); }
                    4 => {
                      // ZigZag-decode RSSI (sint) encoded as varint
                      let rssi = ((val >> 1) as i64) ^ -((val & 1) as i64);
                      obj.insert("rssi".into(), Value::from(rssi));
                    }
                    _ => {}
                  }
                }
                5 => { // 32-bit
                  if i + 4 <= body.len() {
                    let b0 = body[i]; let b1 = body[i+1]; let b2 = body[i+2]; let b3 = body[i+3];
                    i += 4;
                    let u = ((b3 as u32) << 24) | ((b2 as u32) << 16) | ((b1 as u32) << 8) | (b0 as u32);
                    let f = f32::from_bits(u);
                    if field == 3 { obj.insert("frequency_mhz".into(), Value::from(f as f64)); }
                  }
                }
                2 => { // length-delimited
                  // read length varint
                  let mut shift = 0u32; let mut l: u64 = 0;
                  loop {
                    if i >= body.len() { break; }
                    let b = body[i]; i += 1;
                    l |= ((b & 0x7F) as u64) << shift;
                    if (b & 0x80) == 0 { break; }
                    shift += 7;
                  }
                  let li = l as usize;
                  if i + li <= body.len() {
                    let slice = &body[i..i+li];
                    i += li;
                    if field == 5 {
                      // payload bytes -> base64
                      let b64 = base64::encode(slice);
                      obj.insert("payload".into(), Value::from(b64));
                    } else if field == 6 {
                      if let Ok(s) = std::str::from_utf8(slice) { obj.insert("extra".into(), Value::from(s)); }
                    }
                  }
                }
                _ => {
                  // unsupported wire type: bail out to avoid infinite loop
                  break;
                }
              }
            }
            Some(Value::Object(obj))
          } else {
            None
          }
        }
      };

      if v_opt.is_none() {
        continue;
      }
      let v = v_opt.unwrap();

      // Helper to process a single signal object
      let mut process_signal = |sig: &Value| {
        // Extract module (numeric) to decide CSV file
        let module = sig.get("module").and_then(|m| m.as_i64()).unwrap_or(-1);

        if current_module != Some(module) {
          // close old file and open new one
          current_file = None;
          current_path = None;
          current_module = Some(module);

          // create new tmp file in app cache dir (Android-friendly)
          let mut path = std::env::temp_dir(); // fallback
          #[cfg(any(target_os = "android", target_os = "ios"))]
          {
             // Use Tauri's path resolver to get a writable directory
             if let Ok(dir) = app_clone.path().app_cache_dir() {
               path = dir;
             }
          }

          // use fixed filename per module so it gets overwritten each time
          let fname = format!("sharkos_radio_{}.csv", module);
          path.push(fname);

          // truncate file on open so previous contents are removed
          match OpenOptions::new().create(true).write(true).truncate(true).open(&path) {
            Ok(mut f) => {
              // write header
              if let Err(e) = writeln!(f, "timestamp_ms,module,frequency_mhz,rssi,payload_base64,extra") {
                log::error!("bt::listener: failed to write CSV header: {}", e);
              }
              // keep file handle
              current_file = Some(f);
              current_path = Some(path.clone());
              log::info!("bt::listener: opened CSV {}", path.display());
            }
            Err(e) => {
              log::error!("bt::listener: could not open tmp csv {}: {}", path.display(), e);
              current_file = None;
              current_path = None;
            }
          }
        }

        // Serialize row values
        let ts = sig.get("timestamp_ms").and_then(|t| t.as_u64()).unwrap_or_else(|| {
          SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_millis() as u64
        });
        let freq = sig.get("frequency_mhz").and_then(|f| f.as_f64()).unwrap_or(0.0);
        let rssi = sig.get("rssi").and_then(|r| r.as_i64()).unwrap_or(0);
        // payload may be base64 string or array of bytes; try to normalize to base64
        let payload_b64 = match sig.get("payload") {
          Some(p) if p.is_string() => p.as_str().unwrap_or("").to_string(),
          Some(p) if p.is_array() => {
            // convert array of numbers to bytes then base64
            let mut bytes = Vec::new();
            for it in p.as_array().unwrap_or(&vec![]) {
              if let Some(n) = it.as_u64() { bytes.push(n as u8); }
            }
            base64::encode(&bytes)
          }
          _ => String::new(),
        };
        let extra = sig.get("extra").and_then(|e| e.as_str()).unwrap_or("");

        // Append CSV row
        if let Some(f) = current_file.as_mut() {
          let row = format!("{},{},{:.6},{},{},{}\n", ts, module, freq, rssi, payload_b64, extra.replace('\n', " "));
          if let Err(e) = f.write_all(row.as_bytes()) {
            log::error!("bt::listener: failed to append csv row: {}", e);
          }
          // flush to ensure continuous append
          if let Err(e) = f.flush() {
            log::debug!("bt::listener: flush failed: {}", e);
          }
        }
      };

      // handle batch vs single
      if v.is_object() {
        if v.get("type").and_then(|t| t.as_str()) == Some("radio-batch") {
          if let Some(arr) = v.get("signals").and_then(|s| s.as_array()) {
            for s in arr {
              process_signal(s);
            }
          }
        } else {
          // single
          process_signal(&v);
        }
      }
    }

    log::info!("bt::listener thread exiting");
  });
}

/// Tauri command: send a JSON payload (string) to the listener thread.
#[tauri::command]
pub fn bt_listener_append(payload: String) -> Result<(), String> {
  if let Some(tx) = APP_SENDER.get() {
    tx.send(payload).map_err(|e| format!("send_err:{}", e))?;
    Ok(())
  } else {
    Err("listener_not_initialized".into())
  }
}
