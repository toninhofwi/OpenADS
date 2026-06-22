mod history;
mod http_api;
mod snapshot;
mod web;
mod wire;

use anyhow::Result;
use clap::Parser;
use crossterm::event::{self, Event, KeyCode, KeyEventKind};
use crossterm::terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen};
use crossterm::ExecutableCommand;
use http_api::HttpClient;
use ratatui::layout::{Constraint, Direction, Layout};
use ratatui::style::{Color, Modifier, Style};
use ratatui::symbols;
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Cell, Paragraph, Row, Sparkline, Table};
use ratatui::Frame;
use snapshot::{MonitorConfig, MonitorState};
use std::io::stdout;
use std::time::{Duration, Instant};

#[derive(Parser, Debug)]
#[command(name = "openmonitor", about = "Monitor for openads server")]
struct Args {
    /// Wire host (default 127.0.0.1)
    #[arg(long, default_value = "127.0.0.1")]
    wire_host: String,

    /// Wire TCP port (openads server lab default 16262)
    #[arg(long, default_value_t = 16262)]
    wire_port: u16,

    /// HTTP base URL for Studio API (optional, e.g. http://127.0.0.1:6263)
    #[arg(long)]
    http: Option<String>,

    /// Print one snapshot as text and exit (for scripts / CI)
    #[arg(long)]
    once: bool,

    /// Web dashboard (sober UI in the browser)
    #[arg(long)]
    web: bool,

    /// Web UI bind host
    #[arg(long, default_value = "127.0.0.1")]
    web_host: String,

    /// Web UI port (default 9850 — avoids 8080/16263 collisions)
    #[arg(long, default_value_t = 9850)]
    web_port: u16,

    /// Refresh interval in seconds
    #[arg(long, default_value_t = 2)]
    interval: u64,
}

impl Args {
    fn monitor_config(&self) -> MonitorConfig {
        MonitorConfig {
            wire_host: self.wire_host.clone(),
            wire_port: self.wire_port,
            http: self.http.clone(),
            interval_secs: self.interval,
        }
    }
}

struct TuiState {
    inner: MonitorState,
    selected_session: usize,
    last_refresh: Instant,
    status_msg: String,
    show_graphs: bool,
}

impl TuiState {
    fn refresh(&mut self, cfg: &MonitorConfig) {
        self.inner.refresh(cfg);
        self.last_refresh = Instant::now();
        let len = self.inner.http.sessions.len();
        if self.selected_session >= len {
            self.selected_session = len.saturating_sub(1);
        }
    }
}

fn main() -> Result<()> {
    let args = Args::parse();
    let cfg = args.monitor_config();
    let mut app = TuiState {
        inner: MonitorState {
            mg: None,
            mg_err: None,
            http: http_api::HttpSnapshot::default(),
            history: history::MetricHistory::default(),
        },
        selected_session: 0,
        last_refresh: Instant::now(),
        status_msg: String::new(),
        show_graphs: true,
    };
    app.refresh(&cfg);

    if args.once {
        print_once(&app.inner, &cfg);
        return Ok(());
    }

    if args.web {
        return web::run(cfg, &args.web_host, args.web_port);
    }

    run_tui(&mut app, &cfg, &args)
}

fn print_once(state: &MonitorState, cfg: &MonitorConfig) {
    let snap = state.to_snapshot(cfg);
    println!("openmonitor — openads server");
    println!("wire: {}:{}", cfg.wire_host, cfg.wire_port);
    if let Some(h) = &cfg.http {
        println!("http: {h}");
    }
    println!();
    let w = &snap.wire;
    if w.online {
        println!("uptime       : {}", w.uptime.as_deref().unwrap_or("—"));
        println!("connections  : {} (max {})", w.connections, w.max_connections);
        println!("users        : {} (max {})", w.users, w.max_users);
        println!("tables       : {}", w.tables);
        println!("operations   : {}", w.operations);
        println!("packets in/out: {} / {}", w.packets_in, w.packets_out);
        println!("rss          : {}", w.rss);
        println!("listener     : port {}", w.listener_port);
        println!("connected users: {}", w.wire_users.len());
        for u in &w.wire_users {
            println!("  #{} {} @ {}", u.conn_no, u.name, u.address);
        }
    } else if let Some(e) = &w.error {
        println!("[wire error] {e}");
    }
    if cfg.http.is_some() {
        let h = &snap.http;
        if let Some(status) = &h.status {
            println!();
            println!("http status  : {status}");
            println!("http mode    : {}", h.mode.as_deref().unwrap_or("—"));
            println!("data_dir     : {}", h.data_dir.as_deref().unwrap_or("—"));
        }
        println!("http sessions: {}", h.sessions.len());
        for s in &h.sessions {
            println!(
                "  id={} {}:{} user={} tables={} idle={}s",
                s.id, s.peer_ip, s.peer_port, s.user, s.open_tables, s.idle_secs
            );
        }
        if let Some(e) = &h.error {
            println!("[http error] {e}");
        }
    }
}

