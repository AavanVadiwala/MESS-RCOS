# Raspberry Pi server

Use a 64-bit Raspberry Pi OS or Ubuntu installation for the full stack. Put
the repository on the Pi and run these commands from its root. Install Docker
Engine and the Compose plugin for your OS first.
[Raspberry Pi OS 64-bit (Debian packages)](https://docs.docker.com/engine/install/debian/)
· [Ubuntu](https://docs.docker.com/engine/install/ubuntu/)

1. Flash the MESS firmware on each ESP32, plug them all into the Pi over USB
   before startup, and close any programs using their serial ports. For several
   boards, use a powered data-capable USB hub.
2. Install the serial dependency and register the boot service.

   ```bash
   sudo apt update
   sudo apt install python3-serial sqlite3
   sudo bash 'server/raspberry pi/install.sh'
   ```

3. Check the collector and Docker. The tables and graphs are available at
   `http://PI_IP:3000` and `http://PI_IP:3838` respectively.

   ```bash
   sudo journalctl -u mess-collector -b -n 60 --no-pager
   sudo docker compose -f software/docker-compose.yml ps
   ```

On each Pi boot, systemd starts the collector before login. The service probes
USB serial ports in sorted order and verifies the MESS firmware prompt. It
assigns and confirms `node` on the other detected ESPs before making the first
detected ESP the `collector`. Role selection requires adapters with working
automatic reset. A board powered on or attached after discovery needs a
`sudo systemctl restart mess-collector` to be included. If the process exits
with an error, including when no board is detected or the collector disconnects,
systemd retries and rediscovers the roles.

The installer starts Docker Compose from `software/` and creates `web/.env`
from the example when needed. Set your own database password in that file
before the first install if you need one. Keep the checkout in place because
Compose mounts files from it. Docker restarts the containers on boot, and the
named Postgres volume persists across container recreation. Use `docker compose
down` without `--volumes` to stop the stack while keeping its database.

## Check the archive

SQLite commits each valid serial record before upload and keeps pending
measurements if Docker or Wi-Fi goes down. The archive is retained across
reboots and service reinstalls. Incomplete sensor measurements remain in
SQLite. A successful API write followed by a lost response can produce a
duplicate in Postgres when the uploader retries.

Before leaving the Pi unattended, run the following twice, a minute apart.
Check that the total grows and that the latest receive time looks correct.
Reboot once with the ESPs connected, then confirm the count continues to grow.
Check free space against observed growth before a week-long run.

```bash
sudo sqlite3 -readonly /var/lib/mess/readings.sqlite3 \
  'SELECT COUNT(*) AS total, MAX(received) AS latest_utc FROM records;'
sudo sqlite3 -readonly /var/lib/mess/readings.sqlite3 \
  "SELECT COALESCE(delivery, 'pending'), COUNT(*) FROM records WHERE payload IS NOT NULL GROUP BY delivery;"
df -h /var/lib
```

For a consistent copy while polling is active, run the SQLite backup command
and use a destination on storage with sufficient space.

```bash
sudo sqlite3 -readonly /var/lib/mess/readings.sqlite3 \
  ".backup /tmp/mess-readings-backup.sqlite3"
```
