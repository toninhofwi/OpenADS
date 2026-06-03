#Requires -Version 5
# DA-Web vendor download script
# Downloads all client-side libraries so the app works without internet access.

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

$vendors = @(
    @{ url = 'https://code.jquery.com/jquery-3.7.1.min.js'
       dest = 'vendor\jquery\jquery.min.js' },
    @{ url = 'https://cdnjs.cloudflare.com/ajax/libs/jstree/3.3.16/jstree.min.js'
       dest = 'vendor\jstree\jstree.min.js' },
    @{ url = 'https://cdnjs.cloudflare.com/ajax/libs/jstree/3.3.16/themes/default/style.min.css'
       dest = 'vendor\jstree\themes\default\style.min.css' },
    @{ url = 'https://cdnjs.cloudflare.com/ajax/libs/jstree/3.3.16/themes/default/throbber.gif'
       dest = 'vendor\jstree\themes\default\throbber.gif' },
    @{ url = 'https://cdnjs.cloudflare.com/ajax/libs/jstree/3.3.16/themes/default/32px.png'
       dest = 'vendor\jstree\themes\default\32px.png' },
    @{ url = 'https://cdnjs.cloudflare.com/ajax/libs/jstree/3.3.16/themes/default/40px.png'
       dest = 'vendor\jstree\themes\default\40px.png' },
    @{ url = 'https://unpkg.com/tabulator-tables@6.3.0/dist/js/tabulator.min.js'
       dest = 'vendor\tabulator\js\tabulator.min.js' },
    @{ url = 'https://unpkg.com/tabulator-tables@6.3.0/dist/css/tabulator.min.css'
       dest = 'vendor\tabulator\css\tabulator.min.css' },
    @{ url = 'https://unpkg.com/split.js@1.6.5/dist/split.min.js'
       dest = 'vendor\split.js\split.min.js' },

    # Ace Editor 1.32 — SQL editor in triggers, stored procedures, and SQL tabs
    @{ url = 'https://cdnjs.cloudflare.com/ajax/libs/ace/1.32.7/ace.min.js'
       dest = 'vendor\ace\ace.js' },
    @{ url = 'https://cdnjs.cloudflare.com/ajax/libs/ace/1.32.7/mode-sql.min.js'
       dest = 'vendor\ace\mode-sql.js' },
    @{ url = 'https://cdnjs.cloudflare.com/ajax/libs/ace/1.32.7/theme-dracula.min.js'
       dest = 'vendor\ace\theme-dracula.js' },
    @{ url = 'https://cdnjs.cloudflare.com/ajax/libs/ace/1.32.7/ext-language_tools.min.js'
       dest = 'vendor\ace\ext-language_tools.js' }
)

foreach ($v in $vendors) {
    $destPath = Join-Path $root $v.dest
    $dir = Split-Path $destPath
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Force $dir | Out-Null
    }
    Write-Host "Downloading $($v.dest)..."
    Invoke-WebRequest -Uri $v.url -OutFile $destPath -UseBasicParsing
}

Write-Host "`nDone. All vendor files downloaded." -ForegroundColor Green
