<?php
/**
 * DA-Web — Data Architect for OpenADS
 * Main shell: renders the full-page application frame.
 */
?>
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>DA-Web — OpenADS Data Architect</title>

  <!-- Local vendor: jQuery (required by jsTree) -->
  <script src="vendor/jquery/jquery.min.js"></script>

  <!-- Local vendor: jsTree -->
  <link rel="stylesheet" href="vendor/jstree/themes/default/style.min.css">
  <script src="vendor/jstree/jstree.min.js"></script>

  <!-- Local vendor: Tabulator -->
  <link rel="stylesheet" href="vendor/tabulator/css/tabulator.min.css">
  <script src="vendor/tabulator/js/tabulator.min.js"></script>

  <!-- Local vendor: Split.js -->
  <script src="vendor/split.js/split.min.js"></script>

  <!-- Local vendor: Ace editor -->
  <script src="vendor/ace/ace.js"></script>
  <script src="vendor/ace/mode-sql.js"></script>
  <script src="vendor/ace/theme-dracula.js"></script>
  <script src="vendor/ace/ext-language_tools.js"></script>

  <!-- Application styles -->
  <link rel="stylesheet" href="css/app.css">
</head>
<body>
<div id="app">

  <!-- ── Menu bar ─────────────────────────────────────────────────────────── -->
  <div id="menubar">

    <div class="menu-item" data-menu="file">
      File
      <div class="menu-dropdown">
        <div class="drop-item" data-action="open-sql">New SQL Tab</div>
        <div class="drop-separator"></div>
        <div class="drop-item" data-action="exit">Exit</div>
      </div>
    </div>

    <div class="menu-item" data-menu="connection">
      Connection
      <div class="menu-dropdown" id="connection-submenu">
        <div class="drop-item" data-action="create-dd">New DD…</div>
        <div class="drop-item" data-action="open-dd">Open DD…</div>
        <div class="drop-item" data-action="free-tables">Free Tables…</div>
        <div class="drop-separator"></div>
        <div class="drop-item disabled">Loading…</div>
      </div>
    </div>

    <div class="menu-item" data-menu="tools">
      Tools
      <div class="menu-dropdown">
        <div class="drop-item" data-action="refresh-tree">Refresh Tree</div>
      </div>
    </div>

    <div class="menu-item" data-menu="sql">
      SQL
      <div class="menu-dropdown">
        <div class="drop-item" data-action="open-sql">Open SQL Editor</div>
      </div>
    </div>

    <div class="menu-item" data-menu="window">
      Window
      <div class="menu-dropdown">
        <div class="drop-item disabled" id="window-list-placeholder">(no open tabs)</div>
      </div>
    </div>

    <div class="menu-item" data-menu="help">
      Help
      <div class="menu-dropdown">
        <div class="drop-item" data-action="about">About DA-Web</div>
      </div>
    </div>

  </div><!-- #menubar -->

  <!-- ── Workspace ────────────────────────────────────────────────────────── -->
  <div id="workspace">

    <!-- Tree pane -->
    <div id="tree-pane">
      <div class="pane-header">Data Dictionaries</div>
      <div id="tree-container"></div>
    </div>

    <!-- Content pane -->
    <div id="content-pane">

      <!-- Tab bar -->
      <div id="tab-bar">
        <div class="new-tab-btn" id="new-tab-btn" title="New SQL Tab">+</div>
      </div>

      <!-- Tab panels -->
      <div id="tab-content">
        <!-- Panels are injected dynamically by app.js -->
        <div style="display:flex;align-items:center;justify-content:center;height:100%;color:#45475a;font-size:20px;">
          Select a table from the tree to open it, or press + for a SQL editor.
        </div>
      </div>

    </div><!-- #content-pane -->

  </div><!-- #workspace -->

  <!-- ── Status bar ───────────────────────────────────────────────────────── -->
  <div id="statusbar">
    <span id="status-msg">Ready</span>
    <span style="flex:1"></span>
    <span id="status-connections">0 connection(s) open</span>
  </div>

</div><!-- #app -->

<!-- ── Context menu ──────────────────────────────────────────────────────── -->
<div id="ctx-menu"></div>

