use anyhow::{anyhow, Context, Result};
use std::io::{Read, Write};
use std::net::TcpStream;
use std::time::Duration;

const OPCODE_MG_REQUEST: u8 = 0xA2;
const OPCODE_MG_REPLY_ACK: u8 = 0xA3;

#[derive(Debug, Clone, Default)]
pub struct MgUser {
    pub name: String,
    pub address: String,
    pub os_login: String,
    pub conn_no: u16,
    pub connected_at: u64,
}

#[derive(Debug, Clone, Default)]
pub struct MgTable {
    pub name: String,
    pub user: String,
    pub conn_no: u16,
    pub open_mode: u16,
    pub lock_type: u16,
}

#[derive(Debug, Clone, Default)]
pub struct MgSnapshot {
    pub users: u32,
    pub connections: u32,
    pub workareas: u32,
    pub tables: u32,
    pub indexes: u32,
    pub locks: u32,
    pub worker_threads: u32,
    pub server_type: u16,
    pub rss_bytes: u64,
    pub server_port: u16,
    pub uptime_seconds: u64,
    pub packets_in: u64,
    pub packets_out: u64,
    pub bytes_in: u64,
    pub bytes_out: u64,
    pub disconnects: u64,
    pub partial_connects: u64,
    pub operations: u64,
    pub logged_errors: u64,
    pub max_users: u32,
    pub max_connections: u32,
    pub user_list: Vec<MgUser>,
    pub table_list: Vec<MgTable>,
}

struct Reader<'a> {
    data: &'a [u8],
    pos: usize,
}

impl<'a> Reader<'a> {
    fn new(data: &'a [u8]) -> Self {
        Self { data, pos: 0 }
    }

    fn remaining(&self) -> usize {
        self.data.len().saturating_sub(self.pos)
    }

    fn u16(&mut self) -> Result<u16> {
        if self.remaining() < 2 {
            return Err(anyhow!("truncated u16"));
        }
        let v = u16::from_le_bytes([self.data[self.pos], self.data[self.pos + 1]]);
        self.pos += 2;
        Ok(v)
    }

    fn u32(&mut self) -> Result<u32> {
        if self.remaining() < 4 {
            return Err(anyhow!("truncated u32"));
        }
        let mut buf = [0u8; 4];
        buf.copy_from_slice(&self.data[self.pos..self.pos + 4]);
        self.pos += 4;
        Ok(u32::from_le_bytes(buf))
    }

    fn u64(&mut self) -> Result<u64> {
        if self.remaining() < 8 {
            return Err(anyhow!("truncated u64"));
        }
        let mut buf = [0u8; 8];
        buf.copy_from_slice(&self.data[self.pos..self.pos + 8]);
        self.pos += 8;
        Ok(u64::from_le_bytes(buf))
    }

    fn str(&mut self) -> Result<String> {
        let n = self.u16()? as usize;
        if self.remaining() < n {
            return Err(anyhow!("truncated string"));
        }
        let s = std::str::from_utf8(&self.data[self.pos..self.pos + n])
            .context("invalid utf-8 in mg snapshot")?
            .to_string();
        self.pos += n;
        Ok(s)
    }
}

pub fn decode_mg_snapshot(payload: &[u8]) -> Result<MgSnapshot> {
    let mut r = Reader::new(payload);
    let mut s = MgSnapshot::default();
    s.users = r.u32()?;
    s.connections = r.u32()?;
    s.workareas = r.u32()?;
    s.tables = r.u32()?;
    s.indexes = r.u32()?;
    s.locks = r.u32()?;
    s.worker_threads = r.u32()?;
    s.server_type = r.u16()?;
    s.rss_bytes = r.u64()?;
    s.server_port = r.u16()?;
    s.uptime_seconds = r.u64()?;
    s.packets_in = r.u64()?;
    s.packets_out = r.u64()?;
    s.bytes_in = r.u64()?;
    s.bytes_out = r.u64()?;
    s.disconnects = r.u64()?;
    s.partial_connects = r.u64()?;
    s.operations = r.u64()?;
    s.logged_errors = r.u64()?;
    s.max_users = r.u32()?;
    s.max_connections = r.u32()?;
    let _ = r.u32()?;
    let _ = r.u32()?;
    let _ = r.u32()?;
    let _ = r.u32()?;

    let nu = r.u32()?;
    for _ in 0..nu {
        s.user_list.push(MgUser {
            name: r.str()?,
            address: r.str()?,
            os_login: r.str()?,
            conn_no: r.u16()?,
            connected_at: r.u64()?,
        });
    }
    let nt = r.u32()?;
    for _ in 0..nt {
        s.table_list.push(MgTable {
            name: r.str()?,
            user: r.str()?,
            conn_no: r.u16()?,
            open_mode: r.u16()?,
            lock_type: r.u16()?,
        });
    }
    Ok(s)
}

fn write_frame(stream: &mut TcpStream, opcode: u8, payload: &[u8]) -> Result<()> {
    let len = payload.len() as u32;
    let mut header = [0u8; 5];
    header[0..4].copy_from_slice(&len.to_be_bytes());
    header[4] = opcode;
    stream
        .write_all(&header)
        .context("write frame header")?;
    if !payload.is_empty() {
        stream.write_all(payload).context("write frame payload")?;
    }
    Ok(())
}

fn read_frame(stream: &mut TcpStream) -> Result<(u8, Vec<u8>)> {
    let mut header = [0u8; 5];
    stream
        .read_exact(&mut header)
        .context("read frame header")?;
    let len = u32::from_be_bytes([header[0], header[1], header[2], header[3]]) as usize;
    let opcode = header[4];
    let mut payload = vec![0u8; len];
    if len > 0 {
        stream
            .read_exact(&mut payload)
            .context("read frame payload")?;
    }
    Ok((opcode, payload))
}

pub fn fetch_mg_snapshot(host: &str, port: u16) -> Result<MgSnapshot> {
    let addr = format!("{host}:{port}");
    let mut stream = TcpStream::connect(&addr).with_context(|| format!("connect {addr}"))?;
    stream.set_read_timeout(Some(Duration::from_secs(5)))?;
    stream.set_write_timeout(Some(Duration::from_secs(5)))?;

    // Snapshot request: kind=0x01, arg=0
    let req_payload = [0x01u8, 0x00, 0x00];
    write_frame(&mut stream, OPCODE_MG_REQUEST, &req_payload)?;

    let (opcode, payload) = read_frame(&mut stream)?;
    if opcode != OPCODE_MG_REPLY_ACK {
        return Err(anyhow!("unexpected mg reply opcode 0x{opcode:02X}"));
    }
    decode_mg_snapshot(&payload)
}

pub fn format_uptime(secs: u64) -> String {
    let days = secs / 86_400;
    let hours = (secs % 86_400) / 3_600;
    let mins = (secs % 3_600) / 60;
    let s = secs % 60;
    format!("{days}d {hours}h {mins}m {s}s")
}

pub fn format_bytes(n: u64) -> String {
    const KB: u64 = 1024;
    const MB: u64 = KB * 1024;
    const GB: u64 = MB * 1024;
    if n >= GB {
        format!("{:.2} GB", n as f64 / GB as f64)
    } else if n >= MB {
        format!("{:.2} MB", n as f64 / MB as f64)
    } else if n >= KB {
        format!("{:.1} KB", n as f64 / KB as f64)
    } else {
        format!("{n} B")
    }
}