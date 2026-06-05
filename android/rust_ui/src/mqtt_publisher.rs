// Minimal MQTT v3.1.1 publish-only client using raw tokio TCP.
// Only QoS 0 is used (fire-and-forget), so no PUBACK handling is needed.

use log::{error, info};
use std::sync::OnceLock;
use std::time::Duration;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;
use tokio::sync::Mutex;
use tokio::time::timeout;

static MQTT_STREAM: OnceLock<Mutex<Option<TcpStream>>> = OnceLock::new();

fn mqtt_mutex() -> &'static Mutex<Option<TcpStream>> {
    MQTT_STREAM.get_or_init(|| Mutex::new(None))
}

// MQTT variable-length remaining-length encoding (RFC 3.1.1 §2.2.3)
fn encode_remaining(mut len: usize) -> Vec<u8> {
    let mut out = Vec::with_capacity(4);
    loop {
        let mut byte = (len % 128) as u8;
        len /= 128;
        if len > 0 {
            byte |= 0x80;
        }
        out.push(byte);
        if len == 0 {
            break;
        }
    }
    out
}

fn write_utf8(buf: &mut Vec<u8>, s: &str) {
    let b = s.as_bytes();
    buf.extend_from_slice(&(b.len() as u16).to_be_bytes());
    buf.extend_from_slice(b);
}

fn build_connect(
    client_id: &str,
    username: Option<&str>,
    password: Option<&str>,
) -> Vec<u8> {
    let mut payload = Vec::new();

    // Protocol name "MQTT" + level 4 (v3.1.1)
    payload.extend_from_slice(&[0x00, 0x04, b'M', b'Q', b'T', b'T', 0x04]);

    // Connect flags: clean-session (0x02) + optional user/pass bits
    let mut flags: u8 = 0x02;
    if username.is_some() {
        flags |= 0x80;
    }
    if password.is_some() {
        flags |= 0x40;
    }
    payload.push(flags);

    // Keep-alive: 60 seconds
    payload.extend_from_slice(&[0x00, 0x3C]);

    // Payload: client-id, then optional username/password
    write_utf8(&mut payload, client_id);
    if let Some(u) = username {
        write_utf8(&mut payload, u);
    }
    if let Some(p) = password {
        write_utf8(&mut payload, p);
    }

    let mut pkt = Vec::new();
    pkt.push(0x10); // CONNECT fixed-header byte
    pkt.extend(encode_remaining(payload.len()));
    pkt.extend(payload);
    pkt
}

fn build_publish(topic: &str, payload: &[u8]) -> Vec<u8> {
    let mut var = Vec::new();
    write_utf8(&mut var, topic);
    // No packet-id for QoS 0

    let remaining = var.len() + payload.len();
    let mut pkt = Vec::new();
    pkt.push(0x30); // PUBLISH, QoS 0, no retain
    pkt.extend(encode_remaining(remaining));
    pkt.extend(var);
    pkt.extend_from_slice(payload);
    pkt
}

/// Connect to the MQTT broker and store the TCP stream.
pub async fn connect(
    host: &str,
    port: u16,
    client_id: &str,
    username: Option<&str>,
    password: Option<&str>,
) -> Result<(), String> {
    let connect_timeout = Duration::from_secs(10);
    let addr = format!("{host}:{port}");
    let mut stream = timeout(connect_timeout, TcpStream::connect(&addr))
        .await
        .map_err(|_| format!("mqtt tcp connect to {addr}: timed out"))?
        .map_err(|e| format!("mqtt tcp connect to {addr}: {e}"))?;

    let connect_pkt = build_connect(client_id, username, password);
    timeout(connect_timeout, stream.write_all(&connect_pkt))
        .await
        .map_err(|_| "mqtt write CONNECT: timed out".to_string())?
        .map_err(|e| format!("mqtt write CONNECT: {e}"))?;

    // Read CONNACK: fixed-header (0x20), length (0x02), session-present, return-code
    let mut connack = [0u8; 4];
    timeout(connect_timeout, stream.read_exact(&mut connack))
        .await
        .map_err(|_| "mqtt read CONNACK: timed out".to_string())?
        .map_err(|e| format!("mqtt read CONNACK: {e}"))?;

    if connack[0] != 0x20 {
        return Err(format!(
            "mqtt: expected CONNACK (0x20), got 0x{:02X}",
            connack[0]
        ));
    }
    match connack[3] {
        0 => {}
        1 => return Err("mqtt: refused – unacceptable protocol version".into()),
        2 => return Err("mqtt: refused – client ID rejected".into()),
        3 => return Err("mqtt: refused – server unavailable".into()),
        4 => return Err("mqtt: refused – bad username or password".into()),
        5 => return Err("mqtt: refused – not authorised".into()),
        c => return Err(format!("mqtt: CONNACK return code {c}")),
    }

    *mqtt_mutex().lock().await = Some(stream);
    info!("[mqtt] connected to {addr} as '{client_id}'");
    Ok(())
}

pub async fn disconnect() {
    *mqtt_mutex().lock().await = None;
}

pub async fn is_connected() -> bool {
    mqtt_mutex().lock().await.is_some()
}

/// Publish a UTF-8 payload to the given topic (QoS 0).
pub async fn publish(topic: &str, payload: &str) -> Result<(), String> {
    let mut guard = mqtt_mutex().lock().await;
    let Some(stream) = guard.as_mut() else {
        return Ok(()); // not connected – silently drop
    };
    let pkt = build_publish(topic, payload.as_bytes());
    if let Err(e) = stream.write_all(&pkt).await {
        // Connection broken – clear so the next call can try to reconnect
        *guard = None;
        error!("[mqtt] publish failed, connection cleared: {e}");
        return Err(format!("mqtt publish: {e}"));
    }
    Ok(())
}