<!-- ── Modal: New DD (create) ─────────────────────────────────────────────── -->
<div class="modal-overlay" id="modal-create-dd" role="dialog" aria-modal="true">
  <div class="modal">
    <div class="modal-header">
      <span>New Data Dictionary</span>
      <span class="modal-close" onclick="document.getElementById('modal-create-dd').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body">
      <div id="cdd-err" class="modal-err"></div>
      <div class="form-group">
        <label for="cdd-name">Name <span class="req">*</span></label>
        <input type="text" id="cdd-name" placeholder="e.g. Northwind" autocomplete="off">
      </div>
      <div class="form-group">
        <label for="cdd-path">Path for new .add file <span class="req">*</span></label>
        <input type="text" id="cdd-path" placeholder="C:\Data\Northwind.add" autocomplete="off">
      </div>
      <div class="form-group">
        <label for="cdd-password">Admin password <span class="opt">(optional)</span></label>
        <input type="password" id="cdd-password" autocomplete="new-password">
      </div>
      <div class="form-group">
        <label>Connection type</label>
        <div class="toggle-group" id="cdd-conn-type">
          <button type="button" class="toggle-btn active" data-value="local">Local</button>
          <button type="button" class="toggle-btn" data-value="remote">Remote</button>
        </div>
      </div>
    </div>
    <div class="modal-footer">
      <button class="btn" id="cdd-cancel">Cancel</button>
      <button class="btn btn-primary" id="cdd-save">Create</button>
    </div>
  </div>
</div>

<!-- ── Modal: Open DD (add existing) ─────────────────────────────────────── -->
<div class="modal-overlay" id="modal-open-dd" role="dialog" aria-modal="true">
  <div class="modal">
    <div class="modal-header">
      <span>Open Data Dictionary</span>
      <span class="modal-close" onclick="document.getElementById('modal-open-dd').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body">
      <div id="odd-err" class="modal-err"></div>
      <div class="form-group">
        <label for="odd-name">Name <span class="req">*</span></label>
        <input type="text" id="odd-name" placeholder="e.g. Northwind" autocomplete="off">
      </div>
      <div class="form-group">
        <label for="odd-path">Path to .add file <span class="req">*</span></label>
        <input type="text" id="odd-path" placeholder="C:\Data\Northwind.add" autocomplete="off">
      </div>
      <div class="form-group">
        <label for="odd-user">Default username</label>
        <input type="text" id="odd-user" placeholder="AdsSysAdmin" autocomplete="off">
      </div>
      <div class="form-group">
        <label>Connection type</label>
        <div class="toggle-group" id="odd-conn-type">
          <button type="button" class="toggle-btn active" data-value="local">Local</button>
          <button type="button" class="toggle-btn" data-value="remote">Remote</button>
        </div>
      </div>
    </div>
    <div class="modal-footer">
      <button class="btn" id="odd-cancel">Cancel</button>
      <button class="btn btn-primary" id="odd-save">Add</button>
    </div>
  </div>
</div>

<!-- ── Modal: Free Tables directory ──────────────────────────────────────── -->
<div class="modal-overlay" id="modal-free-tables" role="dialog" aria-modal="true">
  <div class="modal">
    <div class="modal-header">
      <span>Add Free Tables Directory</span>
      <span class="modal-close" onclick="document.getElementById('modal-free-tables').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body">
      <div id="ft-err" class="modal-err"></div>
      <div class="form-group">
        <label for="ft-name">Name <span class="req">*</span></label>
        <input type="text" id="ft-name" placeholder="e.g. Legacy Tables" autocomplete="off">
      </div>
      <div class="form-group">
        <label for="ft-path">Directory path <span class="req">*</span></label>
        <input type="text" id="ft-path" placeholder="C:\Data\tables\" autocomplete="off">
      </div>
      <div class="form-group">
        <label>Connection type</label>
        <div class="toggle-group" id="ft-conn-type">
          <button type="button" class="toggle-btn active" data-value="local">Local</button>
          <button type="button" class="toggle-btn" data-value="remote">Remote</button>
        </div>
      </div>
    </div>
    <div class="modal-footer">
      <button class="btn" id="ft-cancel">Cancel</button>
      <button class="btn btn-primary" id="ft-save">Add</button>
    </div>
  </div>
</div>

<!-- ── Modal: Connect ────────────────────────────────────────────────────── -->
<div class="modal-overlay" id="modal-connect" role="dialog" aria-modal="true" aria-labelledby="connect-title">
  <div class="modal">
    <div class="modal-header">
      <span id="connect-title">Connect to <span id="connect-dd-name"></span></span>
      <span class="modal-close" onclick="document.getElementById('modal-connect').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body">
      <div id="connect-err" style="color:#f38ba8;font-size:12px;min-height:16px;margin-bottom:6px;"></div>
      <div class="form-group">
        <label for="connect-username">Username</label>
        <input type="text" id="connect-username" value="AdsSysAdmin" autocomplete="off">
      </div>
      <div class="form-group">
        <label for="connect-password">Password</label>
        <input type="password" id="connect-password" autocomplete="current-password">
      </div>
    </div>
    <div class="modal-footer">
      <button class="btn" id="connect-cancel">Cancel</button>
      <button class="btn btn-primary" id="connect-submit">Connect</button>
    </div>
  </div>
</div>

