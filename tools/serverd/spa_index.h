#pragma once

#if defined(OPENADS_WITH_HTTP)

namespace openads::studio {

// studio.web.0.2 — phpMyAdmin-style admin UI:
//
//   - Left tree: list of *.dbf files; click selects a table.
//   - Top tabs (right pane): Browse | Structure | Insert | SQL | Server.
//   - Browse: paginated row grid with edit/delete buttons per row.
//   - Structure: column metadata + record count.
//   - Insert: empty form for a new row.
//   - SQL: free-form editor + result grid.
//   - Server: engine version + listed tables.
//
// All UI state lives in the SPA; the server is stateless. Each
// REST round-trip opens a short-lived ABI connection.

inline constexpr const char kSpaIndexHtml[] = R"OPENADS_SPA(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>OpenADS Studio</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
  * { box-sizing: border-box; }
  body { font-family: -apple-system, system-ui, Segoe UI, Roboto, sans-serif;
         margin: 0; height: 100vh; display: flex; flex-direction: column;
         background: #1e1e1e; color: #ddd; font-size: 19px; }
  header { background: #0d6efd; color: white; padding: 14px 22px;
           display: flex; align-items: center; justify-content: space-between; }
  header h1 { font-size: 24px; margin: 0; font-weight: 500; }
  header .status { font-size: 17px; opacity: 0.85; }
  main { flex: 1; display: flex; min-height: 0; }
  aside { width: 320px; background: #252526; border-right: 1px solid #333;
          overflow-y: auto; flex-shrink: 0; }
  aside h2 { font-size: 15px; text-transform: uppercase; opacity: 0.6;
             margin: 16px 18px 10px; }
  aside ul { list-style: none; padding: 0; margin: 0; }
  aside li { padding: 10px 18px; cursor: pointer; font-size: 18px; }
  aside li:hover { background: #2d2d30; }
  aside li.active { background: #094771; color: white; }
  section.work { flex: 1; display: flex; flex-direction: column; min-width: 0; }
  nav.tabs { background: #2d2d30; padding: 0 12px; display: flex;
             gap: 1px; border-bottom: 1px solid #094771; }
  nav.tabs button { background: #2d2d30; color: #aaa; border: 0;
                    padding: 12px 26px; cursor: pointer; font-size: 17px; }
  nav.tabs button:hover { background: #3a3a3d; color: white; }
  nav.tabs button.active { background: #094771; color: white; }
  .pane { flex: 1; overflow: auto; padding: 20px; min-height: 0; }
  .pane.hidden { display: none; }
  .toolbar { display: flex; gap: 12px; align-items: center;
             margin-bottom: 14px; flex-wrap: wrap; }
  .toolbar button, .btn { background: #0d6efd; color: white; border: 0;
                          padding: 9px 22px; cursor: pointer;
                          border-radius: 3px; font-size: 17px; }
  .toolbar button:hover, .btn:hover { background: #0b5ed7; }
  .btn-danger { background: #d9534f; }
  .btn-danger:hover { background: #c9302c; }
  .btn-secondary { background: #444; }
  .btn-secondary:hover { background: #555; }
  .err { color: #f48771; font-size: 17px; }
  .ok  { color: #6cc24a; font-size: 17px; }
  table { border-collapse: collapse; width: 100%; }
  th, td { padding: 8px 12px; border: 1px solid #333; text-align: left;
           white-space: nowrap; vertical-align: top; font-size: 17px; }
  th { background: #2d2d30; font-weight: 500; position: sticky; top: 0;
       z-index: 1; }
  tr.deleted td { opacity: 0.4; text-decoration: line-through; }
  .editor textarea { background: #1e1e1e; color: #ddd; border: 1px solid #333;
                     outline: none; padding: 14px;
                     font: 18px Consolas, Monaco, monospace; resize: vertical;
                     width: 100%; min-height: 170px; }
  .form-row { display: flex; align-items: center; margin-bottom: 10px;
              gap: 14px; }
  .form-row label { width: 200px; font-size: 17px; opacity: 0.85; }
  .form-row input { flex: 1; background: #2d2d30; color: #ddd;
                    border: 1px solid #444; padding: 8px 12px;
                    font: 17px Consolas, monospace; border-radius: 2px; }
  .empty { padding: 28px; opacity: 0.5; font-size: 18px; }
  .pager { margin-top: 14px; display: flex; gap: 12px; align-items: center; }
  .pager span { font-size: 17px; opacity: 0.85; }
  .kv { display: grid; grid-template-columns: 200px 1fr; gap: 8px 22px;
        font-size: 17px; max-width: 800px; }
  .kv > div:nth-child(odd) { opacity: 0.7; }
  .modal-bg { position: fixed; inset: 0; background: rgba(0,0,0,0.6);
              display: none; align-items: center; justify-content: center;
              z-index: 10; }
  .modal-bg.show { display: flex; }
  .modal { background: #252526; border: 1px solid #094771;
           padding: 22px; border-radius: 6px; min-width: 460px;
           max-width: 90vw; max-height: 90vh; overflow: auto; }
  .modal h3 { margin: 0 0 16px; font-size: 17px; }
  .col-row { display: grid; grid-template-columns: 1.3fr 0.6fr 0.5fr 0.5fr 0.4fr;
             gap: 6px; margin-bottom: 5px; align-items: center; }
  .col-row input, .col-row select { background: #2d2d30; color: #ddd;
             border: 1px solid #444; padding: 5px 8px;
             font: 14px Consolas, monospace; border-radius: 2px; }
  .col-row button { background: #d9534f; color: white; border: 0;
             padding: 4px 8px; cursor: pointer; border-radius: 2px;
             font-size: 12px; }
  .aside-head { display: flex; align-items: center;
                justify-content: space-between; margin: 14px 16px 8px; }
  .aside-head h2 { margin: 0; }
  .aside-head button { background: #094771; color: white; border: 0;
                       width: 24px; height: 24px; cursor: pointer;
                       border-radius: 3px; font-size: 16px; line-height: 1; }
</style>
</head>
<body>
<header>
  <h1>OpenADS Studio</h1>
  <div style="display:flex;gap:18px;align-items:center">
    <a href="https://fivetechsoft.github.io/OpenADS/"
       target="_blank" rel="noopener"
       style="color:white;text-decoration:none;font-size:15px;
              opacity:0.9;border:1px solid rgba(255,255,255,0.4);
              padding:5px 12px;border-radius:3px">
      📖 Docs
    </a>
    <div class="status" id="status">…</div>
  </div>
</header>
<main>
  <aside>
    <div class="aside-head">
      <h2>Tables</h2>
      <button id="btn-new-table" title="New table">+</button>
    </div>
    <ul id="tables"></ul>
    <h2>Server</h2>
    <ul><li id="server-link">Info</li></ul>
  </aside>
  <section class="work">
    <nav class="tabs" id="tabs">
      <button data-tab="browse" class="active">Browse</button>
      <button data-tab="structure">Structure</button>
      <button data-tab="insert">Insert</button>
      <button data-tab="sql">SQL</button>
      <button data-tab="server">Server</button>
      <button data-tab="sessions">Sessions</button>
      <button data-tab="dd">Dict</button>
    </nav>

    <div id="pane-browse" class="pane">
      <div class="toolbar">
        <span id="browse-title" class="err"></span>
      </div>
      <div id="browse-grid" class="empty">Select a table on the left.</div>
      <div class="pager" id="browse-pager"></div>
    </div>

    <div id="pane-structure" class="pane hidden">
      <div id="structure-body" class="empty">Select a table.</div>
    </div>

    <div id="pane-insert" class="pane hidden">
      <div id="insert-body" class="empty">Select a table.</div>
    </div>

    <div id="pane-sql" class="pane hidden">
      <div class="editor">
        <textarea id="sql"
                  placeholder="SELECT * FROM yourtable.dbf"></textarea>
      </div>
      <div class="toolbar">
        <button id="sql-run">Run (Ctrl+Enter)</button>
        <button id="sql-export" class="btn-secondary">Export CSV</button>
        <span id="sql-status"></span>
      </div>
      <div id="sql-result" class="empty">Result will appear here.</div>
    </div>

    <div id="pane-server" class="pane hidden">
      <div id="server-body" class="empty">Loading…</div>
    </div>

    <div id="pane-sessions" class="pane hidden">
      <div class="toolbar">
        <button class="btn btn-secondary" id="sessions-refresh">Refresh</button>
        <label style="font-size:14px;opacity:0.85">
          <input type="checkbox" id="sessions-auto" checked>
          auto-refresh every 3 s
        </label>
        <span id="sessions-status"></span>
      </div>
      <div id="sessions-body" class="empty">Loading…</div>
    </div>

    <div id="pane-dd" class="pane hidden">
      <div class="toolbar">
        <select id="dd-pick" style="background:#2d2d30;color:#ddd;
                border:1px solid #444;padding:6px 10px;font-size:15px;
                border-radius:2px"></select>
        <button class="btn" id="dd-new">New dict…</button>
        <button class="btn btn-danger" id="dd-drop">Drop</button>
        <span id="dd-status"></span>
      </div>
      <div id="dd-body" class="empty">No dictionary selected.</div>
    </div>
  </section>
</main>

<div class="modal-bg" id="modal-create">
  <div class="modal">
    <h3>New Table</h3>
    <div class="form-row">
      <label>Table file name</label>
      <input id="ct-name" type="text" placeholder="newtable.dbf">
    </div>
    <div class="col-row" style="font-weight:500;opacity:0.85;font-size:13px">
      <div>Column name</div><div>Type</div><div>Length</div>
      <div>Decimals</div><div></div>
    </div>
    <div id="ct-cols"></div>
    <div class="toolbar" style="margin-top:14px">
      <button class="btn btn-secondary" id="ct-add-col">+ column</button>
      <button class="btn" id="ct-create">Create</button>
      <button class="btn btn-secondary" id="ct-cancel">Cancel</button>
      <span id="ct-status"></span>
    </div>
  </div>
</div>

<div class="modal-bg" id="modal-encrypt">
  <div class="modal">
    <h3>Encrypt table</h3>
    <p style="margin:0 0 14px;opacity:0.8">
      AES-256-CTR keyed off the password below. Schema stays plain
      text; record bodies are encrypted in place (M11.2). The
      password must be supplied to every connection that reopens
      this table afterwards.
    </p>
    <div class="form-row">
      <label>Password</label>
      <input id="enc-pw" type="password">
    </div>
    <div class="toolbar" style="margin-top:14px">
      <button class="btn" id="enc-go">Encrypt</button>
      <button class="btn btn-secondary" id="enc-cancel">Cancel</button>
      <span id="enc-status"></span>
    </div>
  </div>
</div>
)OPENADS_SPA"
R"OPENADS_SPA(<script>
const $ = id => document.getElementById(id);
const esc = s => (""+s)
  .replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;")
  .replace(/"/g,"&quot;");

let state = { table: null, schema: null,
              browseOffset: 0, browseLimit: 50 };

function setStatus(s, klass) {
  const el = $("status"); el.textContent = s;
  el.className = "status" + (klass ? " " + klass : "");
}
async function api(url, opts) {
  const r = await fetch(url, opts);
  const txt = await r.text();
  let body = null;
  try { body = JSON.parse(txt); } catch {}
  if (!r.ok || (body && body.error)) {
    throw new Error((body && body.error) || `${r.status} ${r.statusText}`);
  }
  return body;
}

function showTab(tab) {
  ["browse","structure","insert","sql","server","sessions","dd"].forEach(t => {
    $("pane-" + t).classList.toggle("hidden", t !== tab);
  });
  document.querySelectorAll("nav.tabs button").forEach(b =>
    b.classList.toggle("active", b.dataset.tab === tab));
  if (tab === "structure" && state.table) loadStructure();
  if (tab === "insert"    && state.table) loadInsertForm();
  if (tab === "browse"    && state.table) loadBrowse();
  if (tab === "server")                   loadServerInfo();
  if (tab === "sessions") {
    loadSessions();
    startSessionsAutoRefresh();
  } else {
    stopSessionsAutoRefresh();
  }
  if (tab === "dd") loadDicts();
}

async function loadTables() {
  try {
    const data = await api("/api/tables");
    setStatus(`${data.tables.length} tables · ${data.data_dir}`);
    $("tables").innerHTML = data.tables.map(t =>
      `<li data-name="${esc(t)}">${esc(t)}</li>`).join("");
    document.querySelectorAll("aside li[data-name]").forEach(li =>
      li.addEventListener("click", () => selectTable(li.dataset.name)));
  } catch (e) { setStatus("offline: " + e.message); }
}

function selectTable(name) {
  state.table = name;
  state.browseOffset = 0;
  document.querySelectorAll("aside li").forEach(li =>
    li.classList.toggle("active", li.dataset.name === name));
  showTab("browse");
}

async function loadBrowse() {
  const t = state.table;
  $("browse-title").textContent = `Table: ${t}`;
  try {
    const data = await api(`/api/tables/${encodeURIComponent(t)}` +
      `/rows?offset=${state.browseOffset}&limit=${state.browseLimit}`);
    if (!data.rows || data.rows.length === 0) {
      $("browse-grid").innerHTML = `<div class="empty">No rows.</div>`;
    } else {
      const cols = data.cols;
      const tbody = data.rows.map(r => {
        const meta = r[0];
        const cells = r.slice(1);
        const cls = meta._deleted ? "deleted" : "";
        return `<tr class="${cls}">
          <td>${meta._recno}</td>
          ${cells.map(v => `<td>${esc(v)}</td>`).join("")}
          <td><button class="btn btn-secondary" data-edit="${meta._recno}">Edit</button>
              <button class="btn btn-danger" data-delete="${meta._recno}"
                      data-deleted="${meta._deleted}">${
                meta._deleted ? "Recall" : "Delete"
              }</button></td>
        </tr>`;
      }).join("");
      $("browse-grid").innerHTML = `<table>
        <thead><tr><th>recno</th>${cols.map(c => `<th>${esc(c)}</th>`).join("")}<th>actions</th></tr></thead>
        <tbody>${tbody}</tbody>
      </table>`;
      document.querySelectorAll("[data-edit]").forEach(b =>
        b.addEventListener("click", () => editRow(+b.dataset.edit)));
      document.querySelectorAll("[data-delete]").forEach(b =>
        b.addEventListener("click", () =>
          deleteRow(+b.dataset.delete, b.dataset.deleted === "true")));
    }
    $("browse-pager").innerHTML = `
      <button class="btn btn-secondary" id="prev"
              ${state.browseOffset === 0 ? "disabled" : ""}>‹ Prev</button>
      <span>${state.browseOffset + 1}–${state.browseOffset + data.rows.length}
            of ${data.total}</span>
      <button class="btn btn-secondary" id="next"
              ${state.browseOffset + data.rows.length >= data.total ?
                "disabled" : ""}>Next ›</button>`;
    $("prev")?.addEventListener("click", () => {
      state.browseOffset = Math.max(0, state.browseOffset - state.browseLimit);
      loadBrowse();
    });
    $("next")?.addEventListener("click", () => {
      state.browseOffset += state.browseLimit; loadBrowse();
    });
  } catch (e) {
    $("browse-grid").innerHTML = `<div class="err">${esc(e.message)}</div>`;
  }
}

)OPENADS_SPA"
R"OPENADS_SPA(async function ensureSchema() {
  if (state.schema && state.schema.table === state.table) return state.schema;
  state.schema = await api(
    `/api/tables/${encodeURIComponent(state.table)}/schema`);
  return state.schema;
}

async function loadStructure() {
  try {
    const s = await ensureSchema();
    $("structure-body").innerHTML = `
      <div class="kv">
        <div>Table</div><div>${esc(s.table)}</div>
        <div>Records</div><div>${s.record_count}</div>
        <div>File size</div><div>${s.file_bytes} bytes</div>
      </div>
      <div class="toolbar" style="margin-top:14px">
        <button class="btn" id="btn-encrypt">Encrypt…</button>
        <button class="btn btn-danger" id="btn-drop">Drop table</button>
      </div>
      <h3 style="margin-top:18px;font-size:15px;opacity:0.85">Columns</h3>
      <table><thead><tr><th>#</th><th>Name</th><th>Type</th><th>Length</th><th>Decimals</th></tr></thead>
      <tbody>${s.columns.map((c,i) => `<tr>
        <td>${i+1}</td><td>${esc(c.name)}</td><td>${c.type}</td>
        <td>${c.length}</td><td>${c.decimals}</td></tr>`).join("")}</tbody></table>`;
    $("btn-drop").addEventListener("click", dropCurrentTable);
    $("btn-encrypt").addEventListener("click", openEncryptModal);
  } catch (e) {
    $("structure-body").innerHTML = `<div class="err">${esc(e.message)}</div>`;
  }
}

async function dropCurrentTable() {
  const t = state.table;
  if (!confirm(`Drop table ${t}?  This deletes the .dbf + every sidecar (.cdx/.fpt/.dbt/.lck) on disk.`)) return;
  try {
    await api(`/api/tables/${encodeURIComponent(t)}`, {method:"DELETE"});
    state.table = null; state.schema = null;
    await loadTables();
    showTab("server");
  } catch (e) { alert("DROP failed: " + e.message); }
}

function openEncryptModal() {
  $("enc-pw").value = "";
  $("enc-status").textContent = "";
  $("modal-encrypt").classList.add("show");
}
async function runEncrypt() {
  const pw = $("enc-pw").value;
  if (!pw) { $("enc-status").textContent = "password required";
             $("enc-status").className = "err"; return; }
  try {
    await api(`/api/tables/${encodeURIComponent(state.table)}/encrypt`,
      { method: "POST",
        headers: {"content-type":"application/json"},
        body: JSON.stringify({password: pw}) });
    $("modal-encrypt").classList.remove("show");
    state.schema = null;
    loadStructure();
  } catch (e) {
    $("enc-status").textContent = e.message;
    $("enc-status").className = "err";
  }
}

// CREATE TABLE wizard
function openCreateModal() {
  $("ct-name").value = "";
  $("ct-cols").innerHTML = "";
  $("ct-status").textContent = "";
  addCreateCol(); addCreateCol();
  $("modal-create").classList.add("show");
}
function addCreateCol() {
  const div = document.createElement("div");
  div.className = "col-row";
  div.innerHTML = `
    <input class="ct-cn" type="text" placeholder="COL_NAME">
    <select class="ct-ct">
      <option value="C">C (char)</option>
      <option value="N">N (numeric)</option>
      <option value="L">L (logical)</option>
      <option value="D">D (date)</option>
      <option value="M">M (memo)</option>
    </select>
    <input class="ct-cl" type="number" value="10" min="1">
    <input class="ct-cd" type="number" value="0" min="0">
    <button title="remove">×</button>`;
  div.querySelector("button").addEventListener("click", () => div.remove());
  $("ct-cols").appendChild(div);
}
async function runCreateTable() {
  const name = $("ct-name").value.trim();
  if (!name) { $("ct-status").textContent = "table name required";
               $("ct-status").className = "err"; return; }
  const cols = [...$("ct-cols").children].map(div => ({
    name:     div.querySelector(".ct-cn").value.trim(),
    type:     div.querySelector(".ct-ct").value,
    length:   parseInt(div.querySelector(".ct-cl").value, 10) || 0,
    decimals: parseInt(div.querySelector(".ct-cd").value, 10) || 0
  })).filter(c => c.name);
  if (cols.length === 0) { $("ct-status").textContent = "at least one column";
                           $("ct-status").className = "err"; return; }
  try {
    await api("/api/tables", {
      method: "POST",
      headers: {"content-type":"application/json"},
      body: JSON.stringify({name, columns: cols})});
    $("modal-create").classList.remove("show");
    await loadTables();
    selectTable(name);
  } catch (e) {
    $("ct-status").textContent = e.message;
    $("ct-status").className = "err";
  }
}

// SQL history (persistent localStorage). Up/Down arrows recall.
const SQL_HIST_KEY = "openads-studio.sql.history";
function loadHistory() {
  try { return JSON.parse(localStorage.getItem(SQL_HIST_KEY) || "[]"); }
  catch { return []; }
}
function saveHistory(arr) {
  localStorage.setItem(SQL_HIST_KEY,
                       JSON.stringify(arr.slice(-100)));
}
let sqlHistIdx = -1;
function pushHistory(sql) {
  const h = loadHistory();
  if (h.length === 0 || h[h.length-1] !== sql) h.push(sql);
  saveHistory(h);
  sqlHistIdx = h.length;
}
function recallHistory(delta) {
  const h = loadHistory();
  if (h.length === 0) return;
  sqlHistIdx = Math.max(0, Math.min(h.length, sqlHistIdx + delta));
  $("sql").value = sqlHistIdx < h.length ? h[sqlHistIdx] : "";
}

async function loadInsertForm() {
  try {
    const s = await ensureSchema();
    const body = `<form id="insert-form">
      ${s.columns.map(c =>
        `<div class="form-row">
          <label>${esc(c.name)} <small>(${c.type}/${c.length})</small></label>
          <input name="${esc(c.name)}" type="text">
        </div>`).join("")}
      <div class="toolbar" style="margin-top:14px">
        <button type="submit" class="btn">Append record</button>
        <span id="insert-status"></span>
      </div></form>`;
    $("insert-body").innerHTML = body;
    $("insert-form").addEventListener("submit", async e => {
      e.preventDefault();
      const fd = new FormData(e.target);
      const obj = {};
      for (const [k,v] of fd.entries()) obj[k] = v;
      try {
        const r = await api(
          `/api/tables/${encodeURIComponent(state.table)}/insert`,
          { method: "POST",
            headers: {"content-type":"application/json"},
            body: JSON.stringify(obj) });
        $("insert-status").textContent = `appended recno ${r.recno}`;
        $("insert-status").className = "ok";
        e.target.reset();
        state.schema = null;
      } catch (err) {
        $("insert-status").textContent = err.message;
        $("insert-status").className = "err";
      }
    });
  } catch (e) {
    $("insert-body").innerHTML = `<div class="err">${esc(e.message)}</div>`;
  }
}

async function editRow(recno) {
  try {
    const s = await ensureSchema();
    const data = await api(
      `/api/tables/${encodeURIComponent(state.table)}` +
      `/rows?offset=${recno - 1}&limit=1`);
    const row = data.rows[0];
    const cells = row.slice(1);
    const lines = s.columns.map((c, i) =>
      `<div class="form-row">
        <label>${esc(c.name)} <small>(${c.type}/${c.length})</small></label>
        <input name="${esc(c.name)}" type="text"
               value="${esc(cells[i] || "")}"></div>`).join("");
    const html = `<h3 style="font-size:13px;margin:0 0 10px">
      Edit recno ${recno} of ${esc(state.table)}</h3>
      <form id="edit-form">${lines}
      <div class="toolbar" style="margin-top:14px">
        <button type="submit" class="btn">Save</button>
        <button type="button" class="btn btn-secondary" id="edit-cancel">Cancel</button>
        <span id="edit-status"></span>
      </div></form>`;
    $("browse-grid").innerHTML = html;
    $("edit-cancel").addEventListener("click", loadBrowse);
    $("edit-form").addEventListener("submit", async e => {
      e.preventDefault();
      const fd = new FormData(e.target);
      const obj = {};
      for (const [k,v] of fd.entries()) obj[k] = v;
      try {
        await api(`/api/tables/${encodeURIComponent(state.table)}` +
          `/update?recno=${recno}`,
          { method: "POST",
            headers: {"content-type":"application/json"},
            body: JSON.stringify(obj) });
        loadBrowse();
      } catch (err) {
        $("edit-status").textContent = err.message;
        $("edit-status").className = "err";
      }
    });
  } catch (e) {
    $("browse-grid").innerHTML = `<div class="err">${esc(e.message)}</div>`;
  }
}

async function deleteRow(recno, currentlyDeleted) {
  const verb = currentlyDeleted ? "recall" : "delete";
  if (!confirm(`${verb} recno ${recno}?`)) return;
  try {
    await api(`/api/tables/${encodeURIComponent(state.table)}` +
      `/delete?recno=${recno}&recall=${currentlyDeleted ? "1" : "0"}`,
      { method: "POST" });
    loadBrowse();
  } catch (e) { alert(e.message); }
}

async function runSql() {
  const sql = $("sql").value.trim();
  if (!sql) return;
  pushHistory(sql);
  $("sql-status").textContent = "running…"; $("sql-status").className = "";
  try {
    const data = await api("/api/sql", {
      method: "POST",
      headers: {"content-type": "application/json"},
      body: JSON.stringify({sql, limit: 500})});
    if (!data.rows || data.rows.length === 0) {
      $("sql-result").innerHTML = `<div class="empty">no rows / OK</div>`;
    } else {
      $("sql-result").innerHTML = `<table>
        <thead><tr>${data.cols.map(c => `<th>${esc(c)}</th>`).join("")}</tr></thead>
        <tbody>${data.rows.map(r => `<tr>${
          r.map(v => `<td>${esc(v)}</td>`).join("")}</tr>`).join("")}</tbody></table>`;
    }
    $("sql-status").textContent = `${data.rows_returned} rows`;
    $("sql-status").className = "ok";
    state.lastSqlResult = data;
  } catch (e) {
    $("sql-status").textContent = e.message;
    $("sql-status").className = "err";
  }
}

function exportCsv() {
  const data = state.lastSqlResult;
  if (!data || !data.rows) return alert("Run a query first.");
  const csv = [data.cols.join(","),
    ...data.rows.map(r => r.map(v => {
      const s = (""+v).replace(/"/g,'""');
      return /[",\n]/.test(s) ? `"${s}"` : s;
    }).join(","))].join("\n");
  const blob = new Blob([csv], {type:"text/csv"});
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = "openads-result.csv"; a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

async function loadServerInfo() {
  try {
    const data = await api("/api/server/info");
    $("server-body").innerHTML = `<div class="kv">
      <div>Engine</div><div>${esc(data.engine)} ${esc(data.version || "")}</div>
      <div>HTTP module</div><div>${esc(data.http || "")}</div>
      <div>Server name</div><div>${esc(data.server_name || "")}</div>
      <div>Data dir</div><div>${esc(data.data_dir)}</div>
      <div>Tables</div><div>${data.tables.length}</div>
      </div>`;
  } catch (e) {
    $("server-body").innerHTML = `<div class="err">${esc(e.message)}</div>`;
  }
}

let sessionsTimer = null;
function startSessionsAutoRefresh() {
  stopSessionsAutoRefresh();
  if (!$("sessions-auto").checked) return;
  sessionsTimer = setInterval(loadSessions, 3000);
}
function stopSessionsAutoRefresh() {
  if (sessionsTimer) { clearInterval(sessionsTimer); sessionsTimer = null; }
}
function fmtDuration(s) {
  if (s < 60)    return s + "s";
  if (s < 3600)  return Math.floor(s/60) + "m " + (s%60) + "s";
  return Math.floor(s/3600) + "h " + Math.floor((s%3600)/60) + "m";
}
)OPENADS_SPA"
R"OPENADS_SPA(// ---- studio.web.0.5 — Data Dictionary tab ---------------------------
let ddState = { current: null };

async function loadDicts() {
  try {
    const data = await api("/api/dd");
    const sel = $("dd-pick");
    sel.innerHTML = '<option value="">— pick a dictionary —</option>'
      + data.dicts.map(d =>
          `<option value="${esc(d.name)}">${esc(d.name)}` +
          ` (${d.bytes} B)</option>`).join("");
    if (ddState.current && data.dicts.find(d => d.name === ddState.current)) {
      sel.value = ddState.current;
      loadDictView(ddState.current);
    } else {
      $("dd-body").innerHTML =
        `<div class="empty">${data.dicts.length} dictionary file(s) in `
        + `${esc(data.data_dir)}.</div>`;
    }
  } catch (e) {
    $("dd-body").innerHTML = `<div class="err">${esc(e.message)}</div>`;
  }
}
async function loadDictView(name) {
  ddState.current = name;
  $("dd-status").textContent = "loading…"; $("dd-status").className = "";
  try {
    const d = await api(`/api/dd/${encodeURIComponent(name)}`);
    const tblRows = (d.tables||[]).map(t =>
      `<tr><td>${esc(t.alias)}</td><td>${esc(t.path)}</td>
       <td><button class="btn btn-danger" data-rm-tbl="${esc(t.alias)}">Remove</button></td></tr>`).join("");
    const usrRows = (d.users||[]).map(u =>
      `<tr><td>${esc(u)}</td>
       <td><button class="btn btn-danger" data-rm-usr="${esc(u)}">Remove</button></td></tr>`).join("");
    const ixRows  = (d.indexes||[]).map(i =>
      `<tr><td>${esc(i.table)}</td><td>${esc(i.path)}</td><td>${esc(i.comment)}</td></tr>`).join("");
    const lkRows  = (d.links||[]).map(l =>
      `<tr><td>${esc(l.alias)}</td><td>${esc(l.path)}</td><td>${esc(l.user)}</td></tr>`).join("");
    const riRows  = (d.ri||[]).map(r =>
      `<tr><td>${esc(r.name)}</td><td>${esc(r.parent)}</td><td>${esc(r.child)}</td>
       <td>${esc(r.tag)}</td><td>${esc(r.update_opt)}</td><td>${esc(r.delete_opt)}</td>
       <td>${esc(r.fail_table)}</td></tr>`).join("");
    const propRows = (d.db_props||[]).map(p =>
      `<tr><td>${esc(p.key)}</td><td>${esc(p.value)}</td></tr>`).join("");

    $("dd-body").innerHTML = `
      <h3 style="font-size:17px;margin:0 0 8px">${esc(d.name)}</h3>
      <p style="opacity:0.7;font-size:14px;margin:0 0 16px">${esc(d.path)}</p>

      <h4 style="font-size:15px;margin:18px 0 6px">Tables</h4>
      <form id="dd-add-tbl" class="form-row" style="margin-bottom:8px">
        <input name="alias" placeholder="alias" required>
        <input name="path"  placeholder="relative.dbf" required>
        <button type="submit" class="btn">+ Add table</button>
      </form>
      ${tblRows ? `<table><thead><tr><th>alias</th><th>path</th><th></th></tr></thead><tbody>${tblRows}</tbody></table>`
                : `<div class="empty">No tables yet.</div>`}

      <h4 style="font-size:15px;margin:18px 0 6px">Users</h4>
      <form id="dd-add-usr" class="form-row" style="margin-bottom:8px">
        <input name="user" placeholder="username" required>
        <button type="submit" class="btn">+ Add user</button>
      </form>
      ${usrRows ? `<table><thead><tr><th>name</th><th></th></tr></thead><tbody>${usrRows}</tbody></table>`
                : `<div class="empty">No users yet.</div>`}

      <h4 style="font-size:15px;margin:18px 0 6px">Indexes</h4>
      ${ixRows  ? `<table><thead><tr><th>table</th><th>path</th><th>comment</th></tr></thead><tbody>${ixRows}</tbody></table>`
                : `<div class="empty">No indexes registered.</div>`}

      <h4 style="font-size:15px;margin:18px 0 6px">Links</h4>
      ${lkRows  ? `<table><thead><tr><th>alias</th><th>path</th><th>user</th></tr></thead><tbody>${lkRows}</tbody></table>`
                : `<div class="empty">No links.</div>`}

      <h4 style="font-size:15px;margin:18px 0 6px">RI rules</h4>
      ${riRows  ? `<table><thead><tr><th>name</th><th>parent</th><th>child</th><th>tag</th>
                   <th>upd</th><th>del</th><th>fail</th></tr></thead><tbody>${riRows}</tbody></table>`
                : `<div class="empty">No RI rules.</div>`}

      <h4 style="font-size:15px;margin:18px 0 6px">DB properties</h4>
      <form id="dd-add-prop" class="form-row" style="margin-bottom:8px">
        <input name="key"   placeholder="key" required>
        <input name="value" placeholder="value">
        <button type="submit" class="btn">Set</button>
      </form>
      ${propRows ? `<table><thead><tr><th>key</th><th>value</th></tr></thead><tbody>${propRows}</tbody></table>`
                 : `<div class="empty">No DB properties yet.</div>`}
    `;
    $("dd-status").textContent = ""; $("dd-status").className = "";

    $("dd-add-tbl").addEventListener("submit", async e => {
      e.preventDefault();
      const fd = new FormData(e.target);
      await ddPost(`/api/dd/${encodeURIComponent(name)}/tables`,
                   {alias: fd.get("alias"), path: fd.get("path")});
    });
    $("dd-add-usr").addEventListener("submit", async e => {
      e.preventDefault();
      const fd = new FormData(e.target);
      await ddPost(`/api/dd/${encodeURIComponent(name)}/users`,
                   {user: fd.get("user")});
    });
    $("dd-add-prop").addEventListener("submit", async e => {
      e.preventDefault();
      const fd = new FormData(e.target);
      await ddPost(`/api/dd/${encodeURIComponent(name)}/dbprop`,
                   {key: fd.get("key"), value: fd.get("value") || ""});
    });
    document.querySelectorAll("[data-rm-tbl]").forEach(b =>
      b.addEventListener("click", () => ddDelete(
        `/api/dd/${encodeURIComponent(name)}/tables/${
          encodeURIComponent(b.dataset.rmTbl)}`)));
    document.querySelectorAll("[data-rm-usr]").forEach(b =>
      b.addEventListener("click", () => ddDelete(
        `/api/dd/${encodeURIComponent(name)}/users/${
          encodeURIComponent(b.dataset.rmUsr)}`)));
  } catch (e) {
    $("dd-body").innerHTML = `<div class="err">${esc(e.message)}</div>`;
  }
}
async function ddPost(url, body) {
  try {
    await api(url, {method:"POST",
                    headers:{"content-type":"application/json"},
                    body: JSON.stringify(body)});
    if (ddState.current) loadDictView(ddState.current);
  } catch (e) {
    $("dd-status").textContent = e.message;
    $("dd-status").className = "err";
  }
}
async function ddDelete(url) {
  if (!confirm("Remove?")) return;
  try {
    await api(url, {method:"DELETE"});
    if (ddState.current) loadDictView(ddState.current);
  } catch (e) {
    $("dd-status").textContent = e.message;
    $("dd-status").className = "err";
  }
}

async function loadSessions() {
  try {
    const data = await api("/api/server/sessions");
    $("sessions-status").textContent =
      `${data.count} active`;
    $("sessions-status").className = "ok";
    if (!data.sessions || data.sessions.length === 0) {
      $("sessions-body").innerHTML =
        `<div class="empty">No active wire sessions.</div>`;
      return;
    }
    const rows = data.sessions
      .sort((a,b) => a.connected_secs - b.connected_secs)
      .map(s => `<tr>
        <td>${s.id}</td>
        <td>${esc(s.peer_ip)}:${s.peer_port}</td>
        <td>${esc(s.user || "<i style='opacity:0.5'>(none)</i>")}</td>
        <td>${esc(s.data_dir)}</td>
        <td>${fmtDuration(s.connected_secs)}</td>
        <td>${fmtDuration(s.idle_secs)}</td>
        <td>${s.frames_in}</td>
        <td>${s.frames_out}</td>
        <td>${s.open_tables}</td></tr>`).join("");
    $("sessions-body").innerHTML = `<table>
      <thead><tr>
        <th>ID</th><th>Peer</th><th>User</th><th>Data dir</th>
        <th>Connected</th><th>Idle</th>
        <th>frames in</th><th>frames out</th><th>open</th>
      </tr></thead><tbody>${rows}</tbody></table>`;
  } catch (e) {
    $("sessions-body").innerHTML = `<div class="err">${esc(e.message)}</div>`;
  }
}

document.querySelectorAll("nav.tabs button").forEach(b =>
  b.addEventListener("click", () => showTab(b.dataset.tab)));
$("server-link").addEventListener("click", () => showTab("server"));
$("sql-run").addEventListener("click", runSql);
$("sql-export").addEventListener("click", exportCsv);
$("sql").addEventListener("keydown", e => {
  if ((e.ctrlKey || e.metaKey) && e.key === "Enter") { runSql(); return; }
  // History navigation only when caret is at first/last line so we
  // don't break in-textarea cursor movement.
  if (e.key === "ArrowUp" && e.ctrlKey)   { e.preventDefault(); recallHistory(-1); }
  if (e.key === "ArrowDown" && e.ctrlKey) { e.preventDefault(); recallHistory( 1); }
});
$("btn-new-table").addEventListener("click", openCreateModal);
$("ct-add-col").addEventListener("click", addCreateCol);
$("ct-create").addEventListener("click", runCreateTable);
$("ct-cancel").addEventListener("click", () =>
  $("modal-create").classList.remove("show"));
$("enc-go").addEventListener("click", runEncrypt);
$("enc-cancel").addEventListener("click", () =>
  $("modal-encrypt").classList.remove("show"));
$("sessions-refresh").addEventListener("click", loadSessions);
$("sessions-auto").addEventListener("change", startSessionsAutoRefresh);

$("dd-pick").addEventListener("change", e => {
  if (e.target.value) loadDictView(e.target.value);
});
$("dd-new").addEventListener("click", async () => {
  const n = prompt("New dictionary file name (e.g. orders.add):");
  if (!n) return;
  try {
    await api("/api/dd", {method:"POST",
       headers:{"content-type":"application/json"},
       body: JSON.stringify({name: n})});
    ddState.current = n; loadDicts();
  } catch (e) { alert(e.message); }
});
$("dd-drop").addEventListener("click", async () => {
  const n = ddState.current;
  if (!n) return alert("Pick a dictionary first.");
  if (!confirm(`Drop ${n}?  This deletes the .add file on disk.`)) return;
  try {
    await api(`/api/dd/${encodeURIComponent(n)}`, {method:"DELETE"});
    ddState.current = null; loadDicts();
  } catch (e) { alert(e.message); }
});

// URL params let docs / scripts deep-link to a specific tab + table.
//   /?table=employees.dbf&tab=structure
async function applyUrlState() {
  await loadTables();
  const u = new URL(location.href);
  const t = u.searchParams.get("table");
  const tab = u.searchParams.get("tab");
  const q   = u.searchParams.get("q");
  const run = u.searchParams.get("autorun") === "1";
  if (t) {
    state.table = t;
    document.querySelectorAll("aside li").forEach(li =>
      li.classList.toggle("active", li.dataset.name === t));
  }
  if (tab) showTab(tab);
  if (q && tab === "sql") {
    $("sql").value = q;
    if (run) await runSql();
  }
}
applyUrlState();
</script>
</body>
</html>
)OPENADS_SPA";

} // namespace openads::studio

#endif // OPENADS_WITH_HTTP