fn run_tui(app: &mut TuiState, cfg: &MonitorConfig, args: &Args) -> Result<()> {
    enable_raw_mode()?;
    stdout().execute(EnterAlternateScreen)?;
    let mut term = ratatui::init();

    let tick = Duration::from_secs(cfg.interval_secs);
    let mut needs_redraw = true;

    let res = (|| -> Result<()> {
        loop {
            if needs_redraw || app.last_refresh.elapsed() >= tick {
                app.refresh(cfg);
                needs_redraw = true;
            }

            if needs_redraw {
                term.draw(|f| ui(f, app, cfg, args))?;
                needs_redraw = false;
            }

            if event::poll(Duration::from_millis(200))? {
                if let Event::Key(key) = event::read()? {
                    if key.kind != KeyEventKind::Press {
                        continue;
                    }
                    match key.code {
                        KeyCode::Char('q') | KeyCode::Esc => break,
                        KeyCode::Char('r') => {
                            app.refresh(cfg);
                            app.status_msg = "refreshed".into();
                            needs_redraw = true;
                        }
                        KeyCode::Up => {
                            app.selected_session = app.selected_session.saturating_sub(1);
                            needs_redraw = true;
                        }
                        KeyCode::Down => {
                            if !app.inner.http.sessions.is_empty() {
                                app.selected_session = (app.selected_session + 1)
                                    .min(app.inner.http.sessions.len() - 1);
                            }
                            needs_redraw = true;
                        }
                        KeyCode::Char('g') => {
                            app.show_graphs = !app.show_graphs;
                            app.status_msg = if app.show_graphs {
                                "graphs on".into()
                            } else {
                                "graphs off".into()
                            };
                            needs_redraw = true;
                        }
                        KeyCode::Char('k') => {
                            if let (Some(base), Some(sess)) =
                                (&cfg.http, app.inner.http.sessions.get(app.selected_session))
                            {
                                let client = HttpClient::new(base);
                                match client.kill_session(sess.id) {
                                    Ok(()) => {
                                        app.status_msg = format!("killed session {}", sess.id);
                                        app.refresh(cfg);
                                    }
                                    Err(e) => app.status_msg = format!("kill failed: {e}"),
                                }
                                needs_redraw = true;
                            } else {
                                app.status_msg =
                                    "kill needs --http and a selected session".into();
                                needs_redraw = true;
                            }
                        }
                        _ => {}
                    }
                }
            }
        }
        Ok(())
    })();

    disable_raw_mode()?;
    stdout().execute(LeaveAlternateScreen)?;
    ratatui::restore();
    res
}

