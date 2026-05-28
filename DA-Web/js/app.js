/* DA-Web — main application JS */
(function () {
  'use strict';

  // ── State ──────────────────────────────────────────────────────────────────
  const state = {
    openConnections: new Set(),
    tabs: [],          // { id, title, type, dd, table }
    activeTabId: null,
    nextTabId: 1,
    selectedDD: null,  // DD of the currently highlighted tree node
  };

  // Per-table-tab state: tabId → { tbl, dd, table, rowIdx, pendingRow, confirmBtn }
  const tblState = {};

  // Context holders for async modals
  let pendingDeleteTabId = null;
  let pendingSqlSaveTabId = null;

  // ── Helpers ────────────────────────────────────────────────────────────────
  async function apiFetch(url, options = {}) {
    const res = await fetch(url, options);
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || `HTTP ${res.status}`);
    return data;
  }

  function setStatus(msg, type = 'info') {
    const bar = document.getElementById('status-msg');
    if (bar) bar.textContent = msg;
  }

  function showAlert(container, msg, type = 'error') {
    const el = document.createElement('div');
    el.className = `alert alert-${type}`;
    el.textContent = msg;
    container.prepend(el);
    setTimeout(() => el.remove(), 5000);
  }

  // ── Menu bar ───────────────────────────────────────────────────────────────
  document.querySelectorAll('#menubar .menu-item').forEach(item => {
    item.addEventListener('click', e => {
      // If the click came from a drop-item, close the menu and let it bubble
      // to the document-level action dispatcher — do NOT stopPropagation.
      if (e.target.closest('.drop-item')) {
        document.querySelectorAll('#menubar .menu-item').forEach(i => i.classList.remove('active'));
        return;
      }
      const wasActive = item.classList.contains('active');
      document.querySelectorAll('#menubar .menu-item').forEach(i => i.classList.remove('active'));
      if (!wasActive) item.classList.add('active');
      e.stopPropagation();
    });
  });

  document.addEventListener('click', () => {
    document.querySelectorAll('#menubar .menu-item').forEach(i => i.classList.remove('active'));
  });

  // Menu action dispatcher — fires for drop-item clicks that bubbled to document
  document.addEventListener('click', e => {
    const item = e.target.closest('.drop-item[data-action]');
    if (!item) return;
    handleMenuAction(item.dataset.action, item);
  });

  function handleMenuAction(action, el) {
    switch (action) {
      case 'create-dd':    openCreateDDModal();            break;
      case 'open-dd':      openOpenDDModal();              break;
      case 'free-tables':  openFreeTablesModal();          break;
      case 'connect':      openConnectModal(el.dataset.dd); break;
      case 'disconnect':   disconnectDD(el.dataset.dd);   break;
      case 'open-sql':     openSqlTab();                  break;
      case 'refresh-tree': refreshTree();                  break;
      case 'about':        openAboutModal();               break;
    }
  }

  // ── Toggle-group wiring (called once after DOMContentLoaded) ──────────────
  function initToggleGroups() {
    document.querySelectorAll('.toggle-group').forEach(group => {
      group.querySelectorAll('.toggle-btn').forEach(btn => {
        btn.addEventListener('click', () => {
          group.querySelectorAll('.toggle-btn').forEach(b => b.classList.remove('active'));
          btn.classList.add('active');
        });
      });
    });
  }

  function toggleGroupValue(groupId) {
    return document.querySelector(`#${groupId} .toggle-btn.active`)?.dataset.value ?? 'local';
  }

  function resetToggleGroup(groupId) {
    const group = document.getElementById(groupId);
    if (!group) return;
    group.querySelectorAll('.toggle-btn').forEach((b, i) => b.classList.toggle('active', i === 0));
  }

  // ── Split.js ───────────────────────────────────────────────────────────────
  if (typeof Split !== 'undefined') {
    Split(['#tree-pane', '#content-pane'], {
      sizes: [22, 78],
      minSize: [120, 300],
      gutterSize: 4,
      cursor: 'col-resize',
    });
  }

  // ── jsTree ─────────────────────────────────────────────────────────────────
  function initTree() {
    $('#tree-container').jstree({
      core: {
        themes: { name: 'default', dots: true, icons: true },
        data: function (node, cb) {
          const isRoot = node.id === '#';
          if (isRoot) {
            fetch('api/tree.php?action=roots')
              .then(r => r.json()).then(cb).catch(() => cb([]));
            return;
          }
          const a = node.a_attr || {};
          const dd   = a['data-dd']    || '';
          const type = a['data-type']  || '';
          const cat  = a['data-cat']   || '';
          const tbl  = a['data-table'] || '';

          if (type === 'dd') {
            fetch(`api/tree.php?action=dd_children&dd=${encodeURIComponent(dd)}`)
              .then(r => r.json()).then(cb).catch(() => cb([]));
          } else if (type === 'category') {
            fetch(`api/tree.php?action=category_children&dd=${encodeURIComponent(dd)}&cat=${encodeURIComponent(cat)}`)
              .then(r => r.json()).then(cb).catch(() => cb([]));
          } else if (type === 'table') {
            fetch(`api/tree.php?action=table_children&dd=${encodeURIComponent(dd)}&table=${encodeURIComponent(tbl)}`)
              .then(r => r.json()).then(cb).catch(() => cb([]));
          } else {
            cb([]);
          }
        },
      },
      plugins: ['wholerow', 'types'],
    });

    $('#tree-container').on('select_node.jstree', function (e, data) {
      const a    = data.node.a_attr || {};
      const type = a['data-type']   || '';
      const dd   = a['data-dd']     || '';
      const tbl  = a['data-table']  || '';

      // Track which DD is currently highlighted so SQL editor can pre-select it
      if (dd) state.selectedDD = dd;

      if (type === 'dd') {
        const connected = a['data-connected'] === 'true';
        if (!connected) openConnectModal(dd);
      } else if (type === 'table') {
        openTableTab(dd, tbl);
      } else if (type === 'fields') {
        openMetaTab(dd, tbl, 'fields');
      } else if (type === 'indexes') {
        openMetaTab(dd, tbl, 'indexes');
      }
    });

    $('#tree-container').on('dblclick.jstree', function (e) {
      const node = $(e.target).closest('li');
      if (!node.length) return;
      const treeNode = $.jstree.reference(node);
      if (!treeNode) return;
      const sel = treeNode.get_node(node);
      if (!sel) return;
      const a    = sel.a_attr || {};
      const type = a['data-type'] || '';
      const dd   = a['data-dd']   || '';
      const tbl  = a['data-table'] || '';
      if (type === 'table') openTableTab(dd, tbl);
    });
  }

  function refreshTree() {
    $('#tree-container').jstree(true).refresh();
  }

  // ── Tabs ───────────────────────────────────────────────────────────────────
  function openTableTab(dd, table) {
    const existingId = state.tabs.findIndex(t => t.type === 'table' && t.dd === dd && t.table === table);
    if (existingId !== -1) {
      activateTab(state.tabs[existingId].id);
      return;
    }
    const id = 'tab-' + (state.nextTabId++);
    state.tabs.push({ id, title: `${dd}.${table}`, type: 'table', dd, table });
    renderTabs();
    activateTab(id);
    loadTableData(id, dd, table);
  }

  function openMetaTab(dd, table, kind) {
    const metaKey = `${dd}.${table}.${kind}`;
    const existing = state.tabs.find(t => t.type === 'meta' && t.metaKey === metaKey);
    if (existing) { activateTab(existing.id); return; }
    const id = 'tab-' + (state.nextTabId++);
    const title = `${table} ${kind === 'fields' ? 'Fields' : 'Indexes'}`;
    state.tabs.push({ id, title, type: 'meta', dd, table, kind, metaKey });
    renderTabs();
    activateTab(id);
    loadMetaData(id, dd, table, kind);
  }

  function loadMetaData(tabId, dd, table, kind) {
    const container = document.getElementById('meta-' + tabId);
    if (!container) return;
    fetch(`api/table_meta.php?dd=${encodeURIComponent(dd)}&table=${encodeURIComponent(table)}&kind=${encodeURIComponent(kind)}`)
      .then(r => r.json())
      .then(resp => {
        if (resp.error) {
          container.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(resp.error)}</div>`;
          return;
        }
        container.innerHTML = `<div id="meta-tbl-${tabId}" style="flex:1;min-height:0;overflow:hidden;"></div>`;
        /* global Tabulator */
        new Tabulator('#meta-tbl-' + tabId, {
          data: resp.data,
          autoColumns: true,
          layout: 'fitDataFill',
          pagination: 'local',
          paginationSize: 200,
          paginationCounter: 'rows',
          height: '100%',
          placeholder: '(no data)',
        });
      })
      .catch(err => {
        if (container) container.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(err.message)}</div>`;
      });
  }

  function openSqlTab(dd = null, sql = '', scriptName = '') {
    const targetDD = dd || state.selectedDD;
    const id = 'tab-' + (state.nextTabId++);
    const title = scriptName || (targetDD ? `SQL – ${targetDD}` : 'SQL');
    state.tabs.push({ id, title, type: 'sql', dd: targetDD, initialSql: sql });
    renderTabs();
    activateTab(id);
    setTimeout(() => {
      const ta = document.getElementById('sql-text-' + id);
      if (ta) { if (sql) ta.value = sql; ta.focus(); }
    }, 80);
  }

  function renderTabs() {
    const bar = document.getElementById('tab-bar');
    const content = document.getElementById('tab-content');

    // Remove existing tab buttons
    bar.querySelectorAll('.tab').forEach(el => el.remove());

    // Remove tab panels that no longer exist
    const activeIds = new Set(state.tabs.map(t => t.id));
    content.querySelectorAll('.tab-panel').forEach(p => {
      if (!activeIds.has(p.dataset.tabId)) p.remove();
    });

    state.tabs.forEach(tab => {
      // Tab button
      const btn = document.createElement('div');
      btn.className = 'tab' + (tab.id === state.activeTabId ? ' active' : '');
      btn.dataset.tabId = tab.id;
      btn.innerHTML = `<span>${escHtml(tab.title)}</span><span class="tab-close" title="Close">×</span>`;
      btn.querySelector('span:first-child').addEventListener('click', () => activateTab(tab.id));
      btn.querySelector('.tab-close').addEventListener('click', e => { e.stopPropagation(); closeTab(tab.id); });
      bar.insertBefore(btn, bar.querySelector('.new-tab-btn'));

      // Create panel if needed
      if (!content.querySelector(`[data-tab-id="${tab.id}"]`)) {
        const panel = document.createElement('div');
        panel.className = 'tab-panel' + (tab.id === state.activeTabId ? ' active' : '');
        panel.dataset.tabId = tab.id;
        if (tab.type === 'table') {
          panel.innerHTML = `<div class="data-panel" id="data-${tab.id}" style="display:flex;flex-direction:column;flex:1;overflow:hidden;"><div class="alert alert-info" style="margin:8px;">Loading…</div></div>`;
        } else if (tab.type === 'meta') {
          panel.innerHTML = `<div class="data-panel" id="meta-${tab.id}" style="display:flex;flex-direction:column;flex:1;overflow:hidden;"><div class="alert alert-info" style="margin:8px;">Loading…</div></div>`;
        } else if (tab.type === 'sql') {
          panel.innerHTML = buildSqlPanel(tab.id, tab.dd);
          bindSqlPanel(tab.id, tab);
        }
        content.appendChild(panel);
      }
    });
  }

  function activateTab(id) {
    state.activeTabId = id;
    document.querySelectorAll('.tab').forEach(el => {
      el.classList.toggle('active', el.dataset.tabId === id);
    });
    document.querySelectorAll('.tab-panel').forEach(el => {
      el.classList.toggle('active', el.dataset.tabId === id);
    });
  }

  function closeTab(id) {
    state.tabs = state.tabs.filter(t => t.id !== id);
    delete tblState[id];
    if (state.activeTabId === id) {
      state.activeTabId = state.tabs.length ? state.tabs[state.tabs.length - 1].id : null;
    }
    renderTabs();
    if (state.activeTabId) activateTab(state.activeTabId);
  }

  document.getElementById('new-tab-btn').addEventListener('click', () => openSqlTab());

  // ── Table data loading ─────────────────────────────────────────────────────
  function loadTableData(tabId, dd, table) {
    const container = document.getElementById('data-' + tabId);
    if (!container) return;

    fetch(`api/table_data.php?dd=${encodeURIComponent(dd)}&table=${encodeURIComponent(table)}`)
      .then(r => r.json())
      .then(resp => {
        if (resp.error) {
          container.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(resp.error)}</div>`;
          return;
        }
        container.innerHTML = `<div id="tabulator-${tabId}" style="flex:1;min-height:0;overflow:hidden;"></div>`;
        /* global Tabulator */
        const tbl = new Tabulator('#tabulator-' + tabId, {
          data: resp.data,
          autoColumns: true,
          autoColumnsDefinitions: function (defs) {
            return defs.map(def => ({
              ...def,
              editor: 'input',
              editable: function (cell) {
                const inst = tblState[tabId];
                return !!(inst && inst.pendingRow && cell.getRow() === inst.pendingRow);
              },
            }));
          },
          layout: 'fitDataFill',
          pagination: 'local',
          paginationSize: 50,
          paginationSizeSelector: [25, 50, 100, 200, 500],
          paginationCounter: 'rows',
          selectable: 1,
          movableColumns: true,
          resizableRows: false,
          placeholder: '(no rows)',
          height: '100%',
          rowClick: function (e, row) {
            const inst = tblState[tabId];
            if (!inst) return;
            inst.tbl.deselectRow();
            row.select();
            inst.rowIdx = inst.tbl.getRows().indexOf(row);
          },
        });

        tbl.on('tableBuilt', function () {
          const bar = document.createElement('div');
          bar.className = 'tbl-btn-bar';
          bar.innerHTML = `
            <button class="tbl-btn" data-act="refresh" title="Refresh">&#x27F3;</button>
            <button class="tbl-btn" data-act="top"     title="First record">&#x2912;</button>
            <button class="tbl-btn" data-act="bottom"  title="Last record">&#x2913;</button>
            <button class="tbl-btn" data-act="up"      title="Previous record">&#9650;</button>
            <button class="tbl-btn" data-act="down"    title="Next record">&#9660;</button>
            <span class="tbl-btn-sep"></span>
            <button class="tbl-btn" data-act="add"    title="Add row">&#xff0b;</button>
            <button class="tbl-btn" data-act="delete" title="Delete row">&#x2715;</button>
            <button class="tbl-btn tbl-btn-confirm" data-act="confirm" title="Confirm" disabled>&#x2714;</button>
            <span class="tbl-btn-sep"></span>`;
          bar.addEventListener('click', e => {
            const btn = e.target.closest('[data-act]');
            if (btn) handleTblAction(btn.dataset.act, tabId);
          });
          const paginator = document.querySelector('#tabulator-' + tabId + ' .tabulator-paginator');
          if (paginator) paginator.prepend(bar);
          tblState[tabId] = {
            tbl, dd, table, rowIdx: -1, pendingRow: null,
            confirmBtn: bar.querySelector('[data-act="confirm"]'),
          };
        });
      })
      .catch(err => {
        if (container) container.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(err.message)}</div>`;
      });
  }

  // Navigate to a row by its 0-based index in the full dataset.
  // Tabulator's scrollToRow handles page navigation automatically.
  function navigateToRow(inst, idx) {
    const rows = inst.tbl.getRows();
    if (!rows.length) return;
    idx = Math.max(0, Math.min(idx, rows.length - 1));
    inst.rowIdx = idx;
    const row = rows[idx];
    inst.tbl.deselectRow();
    inst.tbl.selectRow(row);
    inst.tbl.scrollToRow(row, 'top', false);
  }

  function handleTblAction(act, tabId) {
    const inst = tblState[tabId];
    if (!inst) return;
    const { tbl, dd, table } = inst;

    switch (act) {
      case 'refresh':
        fetch(`api/table_data.php?dd=${encodeURIComponent(dd)}&table=${encodeURIComponent(table)}`)
          .then(r => r.json())
          .then(resp => {
            if (!resp.error) { tbl.replaceData(resp.data); inst.rowIdx = -1; }
          })
          .catch(() => {});
        break;
      case 'top':
        navigateToRow(inst, 0);
        break;
      case 'bottom':
        navigateToRow(inst, tbl.getRows().length - 1);
        break;
      case 'up':
        navigateToRow(inst, inst.rowIdx <= 0 ? 0 : inst.rowIdx - 1);
        break;
      case 'down': {
        const total = tbl.getRows().length;
        navigateToRow(inst, inst.rowIdx < 0 ? 0 : Math.min(inst.rowIdx + 1, total - 1));
        break;
      }

      case 'add': {
        if (inst.pendingRow) return; // already an unsaved row
        tbl.addRow({}, false).then(row => {
          inst.pendingRow = row;
          if (inst.confirmBtn) inst.confirmBtn.disabled = false;
          tbl.scrollToRow(row, 'bottom', false);
          setTimeout(() => {
            const cells = row.getCells();
            if (cells.length) cells[0].edit();
          }, 60);
        });
        break;
      }

      case 'delete': {
        if (inst.rowIdx < 0) { setStatus('Select a row to delete'); return; }
        pendingDeleteTabId = tabId;
        openModal('modal-del-confirm');
        break;
      }

      case 'confirm': {
        if (!inst.pendingRow) return;
        const rowData = inst.pendingRow.getData();
        apiFetch('api/row_ops.php', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ action: 'insert', dd, table, row: rowData }),
        }).then(() => {
          tbl.deleteRow(inst.pendingRow);
          inst.pendingRow = null;
          if (inst.confirmBtn) inst.confirmBtn.disabled = true;
          // Refresh from server to get auto-assigned field values
          return fetch(`api/table_data.php?dd=${encodeURIComponent(dd)}&table=${encodeURIComponent(table)}`)
            .then(r => r.json())
            .then(r2 => { if (!r2.error) tbl.replaceData(r2.data); });
        }).then(() => setStatus('Row inserted'))
          .catch(err => setStatus(`Insert failed: ${err.message}`));
        break;
      }
    }
  }

  // ── SQL panel ──────────────────────────────────────────────────────────────
  function buildSqlPanel(tabId, dd) {
    const ddOptions = Array.from(state.openConnections).map(n =>
      `<option value="${escAttr(n)}" ${n === dd ? 'selected' : ''}>${escHtml(n)}</option>`
    ).join('');
    return `
      <div class="sql-panel">
        <div id="sql-editor-wrap">
          <textarea id="sql-text-${tabId}"
            placeholder="-- Type SQL here&#10;-- SELECT * FROM MyTable&#10;-- Select text then press Ctrl+Enter to run just that portion"
            spellcheck="false"></textarea>
        </div>
        <div id="sql-toolbar">
          <select id="sql-dd-${tabId}" style="background:#1e1e2e;color:#cdd6f4;border:1px solid #45475a;padding:3px 8px;border-radius:4px;font-size:12px;">
            <option value="">— database —</option>
            ${ddOptions}
          </select>
          <button class="btn" id="sql-open-btn-${tabId}" title="Open saved script">&#128194; Open</button>
          <button class="btn" id="sql-save-btn-${tabId}" title="Save script">&#128190; Save</button>
          <button class="btn btn-primary" id="sql-run-${tabId}" title="Execute (F5 / F9)">&#9654; Execute</button>
          <button class="btn" id="sql-clear-${tabId}" title="Clear editor">Clear</button>
          <span style="font-size:11px;color:#45475a;margin-left:10px;white-space:nowrap;">
            F5 / F9 &mdash; run all &nbsp;&nbsp; Ctrl+Enter &mdash; run selection
          </span>
          <span id="sql-msg-${tabId}" style="font-size:11px;color:#a6adc8;margin-left:8px;"></span>
        </div>
        <div id="sql-results-${tabId}" style="flex:1;overflow:auto;padding:8px;"></div>
      </div>`;
  }

  // Shared execute logic — accepts the exact SQL string to send
  async function doExecuteSql(tabId, sql) {
    const dd      = (document.getElementById('sql-dd-' + tabId)?.value ?? '').trim();
    const msgEl   = document.getElementById('sql-msg-' + tabId);
    const runBtn  = document.getElementById('sql-run-' + tabId);
    const results = document.getElementById('sql-results-' + tabId);
    if (!msgEl || !results) return;

    if (!sql)  { msgEl.textContent = 'Nothing to execute'; return; }
    if (!dd)   { msgEl.textContent = 'Select a database first'; return; }

    if (runBtn) runBtn.disabled = true;
    msgEl.textContent = 'Running…';
    results.innerHTML = '';

    try {
      const resp = await apiFetch('api/execute_sql.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ dd, sql }),
      });

      if (resp.columns !== undefined) {
        msgEl.textContent = `${resp.data.length} row(s)`;
        results.innerHTML = `<div id="sql-tab-${tabId}"></div>`;
        new Tabulator('#sql-tab-' + tabId, {  /* global Tabulator */
          data: resp.data,
          columns: resp.columns,
          layout: 'fitDataFill',
          pagination: 'local',
          paginationSize: 50,
          paginationSizeSelector: [25, 50, 100, 200],
          movableColumns: true,
          placeholder: '(no rows)',
        });
      } else {
        msgEl.textContent = '';
        results.innerHTML = `<div class="alert alert-success">${escHtml(resp.message)}</div>`;
      }
    } catch (err) {
      msgEl.textContent = '';
      results.innerHTML = `<div class="alert alert-error">${escHtml(err.message)}</div>`;
    } finally {
      if (runBtn) runBtn.disabled = false;
    }
  }

  // Returns selected text in the textarea, or all text if nothing is selected
  function getActiveOrAllSql(tabId) {
    const ta = document.getElementById('sql-text-' + tabId);
    if (!ta) return '';
    const selected = ta.value.substring(ta.selectionStart, ta.selectionEnd).trim();
    return selected || ta.value.trim();
  }

  function bindSqlPanel(tabId, tab) {
    setTimeout(() => {
      const runBtn   = document.getElementById('sql-run-' + tabId);
      const clearBtn = document.getElementById('sql-clear-' + tabId);
      const openBtn  = document.getElementById('sql-open-btn-' + tabId);
      const saveBtn  = document.getElementById('sql-save-btn-' + tabId);
      const textarea = document.getElementById('sql-text-' + tabId);
      const results  = document.getElementById('sql-results-' + tabId);
      const msgEl    = document.getElementById('sql-msg-' + tabId);
      if (!runBtn || !textarea) return;

      openBtn?.addEventListener('click', () => openSqlOpenModal());

      saveBtn?.addEventListener('click', () => {
        const errEl = document.getElementById('sql-save-err');
        if (errEl) errEl.textContent = '';
        // Pre-fill name from tab title if it looks like a script name
        const tabMeta = state.tabs.find(t => t.id === tabId);
        const pre = (tabMeta && !tabMeta.title.startsWith('SQL')) ? tabMeta.title : '';
        const nameInput = document.getElementById('sql-save-name');
        if (nameInput) nameInput.value = pre;
        pendingSqlSaveTabId = tabId;
        openModal('modal-sql-save');
        setTimeout(() => nameInput?.focus(), 50);
      });

      // ▶ Execute button — run all text
      runBtn.addEventListener('click', () => {
        doExecuteSql(tabId, textarea.value.trim());
      });

      // Clear button
      clearBtn?.addEventListener('click', () => {
        textarea.value = '';
        if (results) results.innerHTML = '';
        if (msgEl)   msgEl.textContent = '';
        textarea.focus();
      });

      // Keyboard shortcuts (HeidiSQL-style)
      textarea.addEventListener('keydown', e => {
        // F5 or F9 — execute all
        if (e.key === 'F5' || e.key === 'F9') {
          e.preventDefault();
          doExecuteSql(tabId, textarea.value.trim());
          return;
        }
        // Ctrl+Enter — execute selection, or all if nothing selected
        if (e.key === 'Enter' && e.ctrlKey) {
          e.preventDefault();
          doExecuteSql(tabId, getActiveOrAllSql(tabId));
          return;
        }
      });
    }, 50);
  }

  // ── Helpers: open/close modals ────────────────────────────────────────────
  function openModal(id)  { document.getElementById(id)?.classList.add('open'); }
  function closeModal(id) { document.getElementById(id)?.classList.remove('open'); }
  function clearModalErr(id) { const el = document.getElementById(id); if (el) el.textContent = ''; }

  // ── Modal: New DD (create) ─────────────────────────────────────────────────
  function openCreateDDModal() {
    clearModalErr('cdd-err');
    resetToggleGroup('cdd-conn-type');
    openModal('modal-create-dd');
    setTimeout(() => document.getElementById('cdd-name')?.focus(), 50);
  }

  document.getElementById('cdd-cancel').addEventListener('click', () => closeModal('modal-create-dd'));

  document.getElementById('cdd-save').addEventListener('click', async () => {
    const name     = document.getElementById('cdd-name').value.trim();
    const path     = document.getElementById('cdd-path').value.trim();
    const password = document.getElementById('cdd-password').value;
    const connType = toggleGroupValue('cdd-conn-type');
    const errEl    = document.getElementById('cdd-err');
    errEl.textContent = '';

    if (!name || !path) { errEl.textContent = 'Name and path are required'; return; }

    try {
      await apiFetch('api/create_dd.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name, path, password, connType }),
      });
      closeModal('modal-create-dd');
      document.getElementById('cdd-name').value = '';
      document.getElementById('cdd-path').value = '';
      document.getElementById('cdd-password').value = '';
      refreshTree();
      setStatus(`Created dictionary '${name}'`);
    } catch (err) {
      errEl.textContent = err.message;
    }
  });

  // ── Modal: Open DD (add existing) ─────────────────────────────────────────
  function openOpenDDModal() {
    clearModalErr('odd-err');
    resetToggleGroup('odd-conn-type');
    openModal('modal-open-dd');
    setTimeout(() => document.getElementById('odd-name')?.focus(), 50);
  }

  document.getElementById('odd-cancel').addEventListener('click', () => closeModal('modal-open-dd'));

  document.getElementById('odd-save').addEventListener('click', async () => {
    const name     = document.getElementById('odd-name').value.trim();
    const path     = document.getElementById('odd-path').value.trim();
    const username = document.getElementById('odd-user').value.trim();
    const connType = toggleGroupValue('odd-conn-type');
    const errEl    = document.getElementById('odd-err');
    errEl.textContent = '';

    if (!name || !path) { errEl.textContent = 'Name and path are required'; return; }

    try {
      await apiFetch('api/dictionaries.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'add', name, path, username, connType, entryType: 'dd' }),
      });
      closeModal('modal-open-dd');
      document.getElementById('odd-name').value = '';
      document.getElementById('odd-path').value = '';
      document.getElementById('odd-user').value = '';
      refreshTree();
      setStatus(`Added dictionary '${name}'`);
    } catch (err) {
      errEl.textContent = err.message;
    }
  });

  // ── Modal: Free Tables directory ───────────────────────────────────────────
  function openFreeTablesModal() {
    clearModalErr('ft-err');
    resetToggleGroup('ft-conn-type');
    openModal('modal-free-tables');
    setTimeout(() => document.getElementById('ft-name')?.focus(), 50);
  }

  document.getElementById('ft-cancel').addEventListener('click', () => closeModal('modal-free-tables'));

  document.getElementById('ft-save').addEventListener('click', async () => {
    const name     = document.getElementById('ft-name').value.trim();
    const path     = document.getElementById('ft-path').value.trim();
    const connType = toggleGroupValue('ft-conn-type');
    const errEl    = document.getElementById('ft-err');
    errEl.textContent = '';

    if (!name || !path) { errEl.textContent = 'Name and path are required'; return; }

    try {
      await apiFetch('api/dictionaries.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'add', name, path, username: '', connType, entryType: 'free' }),
      });
      closeModal('modal-free-tables');
      document.getElementById('ft-name').value = '';
      document.getElementById('ft-path').value = '';
      refreshTree();
      setStatus(`Added free tables directory '${name}'`);
    } catch (err) {
      errEl.textContent = err.message;
    }
  });

  // ── Connect modal ──────────────────────────────────────────────────────────
  function openConnectModal(ddName) {
    const overlay = document.getElementById('modal-connect');
    document.getElementById('connect-dd-name').textContent = ddName || '(unknown)';
    overlay.dataset.dd = ddName;
    document.getElementById('connect-password').value = '';
    document.getElementById('connect-err').textContent = '';

    // Pre-fill username from stored dict config if available
    fetch('api/dictionaries.php')
      .then(r => r.json())
      .then(dicts => {
        const d = dicts.find(x => x.name === ddName);
        if (d) document.getElementById('connect-username').value = d.username || '';
      });

    overlay.classList.add('open');
    setTimeout(() => document.getElementById('connect-password').focus(), 50);
  }

  document.getElementById('connect-cancel').addEventListener('click', () => {
    document.getElementById('modal-connect').classList.remove('open');
  });

  document.getElementById('connect-submit').addEventListener('click', async () => {
    const overlay  = document.getElementById('modal-connect');
    const ddName   = overlay.dataset.dd;
    const username = document.getElementById('connect-username').value.trim();
    const password = document.getElementById('connect-password').value;
    const errEl    = document.getElementById('connect-err');
    errEl.textContent = '';

    // Fetch path from stored config
    try {
      const dicts = await apiFetch('api/dictionaries.php');
      const d = dicts.find(x => x.name === ddName);
      if (!d) { errEl.textContent = 'Dictionary not found in config'; return; }

      await apiFetch('api/connect.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'connect', name: ddName, path: d.path, username, password }),
      });

      state.openConnections.add(ddName);
      overlay.classList.remove('open');
      refreshTree();
      setStatus(`Connected to '${ddName}'`);
      updateConnectionCount();
    } catch (err) {
      errEl.textContent = err.message;
    }
  });

  // Enter key submits connect form
  document.getElementById('connect-password').addEventListener('keydown', e => {
    if (e.key === 'Enter') document.getElementById('connect-submit').click();
  });

  // ── Disconnect ─────────────────────────────────────────────────────────────
  async function disconnectDD(ddName) {
    if (!ddName) return;
    try {
      await apiFetch('api/connect.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'disconnect', name: ddName }),
      });
      state.openConnections.delete(ddName);
      refreshTree();
      setStatus(`Disconnected from '${ddName}'`);
      updateConnectionCount();
    } catch (err) {
      setStatus(`Disconnect failed: ${err.message}`);
    }
  }

  // ── Delete row confirmation modal ──────────────────────────────────────────
  document.getElementById('del-confirm-cancel').addEventListener('click', () => {
    closeModal('modal-del-confirm');
    pendingDeleteTabId = null;
  });

  document.getElementById('del-confirm-ok').addEventListener('click', async () => {
    const tabId = pendingDeleteTabId;
    closeModal('modal-del-confirm');
    pendingDeleteTabId = null;
    const inst = tblState[tabId];
    if (!inst || inst.rowIdx < 0) return;
    const rows = inst.tbl.getRows();
    const row  = rows[inst.rowIdx];
    if (!row) return;
    try {
      await apiFetch('api/row_ops.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'delete', dd: inst.dd, table: inst.table, orig: row.getData() }),
      });
      inst.tbl.deleteRow(row);
      inst.rowIdx = -1;
      setStatus('Row deleted');
    } catch (err) {
      setStatus(`Delete failed: ${err.message}`);
    }
  });

  // ── SQL script Save / Open ─────────────────────────────────────────────────
  document.getElementById('sql-save-cancel').addEventListener('click', () => {
    closeModal('modal-sql-save');
    pendingSqlSaveTabId = null;
  });

  document.getElementById('sql-save-ok').addEventListener('click', async () => {
    const name  = document.getElementById('sql-save-name').value.trim();
    const errEl = document.getElementById('sql-save-err');
    errEl.textContent = '';
    if (!name) { errEl.textContent = 'Script name is required'; return; }
    const tabId = pendingSqlSaveTabId;
    const sql   = document.getElementById('sql-text-' + tabId)?.value ?? '';
    try {
      await apiFetch('api/sql_scripts.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'save', name, sql }),
      });
      closeModal('modal-sql-save');
      pendingSqlSaveTabId = null;
      setStatus(`Script '${name}' saved`);
    } catch (err) {
      errEl.textContent = err.message;
    }
  });

  document.getElementById('sql-save-name').addEventListener('keydown', e => {
    if (e.key === 'Enter') document.getElementById('sql-save-ok').click();
  });

  async function openSqlOpenModal() {
    const list = document.getElementById('sql-scripts-list');
    list.innerHTML = '<div style="padding:12px;color:#7f849c;">Loading…</div>';
    openModal('modal-sql-open');
    try {
      const scripts = await apiFetch('api/sql_scripts.php');
      const names = Object.keys(scripts);
      if (!names.length) {
        list.innerHTML = '<div style="padding:12px;color:#7f849c;">No saved scripts yet.</div>';
        return;
      }
      list.innerHTML = names.map(n => `
        <div class="script-row">
          <span class="script-name">${escHtml(n)}</span>
          <div style="display:flex;gap:4px;flex-shrink:0;">
            <button class="btn btn-sm script-load" data-name="${escAttr(n)}">Load</button>
            <button class="btn btn-sm btn-danger script-del"  data-name="${escAttr(n)}" title="Delete script">&#x2715;</button>
          </div>
        </div>`).join('');

      list.querySelectorAll('.script-load').forEach(btn => {
        btn.addEventListener('click', () => {
          openSqlTab(state.selectedDD, scripts[btn.dataset.name], btn.dataset.name);
          closeModal('modal-sql-open');
        });
      });
      list.querySelectorAll('.script-del').forEach(btn => {
        btn.addEventListener('click', async () => {
          await apiFetch('api/sql_scripts.php', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ action: 'delete', name: btn.dataset.name }),
          });
          btn.closest('.script-row').remove();
          if (!list.querySelector('.script-row'))
            list.innerHTML = '<div style="padding:12px;color:#7f849c;">No saved scripts yet.</div>';
        });
      });

      document.getElementById('sql-open-all').onclick = () => {
        names.forEach(n => openSqlTab(state.selectedDD, scripts[n], n));
        closeModal('modal-sql-open');
      };
    } catch (err) {
      list.innerHTML = `<div style="padding:12px;color:#f38ba8;">${escHtml(err.message)}</div>`;
    }
  }

  document.getElementById('modal-sql-open').querySelector('.modal-close')
    .addEventListener('click', () => closeModal('modal-sql-open'));

  // ── About modal ────────────────────────────────────────────────────────────
  function openAboutModal() {
    document.getElementById('modal-about').classList.add('open');
  }

  document.getElementById('about-close').addEventListener('click', () => {
    document.getElementById('modal-about').classList.remove('open');
  });

  // ── Connection submenu builder ─────────────────────────────────────────────
  async function buildConnectionMenu() {
    const dicts = await apiFetch('api/dictionaries.php').catch(() => []);
    const sessionState = await apiFetch('api/session_state.php').catch(() => ({ open: [] }));
    const openSet = new Set(sessionState.open);
    openSet.forEach(n => state.openConnections.add(n));

    const connMenu = document.getElementById('connection-submenu');
    if (!connMenu) return;

    connMenu.innerHTML = `
      <div class="drop-item" data-action="create-dd">New DD…</div>
      <div class="drop-item" data-action="open-dd">Open DD…</div>
      <div class="drop-item" data-action="free-tables">Free Tables…</div>
      <div class="drop-separator"></div>`;

    dicts.forEach(d => {
      const connected = openSet.has(d.name);
      const item = document.createElement('div');
      item.className = 'drop-item';
      item.textContent = (connected ? '● ' : '○ ') + d.name;
      item.dataset.action = connected ? 'disconnect' : 'connect';
      item.dataset.dd = d.name;
      connMenu.appendChild(item);
    });

    if (dicts.length === 0) {
      connMenu.innerHTML += `<div class="drop-item disabled">No dictionaries configured</div>`;
    }

    connMenu.innerHTML += `<div class="drop-separator"></div>
      <div class="drop-item" data-action="refresh-tree">Refresh Tree</div>`;

    updateConnectionCount();
  }

  // ── Status helpers ─────────────────────────────────────────────────────────
  function updateConnectionCount() {
    const el = document.getElementById('status-connections');
    if (el) el.textContent = `${state.openConnections.size} connection(s) open`;
  }

  // ── Close modals on overlay click ──────────────────────────────────────────
  document.querySelectorAll('.modal-overlay').forEach(overlay => {
    overlay.addEventListener('click', e => {
      if (e.target === overlay) overlay.classList.remove('open');
    });
  });

  // ── Context menu ───────────────────────────────────────────────────────────
  const ctxMenu = document.getElementById('ctx-menu');

  document.addEventListener('contextmenu', e => {
    const node = e.target.closest('.jstree-anchor[data-type]');
    if (!node) { ctxMenu.style.display = 'none'; return; }
    e.preventDefault();
    const type  = node.dataset.type  || '';
    const dd    = node.dataset.dd    || '';
    const name  = node.dataset.table || node.dataset.name || dd;
    buildCtxMenu(type, dd, name);
    ctxMenu.style.left = e.pageX + 'px';
    ctxMenu.style.top  = e.pageY + 'px';
    ctxMenu.style.display = 'block';
  });

  document.addEventListener('click', () => { ctxMenu.style.display = 'none'; });

  function buildCtxMenu(type, dd, name) {
    ctxMenu.innerHTML = '';
    const items = [];

    if (type === 'dd') {
      const connected = state.openConnections.has(dd);
      if (connected) {
        items.push({ label: 'Disconnect', action: () => disconnectDD(dd) });
        items.push({ label: 'Open SQL Editor', action: () => openSqlTab(dd) });
      } else {
        items.push({ label: 'Connect…', action: () => openConnectModal(dd) });
      }
      items.push({ label: 'Remove from list', action: () => removeDDFromList(dd) });
    } else if (type === 'table') {
      items.push({ label: 'Open Table', action: () => openTableTab(dd, name) });
    }

    items.forEach(item => {
      const el = document.createElement('div');
      el.className = 'ctx-item';
      el.textContent = item.label;
      el.addEventListener('click', item.action);
      ctxMenu.appendChild(el);
    });
  }

  async function removeDDFromList(ddName) {
    try {
      await apiFetch('api/dictionaries.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'remove', name: ddName }),
      });
      state.openConnections.delete(ddName);
      refreshTree();
      setStatus(`Removed '${ddName}' from list`);
      updateConnectionCount();
    } catch (err) {
      setStatus(`Remove failed: ${err.message}`);
    }
  }

  // ── Utility ────────────────────────────────────────────────────────────────
  function escHtml(s) {
    return String(s)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;')
      .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  function escAttr(s) { return escHtml(s); }

  // ── Boot ───────────────────────────────────────────────────────────────────
  document.addEventListener('DOMContentLoaded', async () => {
    initToggleGroups();
    initTree();
    await buildConnectionMenu();
    setStatus('Ready');

    // Rebuild connection submenu each time Connection menu is opened
    const connMenuItem = document.querySelector('#menubar .menu-item[data-menu="connection"]');
    if (connMenuItem) {
      connMenuItem.addEventListener('click', buildConnectionMenu);
    }
  });

}());
