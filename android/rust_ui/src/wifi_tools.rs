// Rust stubs for Wi‑Fi capture and cracking helpers.
// These are placeholders: real capture must be implemented on the ESP32 or
// platform-specific backend and exposed to the UI (via Tauri/IPC or similar).

/// Start capturing Wi‑Fi frames and forwarding them to the UI or local storage.
pub fn start_wifi_capture(_iface: &str, _filter: Option<&str>) -> Result<(), &'static str> {
    // Integrate with Wi‑Fi MAC/frame capture on the device.
    Err("not_implemented")
}

/// Stop a running capture and return summary information.
pub fn stop_wifi_capture() -> Result<String, &'static str> {
    Err("not_implemented")
}

/// Save captured frames as a pcap file buffer (return bytes or a path).
pub fn export_pcap(_frames: Vec<Vec<u8>>) -> Result<Vec<u8>, &'static str> {
    Err("not_implemented")
}

/// Intercept TLS flows / collect session metadata for offline analysis.
pub fn collect_tls_metadata(_frames: Vec<Vec<u8>>) -> Result<String, &'static str> {
    Err("not_implemented")
}

/// Offline HTTPS cracking helper stub (e.g., assist in session key extraction workflows).
pub fn https_crack_stub(_captured_data: Vec<u8>) -> Result<String, &'static str> {
    Err("not_implemented")
}

/// WPA handshake processing helper — detect handshakes and extract 4-way info.
pub fn detect_wpa_handshake(_frames: Vec<Vec<u8>>) -> Result<bool, &'static str> {
    Err("not_implemented")
}

/// Kick off an offline cracking job (wordlist) — placeholder.
pub fn start_wpa_crack_job(_pcap_bytes: Vec<u8>, _wordlist_path: &str) -> Result<String, &'static str> {
    Err("not_implemented")
}

/// Example helper: spawn an ephemeral evil-twin AP (requires hardware support).
pub fn spawn_evil_twin(_ssid: &str) -> Result<(), &'static str> {
    Err("not_implemented")
}


use aes::Aes128;
use aes::cipher::{BlockDecrypt, KeyInit, generic_array::{GenericArray, typenum::U16}};

/// Toy HTTPS packet decryptor that actually does AES-128-CBC decryption of
/// TLS application-data records.  It iterates over each TLS record in `data`,
/// checks for `content_type == 0x17` (application data), takes the first 16
/// bytes of the record payload as the IV, and decrypts the remainder using the
/// provided `key`.  Other records are copied through unchanged.  If decryption
/// fails or parsing is invalid the payload is returned verbatim.
///
/// `key` must be 16 bytes (AES-128).  No other cipher suites are supported.
pub fn decrypt_https_packets(data: &[u8], key: &[u8]) -> Vec<u8> {
    if key.len() != 16 {
        // unsupported key size – just return input
        return data.to_vec();
    }
    let mut out = Vec::with_capacity(data.len());
    let mut i = 0;
    while i + 5 <= data.len() {
        let rec_type = data[i];
        let len = ((data[i+3] as usize) << 8) | (data[i+4] as usize);
        if i + 5 + len > data.len() {
            // truncated, copy remaining
            out.extend_from_slice(&data[i..]);
            break;
        }
        if rec_type == 0x17 && len >= 16 {
            // application data record
            let iv = &data[i+5..i+21];
            let ciphertext = &data[i+21..i+5+len];
            // perform manual AES-128-CBC decryption with PKCS#7 unpadding
            let cipher = Aes128::new(GenericArray::from_slice(key));
            let mut prev: GenericArray<u8, U16> = GenericArray::clone_from_slice(iv);
            let mut decrypted = Vec::with_capacity(ciphertext.len());
            for chunk in ciphertext.chunks(16) {
                if chunk.len() < 16 {
                    // incomplete block, copy through
                    decrypted.extend_from_slice(chunk);
                    break;
                }
                let mut block: GenericArray<u8, U16> = GenericArray::clone_from_slice(chunk);
                cipher.decrypt_block(&mut block);
                for j in 0..16 {
                    block[j] ^= prev[j];
                }
                prev = GenericArray::clone_from_slice(chunk);
                decrypted.extend_from_slice(&block);
            }
            // strip PKCS#7 padding if valid
            if let Some(&pad) = decrypted.last() {
                let pad = pad as usize;
                if pad > 0 && pad <= decrypted.len() {
                    if decrypted[decrypted.len() - pad..].iter().all(|&b| b == pad as u8) {
                        decrypted.truncate(decrypted.len() - pad);
                    }
                }
            }
            out.extend_from_slice(&decrypted);
        } else {
            // not application data or too short – copy raw record
            out.extend_from_slice(&data[i..i+5+len]);
        }
        i += 5 + len;
    }
    out
}