<!-- ── Modal: About ──────────────────────────────────────────────────────── -->
<div class="modal-overlay" id="modal-about" role="dialog" aria-modal="true">
  <div class="modal">
    <div class="modal-header">
      <span>About DA-Web</span>
      <span class="modal-close" id="about-close">&times;</span>
    </div>
    <div class="modal-body" style="line-height:1.7;font-size:13px;">
      <p><strong>DA-Web</strong> — OpenADS Data Architect</p>
      <p style="margin-top:8px;color:#a6adc8;">Web-based replacement for SAP Data Architect.<br>
      Backend: PHP + php_openads native extension.<br>
      Frontend: jsTree · Tabulator · Split.js · jQuery · Ace Editor.</p>
      <p style="margin-top:12px;color:#45475a;font-size:11px;">
        Part of the <a href="https://github.com/FiveTechSoft/OpenADS" style="color:#89b4fa">OpenADS</a> project.
      </p>
    </div>
    <div class="modal-footer">
      <button class="btn btn-primary" id="about-ok" onclick="document.getElementById('modal-about').classList.remove('open')">OK</button>
    </div>
  </div>
</div>

<!-- ── Modal: Delete row confirmation ────────────────────────────────────── -->
<div class="modal-overlay" id="modal-del-confirm" role="dialog" aria-modal="true">
  <div class="modal" style="max-width:360px">
    <div class="modal-header">
      <span>Delete Row</span>
      <span class="modal-close" onclick="document.getElementById('modal-del-confirm').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body">
      <p style="margin:0;line-height:1.6;">Are you sure you want to delete the selected row?<br>
      <span style="color:#f38ba8;font-size:12px;">This action cannot be undone.</span></p>
    </div>
    <div class="modal-footer">
      <button class="btn" id="del-confirm-cancel">Cancel</button>
      <button class="btn btn-danger" id="del-confirm-ok">Delete</button>
    </div>
  </div>
</div>

<!-- ── Modal: SQL Save script ─────────────────────────────────────────────── -->
<div class="modal-overlay" id="modal-sql-save" role="dialog" aria-modal="true">
  <div class="modal" style="max-width:400px">
    <div class="modal-header">
      <span>Save SQL Script</span>
      <span class="modal-close" onclick="document.getElementById('modal-sql-save').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body">
      <div id="sql-save-err" class="modal-err"></div>
      <div class="form-group">
        <label for="sql-save-name">Script name <span class="req">*</span></label>
        <input type="text" id="sql-save-name" placeholder="e.g. Active leases" autocomplete="off">
      </div>
    </div>
    <div class="modal-footer">
      <button class="btn" id="sql-save-cancel">Cancel</button>
      <button class="btn btn-primary" id="sql-save-ok">Save</button>
    </div>
  </div>
</div>

<!-- ── Modal: SQL Open script ─────────────────────────────────────────────── -->
<div class="modal-overlay" id="modal-sql-open" role="dialog" aria-modal="true">
  <div class="modal" style="max-width:520px">
    <div class="modal-header" style="display:flex;align-items:center;gap:8px;">
      <span style="flex:1">Open SQL Script</span>
      <button class="btn btn-sm" id="sql-open-all" title="Open every saved script as a separate tab">Open All</button>
      <span class="modal-close" onclick="document.getElementById('modal-sql-open').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body" style="padding:0">
      <div id="sql-scripts-list" style="max-height:360px;overflow-y:auto;"></div>
    </div>
  </div>
</div>

<!-- ── Modal: Properties (edit DD / free-tables connection) ──────────────── -->
<div class="modal-overlay" id="modal-props" role="dialog" aria-modal="true">
  <div class="modal" style="max-width:460px">
    <div class="modal-header">
      <span>Properties — <span id="props-title"></span></span>
      <span class="modal-close" onclick="document.getElementById('modal-props').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body">
      <div id="props-err" class="modal-err"></div>
      <input type="hidden" id="props-name">
      <input type="hidden" id="props-entry-type">
      <div class="form-group">
        <label>Name</label>
        <input type="text" id="props-name-display" disabled style="opacity:.6;cursor:default">
      </div>
      <div class="form-group">
        <label for="props-path">Path <span class="req">*</span></label>
        <input type="text" id="props-path" autocomplete="off">
      </div>
      <div class="form-group" id="props-user-row">
        <label for="props-username">Default username</label>
        <input type="text" id="props-username" autocomplete="off">
      </div>
      <div class="form-group">
        <label>Connection type</label>
        <div class="toggle-group" id="props-conn-type">
          <button type="button" class="toggle-btn active" data-value="local">Local</button>
          <button type="button" class="toggle-btn" data-value="remote">Remote</button>
        </div>
      </div>
    </div>
    <div class="modal-footer">
      <button class="btn" id="props-cancel">Cancel</button>
      <button class="btn btn-primary" id="props-save">Save</button>
    </div>
  </div>
</div>

<!-- Application JS (loaded last so DOM is ready) -->
<script src="js/app.js"></script>
</body>
</html>
