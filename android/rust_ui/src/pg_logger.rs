use log::{error, info};
use std::sync::OnceLock;
use tokio::sync::Mutex;
use tokio_postgres::{Client, NoTls};

static PG_CLIENT: OnceLock<Mutex<Option<Client>>> = OnceLock::new();

fn pg_mutex() -> &'static Mutex<Option<Client>> {
    PG_CLIENT.get_or_init(|| Mutex::new(None))
}

pub async fn connect(
    host: &str,
    port: u16,
    database: &str,
    username: &str,
    password: &str,
) -> Result<(), String> {
    let conn_str = format!(
        "host={host} port={port} dbname={database} user={username} password={password}"
    );
    let (client, connection) = tokio_postgres::connect(&conn_str, NoTls)
        .await
        .map_err(|e| format!("pg connect: {e}"))?;

    // Drive the connection in the background; clear our handle if it dies.
    tokio::spawn(async move {
        if let Err(e) = connection.await {
            error!("[pg] connection closed: {e}");
        }
    });

    client
        .batch_execute(
            "
            CREATE TABLE IF NOT EXISTS wifi_packets (
                id          BIGSERIAL PRIMARY KEY,
                captured_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
                frequency_mhz REAL,
                channel     INTEGER,
                rssi        INTEGER,
                ssid        TEXT,
                extra       TEXT,
                payload_b64 TEXT
            );
            CREATE TABLE IF NOT EXISTS subghz_packets (
                id          BIGSERIAL PRIMARY KEY,
                captured_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
                frequency_mhz REAL,
                modulation  TEXT,
                rssi        INTEGER,
                data_length INTEGER,
                raw_data    TEXT,
                module_id   INTEGER
            );
            CREATE TABLE IF NOT EXISTS bluetooth_packets (
                id          BIGSERIAL PRIMARY KEY,
                captured_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
                frequency_mhz REAL,
                channel     INTEGER,
                rssi        INTEGER,
                device_name TEXT,
                extra       TEXT,
                payload_b64 TEXT
            );
            ",
        )
        .await
        .map_err(|e| format!("pg create tables: {e}"))?;

    *pg_mutex().lock().await = Some(client);
    info!("[pg] connected to {host}:{port}/{database}");
    Ok(())
}

pub async fn disconnect() {
    *pg_mutex().lock().await = None;
}

pub async fn is_connected() -> bool {
    pg_mutex().lock().await.is_some()
}

pub async fn log_wifi(
    frequency_mhz: f64,
    channel: i32,
    rssi: i32,
    ssid: &str,
    extra: &str,
    payload_b64: &str,
) -> Result<(), String> {
    let mut guard = pg_mutex().lock().await;
    let Some(client) = guard.as_ref() else {
        return Ok(());
    };
    let result = client
        .execute(
            "INSERT INTO wifi_packets (frequency_mhz, channel, rssi, ssid, extra, payload_b64)
             VALUES ($1,$2,$3,$4,$5,$6)",
            &[
                &(frequency_mhz as f32),
                &channel,
                &rssi,
                &ssid,
                &extra,
                &payload_b64,
            ],
        )
        .await;
    if let Err(e) = result {
        *guard = None;
        return Err(format!("pg insert wifi: {e}"));
    }
    Ok(())
}

pub async fn log_subghz(
    frequency_mhz: f64,
    modulation: &str,
    rssi: i32,
    data_length: i32,
    raw_data: &str,
    module_id: i32,
) -> Result<(), String> {
    let mut guard = pg_mutex().lock().await;
    let Some(client) = guard.as_ref() else {
        return Ok(());
    };
    let result = client
        .execute(
            "INSERT INTO subghz_packets (frequency_mhz, modulation, rssi, data_length, raw_data, module_id)
             VALUES ($1,$2,$3,$4,$5,$6)",
            &[
                &(frequency_mhz as f32),
                &modulation,
                &rssi,
                &data_length,
                &raw_data,
                &module_id,
            ],
        )
        .await;
    if let Err(e) = result {
        *guard = None;
        return Err(format!("pg insert subghz: {e}"));
    }
    Ok(())
}

pub async fn log_bluetooth(
    frequency_mhz: f64,
    channel: i32,
    rssi: i32,
    device_name: &str,
    extra: &str,
    payload_b64: &str,
) -> Result<(), String> {
    let mut guard = pg_mutex().lock().await;
    let Some(client) = guard.as_ref() else {
        return Ok(());
    };
    let result = client
        .execute(
            "INSERT INTO bluetooth_packets (frequency_mhz, channel, rssi, device_name, extra, payload_b64)
             VALUES ($1,$2,$3,$4,$5,$6)",
            &[
                &(frequency_mhz as f32),
                &channel,
                &rssi,
                &device_name,
                &extra,
                &payload_b64,
            ],
        )
        .await;
    if let Err(e) = result {
        *guard = None;
        return Err(format!("pg insert bluetooth: {e}"));
    }
    Ok(())
}
