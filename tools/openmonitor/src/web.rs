use crate::http_api::HttpClient;
use crate::snapshot::{MonitorConfig, MonitorSnapshot, MonitorState};
use anyhow::{Context, Result};
use axum::{
    extract::{Path, State},
    http::StatusCode,
    response::{Html, IntoResponse, Json},
    routing::{get, post},
    Router,
};
use std::sync::{Arc, RwLock};
use std::thread;
use std::time::Duration;
use tokio::net::TcpListener;

#[derive(Clone)]
struct WebState {
    cfg: MonitorConfig,
    snap: Arc<RwLock<MonitorSnapshot>>,
    poll: Arc<RwLock<MonitorState>>,
}

pub fn run(cfg: MonitorConfig, host: &str, port: u16) -> Result<()> {
    let poll = Arc::new(RwLock::new(MonitorState {
        mg: None,
        mg_err: None,
        http: Default::default(),
        history: Default::default(),
    }));

    {
        let mut state = poll.write().unwrap();
        state.refresh(&cfg);
    }

    let initial = poll.read().unwrap().to_snapshot(&cfg);
    let snap = Arc::new(RwLock::new(initial));
    let web = WebState {
        cfg: cfg.clone(),
        snap: snap.clone(),
        poll: poll.clone(),
    };

    let interval = Duration::from_secs(cfg.interval_secs.max(1));
    let poller_cfg = cfg.clone();
    thread::spawn(move || loop {
        thread::sleep(interval);
        if let Ok(mut state) = poll.write() {
            state.refresh(&poller_cfg);
            if let Ok(mut out) = snap.write() {
                *out = state.to_snapshot(&poller_cfg);
            }
        }
    });

    let rt = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()
        .context("tokio runtime")?;

    rt.block_on(async move {
        let app = Router::new()
            .route("/", get(index))
            .route("/api/snapshot", get(api_snapshot))
            .route("/api/sessions/{id}/kill", post(api_kill_session))
            .with_state(web);

        let addr = format!("{host}:{port}");
        let listener = TcpListener::bind(&addr)
            .await
            .with_context(|| format!("bind web UI {addr}"))?;
        eprintln!("[openmonitor] web UI http://{addr}/");
        axum::serve(listener, app)
            .await
            .context("axum serve")?;
        Ok::<(), anyhow::Error>(())
    })?;

    Ok(())
}

async fn index() -> Html<&'static str> {
    Html(include_str!("../assets/dashboard.html"))
}

async fn api_snapshot(State(st): State<WebState>) -> Json<MonitorSnapshot> {
    Json(st.snap.read().unwrap().clone())
}

async fn api_kill_session(
    State(st): State<WebState>,
    Path(id): Path<u64>,
) -> impl IntoResponse {
    let Some(base) = st.cfg.http.clone() else {
        return (StatusCode::BAD_REQUEST, "monitor started without --http").into_response();
    };
    let poll = st.poll.clone();
    let snap = st.snap.clone();
    let cfg = st.cfg.clone();
    let res = tokio::task::spawn_blocking(move || {
        let client = HttpClient::new(&base);
        client.kill_session(id).map(|_| {
            if let Ok(mut state) = poll.write() {
                state.refresh(&cfg);
                if let Ok(mut out) = snap.write() {
                    *out = state.to_snapshot(&cfg);
                }
            }
        })
    })
    .await;

    match res {
        Ok(Ok(())) => StatusCode::OK.into_response(),
        Ok(Err(e)) => (StatusCode::BAD_GATEWAY, e.to_string()).into_response(),
        Err(e) => (StatusCode::INTERNAL_SERVER_ERROR, e.to_string()).into_response(),
    }
}