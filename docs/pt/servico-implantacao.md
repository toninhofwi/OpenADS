---
title: Implantação como serviço
layout: default
parent: Início (PT)
nav_order: 8
permalink: /pt/servico-implantacao/
---

# Implantação como serviço

Desde **v1.0.0-rc14** o mesmo binário `openads_serverd` funciona
também como serviço nativo do SO nas três plataformas. O path de
graceful shutdown é compartilhado com o handler de Ctrl+C
interativo, então um stop limpo executa o mesmo cleanup (flush
de tabelas abertas, drenar frames wire em voo, liberar locks).

## Windows — serviço SCM

O binário se autorregistra via Service Control Manager:

```bat
:: Registrar (SERVICE_AUTO_START no boot). Flags após
:: --install-service ficam embutidas no binPath registrado.
openads_serverd --install-service ^
    --port 6262 ^
    --http-port 6263 ^
    --data C:\app\dados

:: Iniciar
sc start openads_serverd

:: Parar / remover
sc stop  openads_serverd
openads_serverd --uninstall-service
```

Execute `cmd` elevado para install / uninstall.

O serviço roda como `SERVICE_WIN32_OWN_PROCESS`. O handler
respeita `SERVICE_CONTROL_STOP` e `SERVICE_CONTROL_SHUTDOWN`.
Rodar o binário com `--service` **fora** do SCM imprime uma
explicação em vez de travar.

## Linux — systemd

`scripts/openads-serverd.service`:

```ini
[Unit]
Description=OpenADS Server Daemon
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=openads
ExecStart=/usr/local/bin/openads_serverd \
    --port 6262 --http-port 6263 \
    --data /var/lib/openads
Restart=on-failure

# Hardening
ProtectSystem=strict
NoNewPrivileges=yes
PrivateTmp=yes
RestrictAddressFamilies=AF_INET AF_INET6
ReadWritePaths=/var/lib/openads

[Install]
WantedBy=multi-user.target
```

Instalação + ativação:

```sh
sudo useradd --system --no-create-home openads
sudo cp scripts/openads-serverd.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now openads-serverd
sudo systemctl status openads-serverd
```

## macOS — launchd

`scripts/com.openads.serverd.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>com.openads.serverd</string>
  <key>ProgramArguments</key>
  <array>
    <string>/usr/local/bin/openads_serverd</string>
    <string>--port</string><string>6262</string>
    <string>--http-port</string><string>6263</string>
    <string>--data</string><string>/usr/local/var/openads</string>
  </array>
  <key>KeepAlive</key><true/>
  <key>RunAtLoad</key><true/>
  <key>StandardOutPath</key>
  <string>/var/log/openads-serverd.out.log</string>
  <key>StandardErrorPath</key>
  <string>/var/log/openads-serverd.err.log</string>
</dict>
</plist>
```

Em todo o sistema (no boot):

```sh
sudo cp scripts/com.openads.serverd.plist /Library/LaunchDaemons/
sudo launchctl load /Library/LaunchDaemons/com.openads.serverd.plist
```

Por usuário (no login):

```sh
cp scripts/com.openads.serverd.plist ~/Library/LaunchAgents/
launchctl load ~/Library/LaunchAgents/com.openads.serverd.plist
```

## Health check

```sh
curl http://localhost:6263/api/health
# {"status":"ok","mode":"remote-server","version":"1.0.0-rc22", ... }
```

O campo `mode` é o que o badge de modo do Studio usa (rc10).
