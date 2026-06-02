use std::collections::HashMap;
use std::time::Duration;

// ========== Configuration and Data Types ==========

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Modulation {
    OOK,
    FSK,
    GFSK,
    MSK,
    // Add others if needed
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum LineCoding {
    NRZ,
    NRZInverted,          // all bits flipped
    Manchester,           // 01 → 0, 10 → 1
    ManchesterInverted,   // 10 → 0, 01 → 1
    // DifferentialManchester, // can be added later
}

#[derive(Debug, Clone)]
pub struct RxConfig {
    pub freq_hz: u32,
    pub modulation: Modulation,
    pub baud: u32,
    pub raw_mode: bool,               // if true, ignore sync/length and return raw bits
    pub sync_word: Option<u32>,        // used only if raw_mode = false
    pub sync_word_len: u8,             // bits (max 32)
    pub packet_length_mode: PacketLengthMode,
    pub packet_length: Option<u8>,     // for fixed length mode
    pub crc_enabled: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PacketLengthMode {
    Fixed,
    Variable,
}

// ========== Placeholder for Hardware Access ==========

/// This function must be provided by the user. It configures the CC1101
/// according to `config`, captures for `timeout_ms` milliseconds, and
/// returns raw demodulated bits (0/1) if `raw_mode` is true.
/// If `raw_mode` is false, it returns packets detected by the hardware.
/// For our search we only use raw mode.
fn rx_raw(config: &RxConfig, timeout_ms: u32) -> Vec<u8> {
    // User implementation: configure CC1101, capture, return bits.
    // This is a placeholder.
    unimplemented!("Replace with actual CC1101 readout")
}

// ========== Line Coding Decoders ==========

fn apply_line_coding(bits: &[u8], coding: LineCoding) -> Vec<u8> {
    match coding {
        LineCoding::NRZ => bits.to_vec(),
        LineCoding::NRZInverted => bits.iter().map(|&b| if b == 0 { 1 } else { 0 }).collect(),
        LineCoding::Manchester => {
            // Standard Manchester: 01 = 0, 10 = 1
            bits.chunks_exact(2)
                .filter_map(|chunk| match (chunk[0], chunk[1]) {
                    (0, 1) => Some(0),
                    (1, 0) => Some(1),
                    _ => None, // invalid pair -> skip
                })
                .collect()
        }
        LineCoding::ManchesterInverted => {
            // Inverted: 10 = 0, 01 = 1
            bits.chunks_exact(2)
                .filter_map(|chunk| match (chunk[0], chunk[1]) {
                    (1, 0) => Some(0),
                    (0, 1) => Some(1),
                    _ => None,
                })
                .collect()
        }
    }
}

// ========== Packet Detection (gap‑based) ==========

const IDLE_THRESHOLD_BITS: usize = 20; // runs longer than this are gaps

fn detect_packets(bits: &[u8]) -> Vec<Vec<u8>> {
    let mut packets = Vec::new();
    let n = bits.len();
    let mut i = 0;

    while i < n {
        // Skip idle gaps
        while i < n {
            let start = i;
            let cur = bits[i];
            let mut run = 0;
            while i < n && bits[i] == cur {
                run += 1;
                i += 1;
            }
            if run < IDLE_THRESHOLD_BITS {
                // This short run belongs to a packet – rewind and break
                i = start;
                break;
            }
            // Otherwise it was a true gap, continue scanning
        }
        if i >= n {
            break;
        }

        // Collect bits until the next long idle
        let pkt_start = i;
        while i < n {
            let cur = bits[i];
            let mut run = 0;
            while i < n && bits[i] == cur {
                run += 1;
                i += 1;
            }
            if run >= IDLE_THRESHOLD_BITS {
                // Found a gap: packet ends before this run
                let pkt_end = i - run;
                if pkt_end > pkt_start {
                    packets.push(bits[pkt_start..pkt_end].to_vec());
                }
                break;
            }
            // Short run, continue inside packet
        }
        // If we hit EOF without a gap, add the final segment
        if i == n && i > pkt_start {
            packets.push(bits[pkt_start..].to_vec());
        }
    }

    // Filter out packets that are too short or too long
    packets
        .into_iter()
        .filter(|p| p.len() >= 16 && p.len() <= 500)
        .collect()
}

// ========== Checksum Testing ==========

#[derive(Debug, Clone, Copy)]
enum ChecksumType {
    Xor,
    SumMod256,
    Crc8,
}

/// Returns true if the last byte of every packet matches the checksum
/// computed from the preceding bytes using the given algorithm.
fn test_checksum(packets: &[&[u8]], ctype: ChecksumType) -> Option<bool> {
    if packets.is_empty() {
        return None;
    }
    let len = packets[0].len();
    if len < 2 {
        return None;
    }
    // All packets must have the same length
    if !packets.iter().all(|p| p.len() == len) {
        return None;
    }

    let data_len = len - 1;
    for p in packets {
        let data = &p[..data_len];
        let expected = p[data_len];
        let computed = match ctype {
            ChecksumType::Xor => data.iter().fold(0, |acc, &b| acc ^ b),
            ChecksumType::SumMod256 => data.iter().fold(0u8, |acc, &b| acc.wrapping_add(b)),
            ChecksumType::Crc8 => {
                // CRC-8-ATM (poly 0x07)
                let mut crc = 0u8;
                for &b in data {
                    crc ^= b;
                    for _ in 0..8 {
                        if crc & 0x80 != 0 {
                            crc = (crc << 1) ^ 0x07;
                        } else {
                            crc <<= 1;
                        }
                    }
                }
                crc
            }
        };
        if computed != expected {
            return Some(false);
        }
    }
    Some(true)
}

// ========== Packet Scoring ==========

fn score_packets(packets: &[Vec<u8>]) -> f64 {
    if packets.is_empty() {
        return 0.0;
    }

    let mut score = 0.0;

    // 1. Number of packets (capped at 10)
    score += (packets.len() as f64).min(10.0) * 1.0;

    // 2. Length consistency (lower variance is better)
    let lengths: Vec<usize> = packets.iter().map(|p| p.len()).collect();
    let mean_len = lengths.iter().sum::<usize>() as f64 / lengths.len() as f64;
    let variance = lengths
        .iter()
        .map(|&l| (l as f64 - mean_len).powi(2))
        .sum::<f64>()
        / lengths.len() as f64;
    // variance may be zero -> 10/(1+0)=10
    score += 10.0 / (1.0 + variance);

    // 3. Common prefix (possible device ID / sync word)
    if packets.len() >= 2 {
        let min_len = packets.iter().map(|p| p.len()).min().unwrap();
        if min_len >= 16 {
            // Convert first 16 bits to a u16 for easy comparison
            let first_vals: Vec<u16> = packets
                .iter()
                .map(|p| {
                    let mut v = 0u16;
                    for i in 0..16 {
                        if i < p.len() && p[i] == 1 {
                            v |= 1 << (15 - i);
                        }
                    }
                    v
                })
                .collect();

            // Check if all are identical
            if first_vals.windows(2).all(|w| w[0] == w[1]) {
                score += 20.0;
            } else {
                // At least some common value
                let mut counts = HashMap::new();
                for &v in &first_vals {
                    *counts.entry(v).or_insert(0) += 1;
                }
                let max_count = *counts.values().max().unwrap_or(&1);
                if max_count > 1 {
                    score += 5.0 * (max_count as f64 / packets.len() as f64);
                }
            }
        }
    }

    // 4. Checksum detection (if all packets have identical length)
    let same_length = lengths.windows(2).all(|w| w[0] == w[1]);
    if same_length && packets.len() >= 3 {
        let data_refs: Vec<&[u8]> = packets.iter().map(|p| p.as_slice()).collect();
        for &ctype in &[ChecksumType::Xor, ChecksumType::SumMod256, ChecksumType::Crc8] {
            if let Some(true) = test_checksum(&data_refs, ctype) {
                score += 30.0;
                break;
            }
        }
    }

    score
}

// ========== Sync Word Extraction (Optional) ==========

/// Finds the longest common prefix (in bits) among all packets.
/// Returns Some(u32) with that prefix (MSB first, up to 32 bits).
fn find_sync_word(packets: &[Vec<u8>]) -> Option<u32> {
    if packets.is_empty() {
        return None;
    }
    let min_len = packets.iter().map(|p| p.len()).min().unwrap();
    let mut common_len = 0;
    for i in 0..min_len {
        let bit = packets[0][i];
        if packets.iter().all(|p| p[i] == bit) {
            common_len = i + 1;
        } else {
            break;
        }
    }
    if common_len >= 8 {
        let mut sync = 0u32;
        for i in 0..common_len.min(32) {
            if packets[0][i] == 1 {
                sync |= 1 << (common_len - 1 - i); // MSB first
            }
        }
        Some(sync)
    } else {
        None
    }
}

// ========== Main Discovery Routine ==========

#[derive(Debug)]
pub struct DiscoveredParams {
    pub config: RxConfig,
    pub line_coding: LineCoding,
    pub sync_word: Option<u32>,
}

pub fn auto_discover_remote() -> Option<DiscoveredParams> {
    // Search ranges
    let freq_min = 433_000_000;
    let freq_max = 434_000_000;
    let coarse_step = 200_000; // 200 kHz

    let baud_rates = vec![1000, 2400, 4800, 9600, 19200, 38400, 100_000];
    let modulations = vec![
        Modulation::OOK,
        Modulation::FSK,
        Modulation::GFSK,
        Modulation::MSK,
    ];
    let line_codings = vec![
        LineCoding::NRZ,
        LineCoding::NRZInverted,
        LineCoding::Manchester,
        LineCoding::ManchesterInverted,
    ];

    let mut best_score = 0.0;
    let mut best_config: Option<RxConfig> = None;
    let mut best_line_coding = LineCoding::NRZ;

    // ----- Coarse search -----
    for &modulation in &modulations {
        for freq in (freq_min..=freq_max).step_by(coarse_step as usize) {
            for &baud in &baud_rates {
                let config = RxConfig {
                    freq_hz: freq,
                    modulation,
                    baud,
                    raw_mode: true,
                    sync_word: None,
                    sync_word_len: 0,
                    packet_length_mode: PacketLengthMode::Variable,
                    packet_length: None,
                    crc_enabled: false,
                };

                // Capture raw bits (100 ms)
                let raw_bits = rx_raw(&config, 100);
                if raw_bits.is_empty() {
                    continue;
                }

                for &lc in &line_codings {
                    let decoded = apply_line_coding(&raw_bits, lc);
                    let packets = detect_packets(&decoded);
                    if packets.is_empty() {
                        continue;
                    }
                    let score = score_packets(&packets);
                    if score > best_score {
                        best_score = score;
                        best_config = Some(config.clone());
                        best_line_coding = lc;
                    }
                }
            }
        }
    }

    let mut best_config = best_config?;

    // ----- Fine frequency scan around the best candidate -----
    let fine_step = 10_000; // 10 kHz
    let fine_start = best_config.freq_hz.saturating_sub(100_000);
    let fine_end = best_config.freq_hz.saturating_add(100_000).min(freq_max);
    for freq in (fine_start..=fine_end).step_by(fine_step as usize) {
        best_config.freq_hz = freq;
        let raw_bits = rx_raw(&best_config, 100);
        if raw_bits.is_empty() {
            continue;
        }
        let decoded = apply_line_coding(&raw_bits, best_line_coding);
        let packets = detect_packets(&decoded);
        let score = score_packets(&packets);
        if score > best_score {
            best_score = score;
            // keep this config
        } else {
            // revert freq if not better? we'll just keep the best one
        }
    }

    // ----- Optional: extract sync word from best configuration -----
    // Capture a longer sample to get more packets
    let final_raw = rx_raw(&best_config, 500);
    let final_decoded = apply_line_coding(&final_raw, best_line_coding);
    let final_packets = detect_packets(&final_decoded);
    let sync_word = if final_packets.len() >= 3 {
        find_sync_word(&final_packets)
    } else {
        None
    };

    Some(DiscoveredParams {
        config: best_config,
        line_coding: best_line_coding,
        sync_word,
    })
}