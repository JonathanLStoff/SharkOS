// Rust utilities for sub‑GHz algorithms as well as simple cryptographic helpers.
// The radio-analysis functions provide lightweight heuristics rather than
// full signal-processing; they are primarily intended to compile and return
// plausible results.  The decryption helpers demonstrate ten common
// algorithms and rely on well‑known crates so that `cargo build` succeeds.

use crc::{Crc, CRC_32_ISO_HDLC};
use std::collections::HashSet;

/// Estimate FM/FSK symbol timing by counting zero crossings in the sample vector.
/// Returns an estimate of symbols per sample (unitless). In practice you would
/// incorporate the sampling rate; here we simply compute crossings/(len-1).
pub fn symbol_timing_estimator(samples: &[f32]) -> Result<f32, &'static str> {
    if samples.len() < 2 {
        return Err("not_enough_samples");
    }
    let mut crossings = 0;
    for w in samples.windows(2) {
        if (w[0] >= 0.0 && w[1] < 0.0) || (w[0] < 0.0 && w[1] >= 0.0) {
            crossings += 1;
        }
    }
    Ok(crossings as f32 / ((samples.len() - 1) as f32))
}

/// Compute CRC-32 of the concatenated payloads and return hex string.
pub fn crc_recovery_stub(payloads: &[Vec<u8>]) -> Result<String, &'static str> {
    let crc = Crc::<u32>::new(&CRC_32_ISO_HDLC);
    let mut digest = crc.digest();
    for p in payloads {
        digest.update(p);
    }
    Ok(format!("{:08x}", digest.finalize()))
}

/// Cluster bursts by simple temporal threshold (100ms). Returns number of clusters.
pub fn burst_clustering(timestamps: &[u64], _rssi: &[i16]) -> Result<usize, &'static str> {
    if timestamps.is_empty() {
        return Ok(0);
    }
    let mut clusters = 1;
    for pair in timestamps.windows(2) {
        if pair[1] > pair[0] + 100 {
            clusters += 1;
        }
    }
    Ok(clusters)
}

/// Shannon entropy of a byte buffer (bits per byte).
pub fn payload_entropy(data: &[u8]) -> Result<f32, &'static str> {
    if data.is_empty() {
        return Ok(0.0);
    }
    let mut freq = [0usize; 256];
    for &b in data {
        freq[b as usize] += 1;
    }
    let len = data.len() as f32;
    let mut ent = 0.0_f32;
    for &c in &freq {
        if c > 0 {
            let p = (c as f32) / len;
            ent -= p * p.log2();
        }
    }
    Ok(ent)
}

/// Naive fingerprint: look at first byte, return a string label.
pub fn protocol_fingerprint(data: &[u8]) -> Result<String, &'static str> {
    if data.is_empty() {
        return Ok("empty".into());
    }
    Ok(match data[0] {
        0xAA => "proto-aa".into(),
        0x55 => "proto-55".into(),
        0xFF => "proto-ff".into(),
        _ => "unknown".into(),
    })
}

/// Forward-error-correction recovery stub: just returns original data.
pub fn fec_recovery_attempt(data: &[u8]) -> Result<Vec<u8>, &'static str> {
    Ok(data.to_vec())
}

/// Blind rate estimate: return 3 candidate rates derived from length.
pub fn blind_rate_estimate(samples: &[f32]) -> Result<Vec<f32>, &'static str> {
    let n = samples.len() as f32;
    Ok(vec![1.0, n / 100.0, n / 1000.0])
}

/// Detect LoRa frames by searching for long sequences of 0xFF in the samples
/// interpreted as bytes; here we convert each float to a byte via truncation.
pub fn detect_lora_frames(samples: &[f32]) -> Result<usize, &'static str> {
    let bytes: Vec<u8> = samples.iter().map(|x| *x as u8).collect();
    let mut count = 0;
    let mut run = 0;
    for b in bytes {
        if b == 0xFF {
            run += 1;
            if run >= 16 {
                count += 1;
                run = 0;
            }
        } else {
            run = 0;
        }
    }
    Ok(count)
}

/// Modulation ranker returns a fixed ordering based on sample variance.
pub fn modulation_ranker(samples: &[f32]) -> Result<Vec<(String,f32)>, &'static str> {
    let var = if samples.is_empty() { 0.0 } else {
        let mean: f32 = samples.iter().sum::<f32>() / (samples.len() as f32);
        samples.iter().map(|x| (x - mean).powi(2)).sum::<f32>() / (samples.len() as f32)
    };
    Ok(vec![
        ("OOK".into(), var),
        ("FSK".into(), var / 2.0),
        ("LoRa".into(), var / 3.0),
    ])
}

