use crate::wire::MgSnapshot;
use std::time::Instant;

const DEFAULT_CAPACITY: usize = 60;

pub struct MetricHistory {
    capacity: usize,
    connections: Vec<u64>,
    users: Vec<u64>,
    ops_per_sec: Vec<u64>,
    pkt_per_sec: Vec<u64>,
    rss_kb: Vec<u64>,
    last_ops: u64,
    last_packets: u64,
    last_sample: Option<Instant>,
}

impl Default for MetricHistory {
    fn default() -> Self {
        Self::new(DEFAULT_CAPACITY)
    }
}

impl MetricHistory {
    pub fn new(capacity: usize) -> Self {
        Self {
            capacity: capacity.max(8),
            connections: Vec::new(),
            users: Vec::new(),
            ops_per_sec: Vec::new(),
            pkt_per_sec: Vec::new(),
            rss_kb: Vec::new(),
            last_ops: 0,
            last_packets: 0,
            last_sample: None,
        }
    }

    pub fn push(&mut self, snap: &MgSnapshot) {
        let now = Instant::now();
        let (ops_rate, pkt_rate) = match self.last_sample {
            Some(prev) => {
                let secs = now.duration_since(prev).as_secs_f64().max(0.5);
                let ops = snap.operations.saturating_sub(self.last_ops) as f64 / secs;
                let pkts = snap
                    .packets_in
                    .saturating_add(snap.packets_out)
                    .saturating_sub(self.last_packets) as f64
                    / secs;
                (ops.round() as u64, pkts.round() as u64)
            }
            None => (0, 0),
        };
        self.last_ops = snap.operations;
        self.last_packets = snap.packets_in.saturating_add(snap.packets_out);
        self.last_sample = Some(now);

        trim_push(&mut self.connections, snap.connections as u64, self.capacity);
        trim_push(&mut self.users, snap.users as u64, self.capacity);
        trim_push(&mut self.ops_per_sec, ops_rate, self.capacity);
        trim_push(&mut self.pkt_per_sec, pkt_rate, self.capacity);
        trim_push(&mut self.rss_kb, snap.rss_bytes / 1024, self.capacity);
    }

    pub fn connections(&self) -> &[u64] {
        &self.connections
    }

    pub fn users(&self) -> &[u64] {
        &self.users
    }

    pub fn ops_per_sec(&self) -> &[u64] {
        &self.ops_per_sec
    }

    pub fn pkt_per_sec(&self) -> &[u64] {
        &self.pkt_per_sec
    }

    pub fn rss_kb(&self) -> &[u64] {
        &self.rss_kb
    }

    pub fn max_in(series: &[u64]) -> u64 {
        series.iter().copied().max().unwrap_or(1).max(1)
    }
}

fn trim_push(series: &mut Vec<u64>, value: u64, capacity: usize) {
    series.push(value);
    if series.len() > capacity {
        let drop = series.len() - capacity;
        series.drain(0..drop);
    }
}