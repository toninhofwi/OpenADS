use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};
use serde_json::Value;

#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct Health {
    pub status: String,
    pub engine: String,
    pub mode: String,
    pub data_dir: String,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct Session {
    pub id: u64,
    pub peer_ip: String,
    pub peer_port: u16,
    pub user: String,
    pub data_dir: String,
    pub connected_secs: i64,
    pub idle_secs: i64,
    pub frames_in: u64,
    pub frames_out: u64,
    pub open_tables: u32,
}

#[derive(Debug, Clone, Default)]
pub struct HttpSnapshot {
    pub health: Option<Health>,
    pub sessions: Vec<Session>,
    pub server_info: Option<Value>,
    pub error: Option<String>,
}

pub struct HttpClient {
    base: String,
    client: reqwest::blocking::Client,
}

impl HttpClient {
    pub fn new(base: &str) -> Self {
        let base = base.trim_end_matches('/').to_string();
        Self {
            base,
            client: reqwest::blocking::Client::builder()
                .timeout(std::time::Duration::from_secs(5))
                .build()
                .expect("failed to build reqwest client"),
        }
    }

    pub fn poll(&self) -> HttpSnapshot {
        let mut snap = HttpSnapshot::default();
        match self.client.get(format!("{}/api/health", self.base)).send() {
            Ok(r) if r.status().is_success() => {
                match r.json::<Health>() {
                    Ok(h) => snap.health = Some(h),
                    Err(e) => snap.error = Some(format!("health JSON decode: {e}")),
                }
            }
            Ok(r) => snap.error = Some(format!("health HTTP {}", r.status())),
            Err(e) => snap.error = Some(format!("health: {e}")),
        }
        match self
            .client
            .get(format!("{}/api/server/sessions", self.base))
            .send()
        {
            Ok(r) if r.status().is_success() => {
                if let Ok(v) = r.json::<Value>() {
                    if let Some(arr) = v.get("sessions").and_then(|a| a.as_array()) {
                        for item in arr {
                            if let Ok(s) = serde_json::from_value::<Session>(item.clone()) {
                                snap.sessions.push(s);
                            }
                        }
                    }
                }
            }
            Ok(r) => {
                if snap.error.is_none() {
                    snap.error = Some(format!("sessions HTTP {}", r.status()));
                }
            }
            Err(e) => {
                if snap.error.is_none() {
                    snap.error = Some(format!("sessions: {e}"));
                }
            }
        }
        match self
            .client
            .get(format!("{}/api/server/info", self.base))
            .send()
        {
            Ok(r) if r.status().is_success() => snap.server_info = r.json().ok(),
            _ => {}
        }
        snap
    }

    pub fn kill_session(&self, id: u64) -> Result<()> {
        let url = format!("{}/api/server/sessions/{id}/kill", self.base);
        let r = self
            .client
            .post(&url)
            .send()
            .with_context(|| format!("POST {url}"))?;
        if r.status().is_success() {
            Ok(())
        } else {
            Err(anyhow::anyhow!("kill session failed: HTTP {}", r.status()))
        }
    }
}