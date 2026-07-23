use rusqlite::{params, Connection};
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, OnceLock};
use std::time::{SystemTime, UNIX_EPOCH};

const SESSION_PREFIX: &str = "sharkos-sniffer-session-";

static STORE: OnceLock<Mutex<RecorderStore>> = OnceLock::new();

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SnifferPacketInput {
    pub timestamp_ms: i64,
    pub freq: f64,
    pub mod_name: String,
    pub rssi: i64,
    pub len: i64,
    pub data: String,
    pub module: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SnifferRecord {
    pub id: i64,
    pub timestamp_ms: i64,
    pub freq: f64,
    pub mod_name: String,
    pub rssi: i64,
    pub len: i64,
    pub data: String,
    pub module: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SnifferPage {
    pub rows: Vec<SnifferRecord>,
    pub total_count: i64,
    pub filtered_count: i64,
    pub offset: i64,
    pub limit: i64,
    pub storage_bytes: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SnifferSessionStats {
    pub total_count: i64,
    pub storage_bytes: u64,
}

struct RecorderStore {
    conn: Connection,
    path: PathBuf,
}

impl RecorderStore {
    fn new(base_dir: &Path) -> Result<Self, String> {
        let path = session_db_path(base_dir)?;
        let conn = Connection::open(&path).map_err(|e| format!("open recorder db: {e}"))?;
        conn.execute_batch(
            "
            PRAGMA journal_mode = WAL;
            PRAGMA synchronous = NORMAL;
            CREATE TABLE IF NOT EXISTS sniffer_packets (
              id INTEGER PRIMARY KEY AUTOINCREMENT,
              timestamp_ms INTEGER NOT NULL,
              freq REAL NOT NULL,
              mod_name TEXT NOT NULL,
              rssi INTEGER NOT NULL,
              len INTEGER NOT NULL,
              data TEXT NOT NULL,
              module INTEGER NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_sniffer_rssi_id ON sniffer_packets(rssi, id);
            ",
        )
        .map_err(|e| format!("init recorder db: {e}"))?;
        Ok(Self { conn, path })
    }

    fn append_many(&mut self, entries: Vec<SnifferPacketInput>) -> Result<SnifferSessionStats, String> {
        if entries.is_empty() {
            return self.stats();
        }
        let tx = self.conn.transaction().map_err(|e| format!("begin tx: {e}"))?;
        {
            let mut stmt = tx
                .prepare(
                    "INSERT INTO sniffer_packets (timestamp_ms, freq, mod_name, rssi, len, data, module)
                     VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
                )
                .map_err(|e| format!("prepare insert: {e}"))?;
            for entry in entries {
                stmt.execute(params![
                    entry.timestamp_ms,
                    entry.freq,
                    entry.mod_name,
                    entry.rssi,
                    entry.len,
                    entry.data,
                    entry.module,
                ])
                .map_err(|e| format!("insert row: {e}"))?;
            }
        }
        tx.commit().map_err(|e| format!("commit tx: {e}"))?;
        self.stats()
    }

    fn clear(&mut self) -> Result<(), String> {
        self.conn
            .execute("DELETE FROM sniffer_packets", [])
            .map_err(|e| format!("clear recorder db: {e}"))?;
        self.conn
            .execute("DELETE FROM sqlite_sequence WHERE name = 'sniffer_packets'", [])
            .map_err(|e| format!("reset recorder sequence: {e}"))?;
        self.conn
            .execute_batch("VACUUM")
            .map_err(|e| format!("vacuum recorder db: {e}"))?;
        Ok(())
    }

    fn page(&self, offset: i64, limit: i64, min_rssi: i64, min_len: i64) -> Result<SnifferPage, String> {
        let total_count = self.count_all()?;
        let filtered_count = self.count_filtered(min_rssi, min_len)?;
        let mut stmt = self
            .conn
            .prepare(
                "SELECT id, timestamp_ms, freq, mod_name, rssi, len, data, module
                 FROM sniffer_packets
                 WHERE rssi >= ?1 AND len >= ?2
                 ORDER BY id ASC
                 LIMIT ?3 OFFSET ?4",
            )
            .map_err(|e| format!("prepare page query: {e}"))?;
        let rows = stmt
            .query_map(params![min_rssi, min_len, limit, offset], |row| {
                Ok(SnifferRecord {
                    id: row.get(0)?,
                    timestamp_ms: row.get(1)?,
                    freq: row.get(2)?,
                    mod_name: row.get(3)?,
                    rssi: row.get(4)?,
                    len: row.get(5)?,
                    data: row.get(6)?,
                    module: row.get(7)?,
                })
            })
            .map_err(|e| format!("query page rows: {e}"))?
            .collect::<Result<Vec<_>, _>>()
            .map_err(|e| format!("collect page rows: {e}"))?;
        Ok(SnifferPage {
            rows,
            total_count,
            filtered_count,
            offset,
            limit,
            storage_bytes: self.storage_bytes(),
        })
    }

    fn get_record(&self, id: i64) -> Result<Option<SnifferRecord>, String> {
        let mut stmt = self
            .conn
            .prepare(
                "SELECT id, timestamp_ms, freq, mod_name, rssi, len, data, module
                 FROM sniffer_packets
                 WHERE id = ?1",
            )
            .map_err(|e| format!("prepare row query: {e}"))?;
        let mut rows = stmt.query(params![id]).map_err(|e| format!("query row: {e}"))?;
        if let Some(row) = rows.next().map_err(|e| format!("next row: {e}"))? {
            Ok(Some(SnifferRecord {
                id: row.get(0).map_err(|e| format!("row id: {e}"))?,
                timestamp_ms: row.get(1).map_err(|e| format!("row timestamp: {e}"))?,
                freq: row.get(2).map_err(|e| format!("row freq: {e}"))?,
                mod_name: row.get(3).map_err(|e| format!("row mod: {e}"))?,
                rssi: row.get(4).map_err(|e| format!("row rssi: {e}"))?,
                len: row.get(5).map_err(|e| format!("row len: {e}"))?,
                data: row.get(6).map_err(|e| format!("row data: {e}"))?,
                module: row.get(7).map_err(|e| format!("row module: {e}"))?,
            }))
        } else {
            Ok(None)
        }
    }

    fn export_csv(&self, path: &str, min_rssi: i64, min_len: i64) -> Result<SnifferSessionStats, String> {
        let mut writer = csv::Writer::from_path(path).map_err(|e| format!("open csv writer: {e}"))?;
        writer
            .write_record([
                "time",
                "frequency_mhz",
                "modulation",
                "rssi",
                "data_length",
                "raw_data_hex",
            ])
            .map_err(|e| format!("write csv header: {e}"))?;

        let mut stmt = self
            .conn
            .prepare(
                "SELECT timestamp_ms, freq, mod_name, rssi, len, data
                 FROM sniffer_packets
                 WHERE rssi >= ?1 AND len >= ?2
                 ORDER BY id ASC",
            )
            .map_err(|e| format!("prepare csv query: {e}"))?;
        let mut rows = stmt.query(params![min_rssi, min_len]).map_err(|e| format!("query csv rows: {e}"))?;
        while let Some(row) = rows.next().map_err(|e| format!("iterate csv rows: {e}"))? {
            writer
                .write_record([
                    row.get::<_, i64>(0).map_err(|e| format!("csv timestamp: {e}"))?.to_string(),
                    row.get::<_, f64>(1).map_err(|e| format!("csv freq: {e}"))?.to_string(),
                    row.get::<_, String>(2).map_err(|e| format!("csv mod: {e}"))?,
                    row.get::<_, i64>(3).map_err(|e| format!("csv rssi: {e}"))?.to_string(),
                    row.get::<_, i64>(4).map_err(|e| format!("csv len: {e}"))?.to_string(),
                    row.get::<_, String>(5).map_err(|e| format!("csv data: {e}"))?,
                ])
                .map_err(|e| format!("write csv row: {e}"))?;
        }
        writer.flush().map_err(|e| format!("flush csv: {e}"))?;
        self.stats()
    }

    fn stats(&self) -> Result<SnifferSessionStats, String> {
        Ok(SnifferSessionStats {
            total_count: self.count_all()?,
            storage_bytes: self.storage_bytes(),
        })
    }

    fn count_all(&self) -> Result<i64, String> {
        self.conn
            .query_row("SELECT COUNT(*) FROM sniffer_packets", [], |row| row.get(0))
            .map_err(|e| format!("count all rows: {e}"))
    }

    fn count_filtered(&self, min_rssi: i64, min_len: i64) -> Result<i64, String> {
        self.conn
            .query_row(
                "SELECT COUNT(*) FROM sniffer_packets WHERE rssi >= ?1 AND len >= ?2",
                params![min_rssi, min_len],
                |row| row.get(0),
            )
            .map_err(|e| format!("count filtered rows: {e}"))
    }

    fn storage_bytes(&self) -> u64 {
        fs::metadata(&self.path).map(|meta| meta.len()).unwrap_or(0)
    }
}

fn session_db_path(base_dir: &Path) -> Result<PathBuf, String> {
    fs::create_dir_all(base_dir).map_err(|e| format!("create recorder dir: {e}"))?;
    cleanup_old_session_dbs(base_dir);
    let ts = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|e| format!("clock error: {e}"))?
        .as_millis();
    Ok(base_dir.join(format!("{SESSION_PREFIX}{ts}.db")))
}

fn cleanup_old_session_dbs(base_dir: &Path) {
    let Ok(entries) = fs::read_dir(base_dir) else {
        return;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        let Some(name) = path.file_name().and_then(|value| value.to_str()) else {
            continue;
        };
        if name.starts_with(SESSION_PREFIX) {
            let _ = fs::remove_file(path);
        }
    }
}

fn with_store<T>(f: impl FnOnce(&mut RecorderStore) -> Result<T, String>) -> Result<T, String> {
    let store = STORE
        .get()
        .ok_or_else(|| "recorder store not initialized".to_string())?;
    let mut guard = store.lock().map_err(|_| "recorder store lock poisoned".to_string())?;
    f(&mut guard)
}

pub fn init_session_store(base_dir: PathBuf) -> Result<(), String> {
    if STORE.get().is_some() {
        return Ok(());
    }
    let store = RecorderStore::new(&base_dir)?;
    let _ = STORE.set(Mutex::new(store));
    Ok(())
}

pub fn append_packets(entries: Vec<SnifferPacketInput>) -> Result<SnifferSessionStats, String> {
    with_store(|store| store.append_many(entries))
}

pub fn clear_packets() -> Result<(), String> {
    with_store(|store| store.clear())
}

pub fn page_packets(offset: i64, limit: i64, min_rssi: i64, min_len: i64) -> Result<SnifferPage, String> {
    with_store(|store| store.page(offset.max(0), limit.max(1), min_rssi, min_len))
}

pub fn get_packet(id: i64) -> Result<Option<SnifferRecord>, String> {
    with_store(|store| store.get_record(id))
}

pub fn export_packets_csv(path: &str, min_rssi: i64, min_len: i64) -> Result<SnifferSessionStats, String> {
    with_store(|store| store.export_csv(path, min_rssi, min_len))
}

pub fn session_stats() -> Result<SnifferSessionStats, String> {
    with_store(|store| store.stats())
}