#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID} -ne 0 ]]; then
    echo "Run this script with sudo." >&2
    exit 1
fi

pi_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd -- "$pi_dir/../.." && pwd)

if ! /usr/bin/python3 -c 'import serial'; then
    echo "Install python3-serial with apt before running this script." >&2
    exit 1
fi

install -D -m 0644 "$repo/server/windows/collector.py" /usr/local/lib/mess/collector.py
install -m 0644 "$pi_dir/mess-collector.service" /etc/systemd/system/mess-collector.service
systemctl daemon-reload
systemctl enable mess-collector.service
systemctl restart mess-collector.service

if ! command -v docker >/dev/null || ! docker compose version >/dev/null; then
    echo "The collector is enabled. Install Docker Engine and the Compose plugin, then rerun this script to start the dashboard." >&2
    exit 1
fi

cd -- "$repo/software"
if [[ ! -e web/.env ]]; then
    install -m 0600 web/.env.example web/.env
fi
systemctl enable --now docker.service
docker compose up --build -d
