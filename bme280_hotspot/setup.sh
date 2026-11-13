#!/usr/bin/env bash
# Builds and flashes one board of the sensor hotspot setup.
#   ./setup.sh brain            # the board that runs the hotspot and phone page
#   ./setup.sh sensor           # a board with BME280 / BH1750 / SGP30 wired to it
#   ./setup.sh sensor PORT      # pick the USB port yourself
#   ./setup.sh brain --no-monitor
# Run it in an ESP-IDF terminal (idf.py on PATH). Settings shared by both boards are asked
# once and kept in the git-ignored hotspot.local; each board's sdkconfig.local is written from it.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
role=${1:-}
port=''
monitor=1
for arg in "${@:2}"; do
  if [ "$arg" = --no-monitor ]; then monitor=0; else port=$arg; fi
done
case $role in
  brain|sensor) ;;
  *) echo "Usage: $0 brain|sensor [PORT] [--no-monitor]" >&2; exit 2 ;;
esac
if ! command -v idf.py >/dev/null; then
  echo 'idf.py not found. Open an ESP-IDF terminal (or run . $IDF_PATH/export.sh) and try again.' >&2
  exit 1
fi

# ---- Shared settings: asked once, then reused for every board.
settings=$here/hotspot.local
if [ ! -f "$settings" ]; then
  echo "First run: choosing the hotspot settings shared by the brain and every sensor."
  plain() { [[ $1 != *[\'\"\\\$\`]* ]]; } # quotes, backslashes, $ and ` would break the config files
  while :; do
    read -r -p 'Hotspot name [MESS-BME280]: ' ssid
    ssid=${ssid:-MESS-BME280}
    [ ${#ssid} -le 32 ] && plain "$ssid" && break
    echo "Use up to 32 characters, without quotes, backslashes, \$ or \`."
  done
  while :; do
    read -r -s -p 'Hotspot password (8-63 characters): ' password; echo
    [ ${#password} -ge 8 ] && [ ${#password} -le 63 ] && plain "$password" && break
    echo "Use 8 to 63 characters, without quotes, backslashes, \$ or \`."
  done
  while :; do
    read -r -s -p 'Visitor key for uploading to the live site (Enter to skip): ' visitor; echo
    plain "$visitor" && break
    echo "Use a key without quotes, backslashes, \$ or \`."
  done
  # The live site's Supabase project. Both values are public by design (see README).
  umask 077
  cat > "$settings" <<EOF
HOTSPOT_SSID='$ssid'
HOTSPOT_PASSWORD='$password'
VISITOR_KEY='$visitor'
SUPABASE_URL='https://imaoyqoegvrdtqiqzlvh.supabase.co'
SUPABASE_KEY='sb_publishable_cTJj9EWJ9x8CFQF3MKAPFA_o5ySAsIK'
EOF
  echo "Saved in $settings (git-ignored). Delete it to choose again."
fi
# shellcheck disable=SC1090
. "$settings"

# ---- This board's sdkconfig.local, unless someone wrote their own by hand.
project=$here/$role
local_config=$project/sdkconfig.local
marker='# Written by setup.sh from hotspot.local'
if [ -f "$local_config" ] && ! head -1 "$local_config" | grep -qF "$marker"; then
  echo "Keeping your own $local_config (it was not written by setup.sh)."
else
  new=$(mktemp)
  {
    echo "$marker; edit hotspot.local instead."
    if [ "$role" = brain ]; then
      echo "CONFIG_MESS_GW_SSID=\"$HOTSPOT_SSID\""
      echo "CONFIG_MESS_GW_PASSWORD=\"$HOTSPOT_PASSWORD\""
      echo "CONFIG_MESS_GW_SUPABASE_URL=\"$SUPABASE_URL\""
      echo "CONFIG_MESS_GW_SUPABASE_KEY=\"$SUPABASE_KEY\""
      echo "CONFIG_MESS_GW_VISITOR_KEY=\"$VISITOR_KEY\""
    else
      echo "CONFIG_MESS_BME_WIFI_SSID=\"$HOTSPOT_SSID\""
      echo "CONFIG_MESS_BME_WIFI_PASSWORD=\"$HOTSPOT_PASSWORD\""
      echo 'CONFIG_MESS_BME_SERVER_URL="http://192.168.4.1/readings"'
    fi
  } > "$new"
  if ! cmp -s "$new" "$local_config" 2>/dev/null; then
    mv "$new" "$local_config"; chmod 600 "$local_config"
    rm -f "$project/sdkconfig" # settings changed: regenerate the build configuration
  else
    rm -f "$new"
  fi
fi

# ---- USB port: the only ESP32-looking port, or ask.
if [ -z "$port" ]; then
  ports=()
  for p in /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/cu.usbmodem* /dev/ttyUSB* /dev/ttyACM*; do
    [ -e "$p" ] && ports+=("$p")
  done
  if [ ${#ports[@]} -eq 0 ]; then # older Silicon Labs drivers on macOS name the port only this way
    for p in /dev/cu.SLAB_USBtoUART*; do [ -e "$p" ] && ports+=("$p"); done
  fi
  if [ ${#ports[@]} -eq 0 ]; then
    echo 'No ESP32 found on USB. Use a data (not charge-only) cable, install the CP210x or CH340 driver if needed, and retry.' >&2
    exit 1
  elif [ ${#ports[@]} -eq 1 ]; then
    port=${ports[0]}
  else
    echo 'More than one board is plugged in:'
    for i in "${!ports[@]}"; do echo "  $((i + 1))) ${ports[$i]}"; done
    read -r -p "Which one is the $role? " choice
    port=${ports[$((choice - 1))]:-}
    [ -n "$port" ] || { echo 'No such port.' >&2; exit 1; }
  fi
fi
echo "Flashing the $role firmware to $port"

# ---- Build, flash and (optionally) watch it start.
cd "$project"
[ -f sdkconfig ] || idf.py -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.local' set-target esp32
idf.py -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.local' build
idf.py -p "$port" flash
if [ $monitor = 1 ]; then
  echo 'Watching the board; press Ctrl+] to stop.'
  idf.py -p "$port" monitor
fi