fn ui(f: &mut Frame, app: &TuiState, cfg: &MonitorConfig, args: &Args) {
    let graph_rows = if app.show_graphs { 7 } else { 0 };
    let comm_rows = if app.show_graphs { 6 } else { 8 };
    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(3),
            Constraint::Min(5),
            Constraint::Length(graph_rows),
            Constraint::Length(comm_rows),
            Constraint::Length(3),
        ])
        .split(f.area());

    let title = format!(
        " openmonitor — openads server  wire {}:{}",
        cfg.wire_host, cfg.wire_port
    );
    let http_line = cfg
        .http
        .as_deref()
        .map(|u| format!("  http {u}"))
        .unwrap_or_default();
    f.render_widget(
        Paragraph::new(Line::from(vec![
            Span::styled(title, Style::default().add_modifier(Modifier::BOLD)),
            Span::raw(http_line),
        ]))
        .block(Block::default().borders(Borders::ALL).title("OpenMonitor")),
        chunks[0],
    );

    let mid = Layout::default()
        .direction(Direction::Horizontal)
        .constraints([Constraint::Percentage(45), Constraint::Percentage(55)])
        .split(chunks[1]);

    f.render_widget(activity_panel(app), mid[0]);
    f.render_widget(sessions_panel(app), mid[1]);
    let comm_idx = if app.show_graphs { 3 } else { 2 };
    let foot_idx = if app.show_graphs { 4 } else { 3 };
    if app.show_graphs {
        render_graphs(f, app, chunks[2]);
    }
    f.render_widget(comm_panel(app), chunks[comm_idx]);
    let web_hint = if args.web { "" } else { "  w=use --web" };
    f.render_widget(
        Paragraph::new(format!(
            " {}  |  q quit  r refresh  g graphs  up/down  k kill (HTTP){}",
            app.status_msg, web_hint
        ))
        .style(Style::default().fg(Color::DarkGray)),
        chunks[foot_idx],
    );
}

fn render_graphs(f: &mut Frame, app: &TuiState, area: ratatui::layout::Rect) {
    let cols = Layout::default()
        .direction(Direction::Horizontal)
        .constraints([
            Constraint::Percentage(20),
            Constraint::Percentage(20),
            Constraint::Percentage(20),
            Constraint::Percentage(20),
            Constraint::Percentage(20),
        ])
        .split(area);

    let panels = [
        (
            "Connections",
            app.inner.history.connections(),
            Color::Cyan,
            app.inner.mg.as_ref().map(|s| s.max_connections as u64).unwrap_or(1),
        ),
        (
            "Users",
            app.inner.history.users(),
            Color::Green,
            app.inner.mg.as_ref().map(|s| s.max_users as u64).unwrap_or(1),
        ),
        (
            "Ops/s",
            app.inner.history.ops_per_sec(),
            Color::Yellow,
            history::MetricHistory::max_in(app.inner.history.ops_per_sec()),
        ),
        (
            "Pkts/s",
            app.inner.history.pkt_per_sec(),
            Color::Magenta,
            history::MetricHistory::max_in(app.inner.history.pkt_per_sec()),
        ),
        (
            "RSS KB",
            app.inner.history.rss_kb(),
            Color::Blue,
            history::MetricHistory::max_in(app.inner.history.rss_kb()),
        ),
    ];

    for (i, (title, data, color, max_val)) in panels.into_iter().enumerate() {
        let max = max_val.max(1);
        let spark_data: Vec<u64> = if data.is_empty() {
            vec![0]
        } else {
            data.to_vec()
        };
        let last = spark_data.last().copied().unwrap_or(0);
        let widget = Sparkline::default()
            .block(
                Block::default()
                    .borders(Borders::ALL)
                    .title(format!("{title} ({last})")),
            )
            .data(&spark_data)
            .max(max)
            .style(Style::default().fg(color))
            .bar_set(symbols::bar::NINE_LEVELS);
        f.render_widget(widget, cols[i]);
    }
}

