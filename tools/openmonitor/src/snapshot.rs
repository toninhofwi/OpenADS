use crate::history::MetricHistory;
use crate::http_api::{Health, HttpSnapshot, Session};
use crate::wire::{self, MgSnapshot};
use serde::Serialize;

#[derive(Debug, Clone)]
pub struct MonitorConfig {
    pub wire_host: String,
    pub wire_port: u16,
    pub http: Option<String>,
    pub interval_secs: u64,
}

#[derive(Debug, Clone, Serialize)]
pub struct SeriesView {
    pub label: String,
    pub values: Vec<u64>,
    pub current: u64,
    pub max: u64,
}

#[derive(Debug, Clone, Serialize)]
pub struct WireUserView {
    pub conn_no: u16,
    pub name: String,
    pub address: String,
}

#[derive(Debug, Clone, Serialize)]
pub struct WireView {
    pub online: bool,
    pub error: Option<String>,
    pub uptime: Option<String>,
    pub connections: u32,
    pub max_connections: u32,
    pub users: u32,
    pub max_users: u32,
    pub tables: u32,
    pub operations: u64,
    pub packets_in: u64,
    pub packets_out: u64,
    pub bytes_in: String,
    pub bytes_out: String,
    pub rss: String,
    pub listener_port: u16,
    pub wire_users: Vec<WireUserView>,
}

#[derive(Debug, Clone, Serialize)]
pub struct HttpView {
    pub configured: bool,
    pub error: Option<String>,
    pub status: Option<String>,
    pub mode: Option<String>,
    pub data_dir: Option<String>,
    pub version: Option<String>,
    pub disk_tables: Option<usize>,
    pub sessions: Vec<Session>,
}

#[derive(Debug, Clone, Serialize)]
pub struct MonitorSnapshot {
    pub wire_host: String,
    pub wire_port: u16,
    pub http_url: Option<String>,
    pub updated_at: String,
    pub wire: WireView,
    pub http: HttpView,
    pub series: Vec<SeriesView>,
}

pub struct MonitorState {
    pub mg: Option<MgSnapshot>,
    pub mg_err: Option<String>,
    pub http: HttpSnapshot,
    pub history: MetricHistory,
}

impl MonitorState {
    pub fn refresh(&mut self, cfg: &MonitorConfig) {
        match wire::fetch_mg_snapshot(&cfg.wire_host, cfg.wire_port) {
            Ok(s) => {
                self.history.push(&s);
                self.mg = Some(s);
                self.mg_err = None;
            }
            Err(e) => {
                self.mg = None;
                self.mg_err = Some(e.to_string());
            }
        }
        if let Some(base) = &cfg.http {
            self.http = crate::http_api::HttpClient::new(base).poll();
        } else {
            self.http = HttpSnapshot::default();
        }
    }

    pub fn to_snapshot(&self, cfg: &MonitorConfig) -> MonitorSnapshot {
        let wire = build_wire_view(&self.mg, &self.mg_err);
        let http = build_http_view(&self.http, cfg.http.is_some());
        let series = build_series(&self.history, &self.mg);
        MonitorSnapshot {
            wire_host: cfg.wire_host.clone(),
            wire_port: cfg.wire_port,
            http_url: cfg.http.clone(),
            updated_at: chrono::Local::now().format("%H:%M:%S").to_string(),
            wire,
            http,
            series,
        }
    }
}

fn build_wire_view(mg: &Option<MgSnapshot>, err: &Option<String>) -> WireView {
    match mg {
        Some(s) => WireView {
            online: true,
            error: None,
            uptime: Some(wire::format_uptime(s.uptime_seconds)),
            connections: s.connections,
            max_connections: s.max_connections,
            users: s.users,
            max_users: s.max_users,
            tables: s.tables,
            operations: s.operations,
            packets_in: s.packets_in,
            packets_out: s.packets_out,
            bytes_in: wire::format_bytes(s.bytes_in),
            bytes_out: wire::format_bytes(s.bytes_out),
            rss: wire::format_bytes(s.rss_bytes),
            listener_port: s.server_port,
            wire_users: s
                .user_list
                .iter()
                .map(|u| WireUserView {
                    conn_no: u.conn_no,
                    name: u.name.clone(),
                    address: u.address.clone(),
                })
                .collect(),
        },
        None => WireView {
            online: false,
            error: err.clone(),
            uptime: None,
            connections: 0,
            max_connections: 0,
            users: 0,
            max_users: 0,
            tables: 0,
            operations: 0,
            packets_in: 0,
            packets_out: 0,
            bytes_in: "—".into(),
            bytes_out: "—".into(),
            rss: "—".into(),
            listener_port: 0,
            wire_users: vec![],
        },
    }
}

fn build_http_view(http: &HttpSnapshot, configured: bool) -> HttpView {
    let health: Option<&Health> = http.health.as_ref();
    HttpView {
        configured,
        error: http.error.clone(),
        status: health.map(|h| h.status.clone()),
        mode: health.map(|h| h.mode.clone()),
        data_dir: health.map(|h| h.data_dir.clone()),
        version: http
            .server_info
            .as_ref()
            .and_then(|v| v.get("version"))
            .and_then(|v| v.as_str())
            .map(str::to_string),
        disk_tables: http
            .server_info
            .as_ref()
            .and_then(|v| v.get("tables"))
            .and_then(|v| v.as_array())
            .map(|a| a.len()),
        sessions: http.sessions.clone(),
    }
}

fn build_series(history: &MetricHistory, mg: &Option<MgSnapshot>) -> Vec<SeriesView> {
    let max_conn = mg.as_ref().map(|s| s.max_connections as u64).unwrap_or(1);
    let max_users = mg.as_ref().map(|s| s.max_users as u64).unwrap_or(1);
    vec![
        series_one("Conexões", history.connections(), max_conn),
        series_one("Usuários", history.users(), max_users),
        series_one(
            "Ops/s",
            history.ops_per_sec(),
            MetricHistory::max_in(history.ops_per_sec()),
        ),
        series_one(
            "Pkts/s",
            history.pkt_per_sec(),
            MetricHistory::max_in(history.pkt_per_sec()),
        ),
        series_one(
            "RSS KB",
            history.rss_kb(),
            MetricHistory::max_in(history.rss_kb()),
        ),
    ]
}

fn series_one(label: &str, data: &[u64], scale_max: u64) -> SeriesView {
    let values: Vec<u64> = if data.is_empty() { vec![0] } else { data.to_vec() };
    let current = *values.last().unwrap_or(&0);
    let peak = values.iter().copied().max().unwrap_or(1).max(scale_max).max(1);
    SeriesView {
        label: label.to_string(),
        values,
        current,
        max: peak,
    }
}