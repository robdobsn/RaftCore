#!/usr/bin/env python3
# Measure link quality against WiFi TX power and AP for a Raft device with WiFiDiagnostics
#
# For each AP (BSSID) and TX power the device is locked to the AP (over its serial console as the link may fail),
# then a short load test is run (stress.py: HTTP bursts, downloads (device transmits) and uploads (device receives))
# while the device, gateway and control are pinged. Recovery is disabled during the tests and restored at the end.
#
# Usage: txpower_matrix.py --dut <ip> --port <serial port> --bssids <b1,b2> --powers 20,15,11,8 [--minutes 3]

import argparse, json, re, serial, subprocess, sys, time, os

def serialCmd(ser, cmd, waitS=2.5):
    ser.read(100000)
    ser.write((cmd + '\n').encode())
    t0 = time.time(); out = b''
    while time.time() - t0 < waitS:
        out += ser.read(8192)
    s = out.decode(errors='replace')
    for l in s.splitlines():
        if '"diag"' in l:
            try:
                return json.loads(l[l.index('{'):])['diag'], s
            except Exception:
                pass
    return None, s

def ping(ip):
    out = subprocess.run(['ping', '-c1', '-W1', ip], capture_output=True, text=True).stdout
    m = re.search(r'time=([\d.]+) ms', out)
    return float(m.group(1)) if m else None

def runLoad(args, minutes):
    # Ping the DUT while the load runs
    proc = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(__file__), 'stress.py'), '--dut', args.dut,
                             '--minutes', str(minutes), '--idleS', '5', '--phaseS', '40'], stdout=subprocess.PIPE, text=True)
    rtts = []
    while proc.poll() is None:
        rtts.append(ping(args.dut))
        time.sleep(1)
    phases = {}
    for l in proc.stdout.read().splitlines():
        m = re.search(r'PHASE (\w+) (\{.*\}) kBps=([\d.]+)', l)
        if m:
            st = json.loads(m.group(2)); p = phases.setdefault(m.group(1), {'ok': 0, 'fail': 0, 'kBps': []})
            p['ok'] += st['ok']; p['fail'] += st['fail']; p['kBps'].append(float(m.group(3)))
    ok = [r for r in rtts if r is not None]
    loss = 100.0 * (len(rtts) - len(ok)) / len(rtts) if rtts else 100
    med = sorted(ok)[len(ok) // 2] if ok else None
    return phases, loss, med

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--dut', required=True)
    ap.add_argument('--port', required=True)
    ap.add_argument('--bssids', required=True)
    ap.add_argument('--powers', default='20,15,11,8')
    ap.add_argument('--minutes', type=float, default=3)
    args = ap.parse_args()
    ser = serial.Serial(args.port, 115200, timeout=0.1)
    serialCmd(ser, 'wifidiag?pingRecover=0&pingMs=1000')
    print('bssid             txPwr | assoc reasons      | rssi | gwPing  | dutLoss medRtt | burst ok/fail | download kBps (ok/fail) | upload kBps (ok/fail)', flush=True)
    for bssid in args.bssids.split(','):
        for pwr in args.powers.split(','):
            serialCmd(ser, 'wifidiag?txPwr=%s' % pwr, 1.5)
            d, s = serialCmd(ser, 'wifidiag?bssid=%s' % bssid, 25)
            reasons = ','.join(re.findall(r'reason (\d+)', s)[:6])
            d0, _ = serialCmd(ser, 'wifidiag', 1.5)
            linkOk = d0 and d0['link'] == 1 and d0['bssid'] == bssid
            reachable = any(ping(args.dut) is not None for _ in range(5))
            if not (linkOk and reachable):
                print('%s %5s | %-18s | %4s | -       | not connected/unreachable' % (bssid, pwr, reasons, d0['rssi'] if d0 else '-'), flush=True)
                continue
            p0 = d0['ping']
            phases, loss, med = runLoad(args, args.minutes)
            d1, _ = serialCmd(ser, 'wifidiag', 1.5)
            p1 = d1['ping'] if d1 else p0
            gw = '%d/%d' % (p1['recv'] - p0['recv'], p1['sent'] - p0['sent'])
            fmt = lambda k: '%.1f (%d/%d)' % (sum(phases[k]['kBps']) / len(phases[k]['kBps']), phases[k]['ok'], phases[k]['fail']) if k in phases else '-'
            stillOn = d1 and d1['bssid'] == bssid
            print('%s %5s | %-18s | %4s | %-7s | %5.1f%% %6s | %13s | %23s | %21s%s' % (bssid, pwr, reasons, d0['rssi'], gw, loss, med,
                  '%d/%d' % (phases['httpBurst']['ok'], phases['httpBurst']['fail']) if 'httpBurst' in phases else '-',
                  fmt('download'), fmt('upload'), '' if stillOn else ' (LEFT AP)'), flush=True)
    serialCmd(ser, 'wifidiag?bssid=any&txPwr=0&pingMs=5000&pingRecover=4', 15)
    d, _ = serialCmd(ser, 'wifidiag', 1.5)
    print('restored:', d['bssid'] if d else '?', d['rssi'] if d else '?', d['txPwr'] if d else '?', flush=True)