fn activity_panel(app: &TuiState) -> Paragraph<'_> {
    let mut lines = Vec::new();
    if let Some(s) = &app.inner.mg {
        lines.push(Line::from(vec![
            Span::styled("Uptime ", Style::default().fg(Color::Cyan)),
            Span::raw(wire::format_uptime(s.uptime_seconds)),
        ]));
        lines.push(Line::from(format!(
            "Connections  {} / max {}",
            s.connections, s.max_connections
        )));
        lines.push(Line::from(format!(
            "Users        {} / max {}",
            s.users, s.max_users
        )));
        lines.push(Line::from(format!("Tables open   {}", s.tables)));
        lines.push(Line::from(format!("Work areas    {}", s.workareas)));
        lines.push(Line::from(format!("Locks         {}", s.locks)));
        lines.push(Line::from(format!("Operations    {}", s.operations)));
        lines.push(Line::from(format!("Errors logged {}", s.logged_errors)));
        lines.push(Line::from(format!(
            "Memory RSS    {}",
            wire::format_bytes(s.rss_bytes)
        )));
        lines.push(Line::from(format!("Listener port {}", s.server_port)));
        if !s.user_list.is_empty() {
            lines.push(Line::from(""));
            lines.push(Line::from(Span::styled(
                "Wire users",
                Style::default().add_modifier(Modifier::UNDERLINED),
            )));
            for u in s.user_list.iter().take(8) {
                lines.push(Line::from(format!(
                    "  #{} {} @ {}",
                    u.conn_no, u.name, u.address
                )));
            }
        }
    } else if let Some(e) = &app.inner.mg_err {
        lines.push(Line::from(Span::styled(
            format!("Wire offline: {e}"),
            Style::default().fg(Color::Red),
        )));
    }
    if let Some(h) = &app.inner.http.health {
        lines.push(Line::from(""));
        lines.push(Line::from(format!("Studio mode   {}", h.mode)));
        lines.push(Line::from(format!("Data dir      {}", h.data_dir)));
    }
    Paragraph::new(lines).block(
        Block::default()
            .borders(Borders::ALL)
            .title("Activity (wire Mg)"),
    )
}

fn sessions_panel(app: &TuiState) -> Table<'_> {
    let header = Row::new(vec!["ID", "Peer", "User", "Tbl", "Idle"])
        .style(Style::default().add_modifier(Modifier::BOLD))
        .bottom_margin(1);
    let rows: Vec<Row> = app
        .inner
        .http
        .sessions
        .iter()
        .enumerate()
        .map(|(i, s)| {
            let style = if i == app.selected_session {
                Style::default().bg(Color::DarkGray)
            } else {
                Style::default()
            };
            Row::new(vec![
                Cell::from(s.id.to_string()),
                Cell::from(format!("{}:{}", s.peer_ip, s.peer_port)),
                Cell::from(s.user.clone()),
                Cell::from(s.open_tables.to_string()),
                Cell::from(format!("{}s", s.idle_secs)),
            ])
            .style(style)
        })
        .collect();
    let hint = if app.inner.http.sessions.is_empty() {
        "no HTTP sessions (start server with --http-port)"
    } else {
        "HTTP wire sessions"
    };
    Table::new(
        rows,
        [
            Constraint::Length(6),
            Constraint::Length(18),
            Constraint::Min(10),
            Constraint::Length(4),
            Constraint::Length(6),
        ],
    )
    .header(header)
    .block(Block::default().borders(Borders::ALL).title(hint))
}

fn comm_panel(app: &TuiState) -> Paragraph<'_> {
    let mut lines = Vec::new();
    if let Some(s) = &app.inner.mg {
        lines.push(Line::from(format!(
            "Packets in/out     {} / {}",
            s.packets_in, s.packets_out
        )));
        lines.push(Line::from(format!(
            "Bytes in/out       {} / {}",
            wire::format_bytes(s.bytes_in),
            wire::format_bytes(s.bytes_out)
        )));
        lines.push(Line::from(format!(
            "Disconnects        {}   Partial connects {}",
            s.disconnects, s.partial_connects
        )));
        if !s.table_list.is_empty() {
            lines.push(Line::from(Span::styled(
                "Open tables (wire)",
                Style::default().add_modifier(Modifier::UNDERLINED),
            )));
            for t in s.table_list.iter().take(5) {
                lines.push(Line::from(format!(
                    "  {} — {} (#{})",
                    t.name, t.user, t.conn_no
                )));
            }
        }
    }
    if let Some(info) = &app.inner.http.server_info {
        if let Some(v) = info.get("version").and_then(|v| v.as_str()) {
            lines.push(Line::from(format!("Engine version     {v}")));
        }
        if let Some(n) = info.get("tables").and_then(|v| v.as_array()) {
            lines.push(Line::from(format!("Data tables (disk) {}", n.len())));
        }
    }
    if let Some(e) = &app.inner.http.error {
        lines.push(Line::from(Span::styled(
            format!("HTTP: {e}"),
            Style::default().fg(Color::Yellow),
        )));
    }
    Paragraph::new(lines).block(
        Block::default()
            .borders(Borders::ALL)
            .title("Communication / data"),
    )
}