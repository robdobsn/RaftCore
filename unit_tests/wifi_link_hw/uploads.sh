#!/bin/bash
# Repeated curl uploads of a 100KB file (a heavy receive load on the device) for a number of minutes
# Usage: uploads.sh <ip> <minutes> [file]
# Prints one line per upload: time, HTTP status, total time and the connected BSSID if known
IP=$1; MINUTES=$2; FILE=${3:-/tmp/stress_upload.bin}
[ -f "$FILE" ] || head -c 102400 /dev/urandom > "$FILE"
END=$(( $(date +%s) + MINUTES * 60 ))
while [ $(date +%s) -lt $END ]; do
    R=$(curl -s -o /dev/null -w '%{http_code} %{time_total}' -m 60 -F "file=@$FILE" "http://$IP/api/fileupload")
    echo "$(date +%H:%M:%S) $R"
done
