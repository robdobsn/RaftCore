#!/usr/bin/env python3
# WiFi link load generator for a Raft device (run alongside monitor.py)
#
# Cycles through phases: idle, burst of small HTTP requests, repeated download of a 100KB file (device transmits),
# repeated upload of a 100KB file (device receives) and optionally a reboot every N cycles.
#
# Usage: stress.py --dut 192.168.86.52 --minutes 60 [--idleS 120] [--phaseS 60] [--rebootEvery 0]

import argparse, json, os, threading, time, urllib.request, uuid

def ts():
    return time.strftime('%H:%M:%S')

def get(url, timeout=5):
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r:
            return len(r.read())
    except Exception:
        return None

def upload(ip, name, data, timeout=15):
    boundary = uuid.uuid4().hex
    body = (('--%s\r\nContent-Disposition: form-data; name="file"; filename="%s"\r\n'
             'Content-Type: application/octet-stream\r\n\r\n') % (boundary, name)).encode() + data + ('\r\n--%s--\r\n' % boundary).encode()
    req = urllib.request.Request('http://%s/api/fileupload' % ip, data=body,
                                 headers={'Content-Type': 'multipart/form-data; boundary=%s' % boundary})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode()).get('rslt') == 'ok'
    except Exception:
        return False

def runPhase(name, fn, seconds, threads=1):
    stats = {'ok': 0, 'fail': 0, 'bytes': 0}
    lock = threading.Lock()
    end = time.time() + seconds
    def worker():
        while time.time() < end:
            r = fn()
            with lock:
                if r is None or r is False:
                    stats['fail'] += 1
                    time.sleep(0.5)
                else:
                    stats['ok'] += 1
                    stats['bytes'] += r if isinstance(r, int) else 0
    ths = [threading.Thread(target=worker) for _ in range(threads)]
    t0 = time.time()
    for t in ths: t.start()
    for t in ths: t.join()
    dt = time.time() - t0
    print('%s PHASE %s %s kBps=%.1f' % (ts(), name, json.dumps(stats), stats['bytes'] / 1024 / dt), flush=True)

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--dut', required=True)
    ap.add_argument('--minutes', type=float, default=60)
    ap.add_argument('--idleS', type=int, default=120)
    ap.add_argument('--phaseS', type=int, default=60)
    ap.add_argument('--rebootEvery', type=int, default=0)
    args = ap.parse_args()
    ip = args.dut
    data = os.urandom(100 * 1024)
    upload(ip, 'stress.bin', data)
    end = time.time() + args.minutes * 60
    cycle = 0
    while time.time() < end:
        cycle += 1
        print('%s CYCLE %d' % (ts(), cycle), flush=True)
        print('%s PHASE idle %ds' % (ts(), args.idleS), flush=True)
        time.sleep(args.idleS)
        runPhase('httpBurst', lambda: get('http://%s/api/v' % ip), args.phaseS, threads=4)
        runPhase('download', lambda: get('http://%s/files/local/stress.bin' % ip, timeout=15), args.phaseS, threads=2)
        runPhase('upload', lambda: (len(data) if upload(ip, 'stress.bin', data) else None), args.phaseS, threads=1)
        if args.rebootEvery and (cycle % args.rebootEvery == 0):
            print('%s PHASE reboot' % ts(), flush=True)
            get('http://%s/api/reset' % ip, timeout=3)
            time.sleep(25)
    print('%s STRESS DONE' % ts(), flush=True)
