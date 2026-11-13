// Usage: node start.js
// Runs `docker compose up -d --build`, then prints this computer's LAN URL and a QR code for it.
const { spawnSync } = require('child_process');
const { createRequire } = require('module');
const os = require('os');
const path = require('path');

const PORT = 3000;
const VIRTUAL = /vethernet|wsl|docker|virtualbox|vmware|vpn|loopback|tailscale|hyper-v/i;

function lanIp() {
  for (const [name, addrs] of Object.entries(os.networkInterfaces())) {
    if (VIRTUAL.test(name)) continue;
    const v4 = addrs.find((a) => a.family === 'IPv4' && !a.internal);
    if (v4) return v4.address;
  }
  return null;
}

const compose = (...args) => spawnSync('docker', ['compose', ...args], { stdio: 'inherit', cwd: __dirname });

const ip = lanIp();
if (!ip) {
  console.log('No LAN IP found. Run ipconfig and use your IPv4 address.\n');
} else {
  const url = `http://${ip}:${PORT}`;
  console.log(`\nWebapp:        ${url}`);
  console.log(`Send data to:  ${url}/data\n`);

  const qr = createRequire(path.join(__dirname, 'web', 'package.json'))('qrcode-terminal');
  qr.generate(url, { small: true });
}
console.log('\nStarting... press Ctrl+C to stop.\n');

// Ctrl+C reaches docker compose, which stops the containers; node must not die before cleanup
process.on('SIGINT', () => {});
compose('up', '--build');
compose('down'); // remove the stopped containers; the database volume is kept
