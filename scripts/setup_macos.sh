#!/usr/bin/env bash
# OpenADS — one-shot macOS bring-up + build + test.
#
# Run from a fresh user shell on the iMac:
#
#   curl -fsSL https://raw.githubusercontent.com/FiveTechSoft/OpenADS/main/scripts/setup_macos.sh | bash
#
# or, if the repo is already cloned:
#
#   bash scripts/setup_macos.sh
#
# Idempotent — safe to re-run. Prints each step.

set -e

OPENADS_REPO="${OPENADS_REPO:-https://github.com/FiveTechSoft/OpenADS}"
OPENADS_DIR="${OPENADS_DIR:-$HOME/openads}"
SUDO_PW="${SUDO_PW:-}"

log() { printf '\033[1;36m[setup]\033[0m %s\n' "$*"; }
ok()  { printf '\033[1;32m[ ok ]\033[0m %s\n' "$*"; }
warn(){ printf '\033[1;33m[warn]\033[0m %s\n' "$*"; }
fail(){ printf '\033[1;31m[fail]\033[0m %s\n' "$*"; exit 1; }

# ---------- platform sanity ----------
if [[ "$(uname -s)" != "Darwin" ]]; then
    fail "This script is for macOS. Run setup_linux.sh on Linux."
fi
log "macOS $(sw_vers -productVersion) on $(uname -m) ($(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo cpu))"

# ---------- sudo helper ----------
sudo_run() {
    if [[ -n "$SUDO_PW" ]]; then
        echo "$SUDO_PW" | sudo -S "$@"
    else
        sudo "$@"
    fi
}

# ---------- 1. Remote Login (SSH) ----------
log "Checking Remote Login (SSH)…"
if ! sudo_run systemsetup -getremotelogin 2>/dev/null | grep -qi "On"; then
    log "Enabling Remote Login via systemsetup"
    sudo_run systemsetup -setremotelogin on >/dev/null 2>&1 || \
        warn "systemsetup -setremotelogin failed (need Full Disk Access for the terminal?)"
fi
sudo_run systemsetup -getremotelogin 2>/dev/null | sed 's/^/  /' || true
ok "SSHd state checked"

# ---------- 2. Xcode Command Line Tools ----------
log "Checking Xcode Command Line Tools…"
if ! xcode-select -p >/dev/null 2>&1; then
    log "Triggering xcode-select --install (a system dialog will appear)"
    xcode-select --install || true
    log "Waiting for CLT… (re-run this script after the install dialog finishes)"
    until xcode-select -p >/dev/null 2>&1; do sleep 5; done
fi
ok "CLT at: $(xcode-select -p)"

# ---------- 3. Homebrew ----------
log "Checking Homebrew…"
if ! command -v brew >/dev/null 2>&1; then
    log "Installing Homebrew (non-interactive)"
    NONINTERACTIVE=1 /bin/bash -c \
        "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    # Add brew to PATH for this shell + future sessions.
    if [[ -x /opt/homebrew/bin/brew ]]; then BREW=/opt/homebrew/bin/brew
    elif [[ -x /usr/local/bin/brew ]];   then BREW=/usr/local/bin/brew
    else fail "brew not found after install"
    fi
    eval "$($BREW shellenv)"
    if ! grep -q "$BREW shellenv" "$HOME/.zprofile" 2>/dev/null; then
        echo "eval \"\$($BREW shellenv)\"" >> "$HOME/.zprofile"
    fi
fi
ok "brew $(brew --version | head -1)"

# ---------- 4. cmake + ninja ----------
for tool in cmake ninja; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        log "Installing $tool via brew"
        brew install "$tool"
    fi
done
ok "cmake $(cmake --version | head -1 | awk '{print $3}'), ninja $(ninja --version)"

# ---------- 5. Clone or update repo ----------
log "Repo at $OPENADS_DIR"
if [[ -d "$OPENADS_DIR/.git" ]]; then
    git -C "$OPENADS_DIR" fetch origin
    git -C "$OPENADS_DIR" checkout main
    git -C "$OPENADS_DIR" pull --ff-only origin main
else
    git clone --depth 1 "$OPENADS_REPO" "$OPENADS_DIR"
fi
ok "HEAD: $(git -C "$OPENADS_DIR" log -1 --oneline)"

# ---------- 6. Configure + build ----------
log "Configuring (default preset = Release)"
cd "$OPENADS_DIR"
cmake --preset default

log "Building"
cmake --build build/default --config Release -- -j"$(sysctl -n hw.ncpu)"

# ---------- 7. Run tests ----------
log "Running unit tests"
ctest --test-dir build/default --output-on-failure -C Release || \
    warn "Some tests failed — see output above"

# ---------- 8. Network info ----------
log "Network listeners (so the OpenADS server can be reached over LAN):"
ifconfig | awk '/^en[0-9]+:/{ifc=$1} /inet /{print "  " ifc " " $2}' | grep -v "127.0.0.1" || true

ok "OpenADS bring-up complete on $(hostname)."
log "Repo dir: $OPENADS_DIR"
log "ace64 dylib: $OPENADS_DIR/build/default/src/libace64.dylib"