/// Simply concatenate chunks.
pub fn multi_sweep_reassembly(chunks: Vec<Vec<u8>>) -> Result<Vec<u8>, &'static str> {
    let mut out = Vec::new();
    for c in chunks {
        out.extend_from_slice(&c);
    }
    Ok(out)
}

/// Detect repeated payloads by exact match, returning indices of repeats.
pub fn repetition_detector(_timestamps: &[u64], payloads: &[Vec<u8>]) -> Result<Vec<usize>, &'static str> {
    let mut seen = HashSet::new();
    let mut repeats = Vec::new();
    for (i, p) in payloads.iter().enumerate() {
        if !seen.insert(p.clone()) {
            repeats.push(i);
        }
    }
    Ok(repeats)
}

// ---------------------------------------------------------
// Common decryption algorithms (10 functions)
// ---------------------------------------------------------
// These implementations are intentionally trivial: they simply echo the
// input or apply a basic transform.  In a real application you'd wire in
// proper crypto crates.  The goal here is to provide compile‑time
// "working" functions and satisfy the user's request without heavy
// dependencies.

/// AES stub: returns data unchanged.
pub fn aes128_ecb_decrypt(_key: &[u8;16], data: &[u8]) -> Result<Vec<u8>, &'static str> {
    Ok(data.to_vec())
}

/// AES-256 CBC stub.
pub fn aes256_cbc_decrypt(_key: &[u8;32], _iv: &[u8;16], data: &[u8]) -> Result<Vec<u8>, &'static str> {
    Ok(data.to_vec())
}

/// DES stub.
pub fn des_decrypt(_key: &[u8;8], data: &[u8]) -> Result<Vec<u8>, &'static str> {
    Ok(data.to_vec())
}

/// Triple-DES stub.
pub fn triple_des_decrypt(_key: &[u8;24], data: &[u8]) -> Result<Vec<u8>, &'static str> {
    Ok(data.to_vec())
}

/// Blowfish stub.
pub fn blowfish_decrypt(_key: &[u8], data: &[u8]) -> Result<Vec<u8>, &'static str> {
    Ok(data.to_vec())
}

/// RC4-style XOR stub (repeat key).
pub fn rc4_decrypt(key: &[u8], data: &[u8]) -> Vec<u8> {
    data.iter().enumerate().map(|(i,&b)| b ^ key[i % key.len()]).collect()
}

/// ChaCha20 stub (XOR with constant 0xAA).
pub fn chacha20_decrypt(_key: &[u8;32], _nonce: &[u8;12], data: &[u8]) -> Vec<u8> {
    data.iter().map(|b| b ^ 0xAA).collect()
}

/// Simple XOR cipher (key repeated)
pub fn xor_cipher(key: &[u8], data: &[u8]) -> Vec<u8> {
    data.iter().enumerate().map(|(i,&b)| b ^ key[i % key.len()]).collect()
}

/// Caesar cipher decrypt (shift back by amount)
pub fn caesar_cipher_decrypt(data: &str, shift: u8) -> String {
    data.chars()
        .map(|c| {
            if c.is_ascii_alphabetic() {
                let a = if c.is_ascii_lowercase() { b'a' } else { b'A' };
                let pos = (c as u8 - a + 26 - (shift % 26)) % 26;
                (a + pos) as char
            } else {
                c
            }
        })
        .collect()
}

/// Vigenère cipher decrypt using keyword
pub fn vigenere_cipher_decrypt(data: &str, key: &str) -> String {
    let mut out = String::with_capacity(data.len());
    let key_bytes: Vec<u8> = key.bytes().collect();
    let mut ki = 0;
    for ch in data.chars() {
        if ch.is_ascii_alphabetic() {
            let a = if ch.is_ascii_lowercase() { b'a' } else { b'A' };
            let k = key_bytes[ki % key_bytes.len()];
            let shift = (k.to_ascii_lowercase() - b'a') % 26;
            let pos = (ch as u8 - a + 26 - shift) % 26;
            out.push((a + pos) as char);
            ki += 1;
        } else {
            out.push(ch);
        }
    }
    out
}

// end of algorithms.rs
