#!/usr/bin/env bash
# Run one of the raftjs test scripts in this folder inside the raftbuilder docker image (which has Node 20)
# Usage: ./run.sh <script.js> <device-ip> [args...]
# e.g.   ./run.sh try.js 192.168.86.52 "devman/cmdraw?deviceid=1_68&hexWr=75&numToRd=1" 5
#        ./run.sh deftest.js 192.168.86.52
# The first run copies ../../../raftjs (sibling of RaftCore) into .raftjs and builds it (needs network for npm)
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
RAFTJS_SRC=${RAFTJS_SRC:-$(cd "$HERE/../../../raftjs" && pwd)}
IMAGE=${IMAGE:-raftbuilder:latest}
if [ ! -f "$HERE/.raftjs/dist/web/main.js" ]; then
  echo "Building raftjs from $RAFTJS_SRC into $HERE/.raftjs ..."
  rm -rf "$HERE/.raftjs"; mkdir -p "$HERE/.raftjs"
  (cd "$RAFTJS_SRC" && tar --exclude=node_modules --exclude=dist --exclude=examples -cf - .) | (cd "$HERE/.raftjs" && tar -xf -)
  docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$HERE/.raftjs:/work" -w /work "$IMAGE" \
    bash -c 'npm ci --no-audit --no-fund --ignore-scripts >/dev/null && npm i --no-save --no-audit --no-fund ws >/dev/null && npm run build >/dev/null'
  # raftjs selects its BLE module via the bundler - plain Node needs this shim
  echo 'module.exports = require("./RaftChannelBLE.web.js");' > "$HERE/.raftjs/dist/web/RaftChannelBLE.js"
fi
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$HERE:/work" -w /work "$IMAGE" bash -c "node $(printf '%q ' "$@")" 2>&1 \
  | grep -v -E '^(Checking|Python|Activating|Setting IDF_PATH|\* |Done!|Go to the project|  idf.py build|"python3" has been detected|$)'
