---
title: Despliegue como servicio
layout: default
parent: Inicio (ES)
nav_order: 8
permalink: /es/servicio-despliegue/
---

# Despliegue como servicio

Desde **v1.0.0-rc14** el mismo binario `openads_serverd` actúa
también como servicio nativo del SO en las tres plataformas. El
path de shutdown graceful se comparte con el handler interactivo
de Ctrl+C, así que un stop limpio ejecuta el mismo cleanup
(flush tablas abiertas, drenar frames wire en vuelo, liberar
locks).

## Windows — servicio SCM

El binario se autoregistra vía Service Control Manager:

```bat
:: Registrar (SERVICE_AUTO_START al boot). Flags después de
:: --install-service quedan en el binPath registrado.
openads_serverd --install-service ^
    --port 6262 ^
    --http-port 6263 ^
    --data C:\app\datos

:: Iniciar
sc start openads_serverd

:: Parar / quitar
sc stop  openads_serverd
openads_serverd --uninstall-service
```

Ejecuta `cmd` elevado para install / uninstall.

El servicio corre como `SERVICE_WIN32_OWN_PROCESS`. El handler
honra `SERVICE_CONTROL_STOP` y `SERVICE_CONTROL_SHUTDOWN`.
Ejecutar el binario con `--service` **fuera** del SCM imprime
una explicación en vez de quedarse colgado.

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

Instalación + activación:

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

System-wide (al boot):

```sh
sudo cp scripts/com.openads.serverd.plist /Library/LaunchDaemons/
sudo launchctl load /Library/LaunchDaemons/com.openads.serverd.plist
```

Per-user (al login):

```sh
cp scripts/com.openads.serverd.plist ~/Library/LaunchAgents/
launchctl load ~/Library/LaunchAgents/com.openads.serverd.plist
```

## Healthcheck

```sh
curl http://localhost:6263/api/health
# {"status":"ok","mode":"remote-server","version":"1.0.0-rc22", ... }
```

El campo `mode` es lo que usa el badge de modo de Studio (rc10).
