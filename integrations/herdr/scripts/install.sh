#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

mkdir -p bin

echo "=== Installing Agent Pet Herdr Plugin ==="

# 1. Build herdr-agent-pet CLI binary
if command -v go >/dev/null 2>&1; then
    echo "Compiling herdr-agent-pet..."
    CGO_ENABLED=0 go build -trimpath -ldflags "-s -w" -o bin/herdr-agent-pet ./cmd/herdr-agent-pet
    chmod +x bin/herdr-agent-pet
    echo "✓ Built bin/herdr-agent-pet"
else
    echo "Error: 'go' is required to compile herdr-agent-pet." >&2
    exit 1
fi

# 2. Check for agent-pet desktop executable
FOUND_AGENT_PET=""

if command -v agent-pet >/dev/null 2>&1; then
    FOUND_AGENT_PET=$(command -v agent-pet)
elif [ -x "$HOME/.local/bin/agent-pet" ]; then
    FOUND_AGENT_PET="$HOME/.local/bin/agent-pet"
elif [ -x "../../starter/build/agent-pet" ]; then
    FOUND_AGENT_PET="../../starter/build/agent-pet"
elif [ -d "/Applications/Agent Pet.app" ]; then
    FOUND_AGENT_PET="/Applications/Agent Pet.app"
fi

if [ -n "$FOUND_AGENT_PET" ]; then
    echo "✓ Found Agent Pet at: $FOUND_AGENT_PET"
else
    echo "Note: agent-pet binary was not found on PATH or standard locations."
    echo "You can install it from: https://github.com/WindyWin/vpet-agent-pet/releases"
fi

echo "✓ Agent Pet Herdr plugin installed successfully."
