#!/usr/bin/env python3
# WiFi link monitor for a Raft device
#
# Pings the device under test (DUT), a control device and the gateway every second, polls HTTP on the devices,
# records the host's own WiFi link and captures the DUT's serial console (with host timestamps).
# When the DUT becomes unreachable while the gateway is reachable (an "outage"), the DUT is investigated over
# its serial console: link diagnostics are read, a ping from the DUT to the gateway is started (does the DUT
# still reach the network?), self-recovery is awaited and, if it doesn't come, a reconnect is forced.
#
# Usage: monitor.py --dut 192.168.86.52 --port /dev/ttyACM1 --control 192.168.86.43 --minutes 60 --out <dir>
# Needs pyserial (for --port). Output: <out>/events.jsonl, <out>/samples.csv, <out>/serial.log, summaries on stdout

import argparse, json, os, re, subprocess, threading, time, urllib.request, statistics

def now():
    return time.time()

def ts(t=None):
    t = now() if t is None else t
    return time.strftime('%H:%M:%S', time.localtime(t)) + '.%03d' % int((t % 1) * 1000)

class Monitor:
    def __init__(self, args):
        self.args = args
        os.makedirs(args.out, exist_ok=True)
        self.lock = threading.Lock()
        self.events = open(os.path.join(args.out, 'events.jsonl'), 'a')
        self.samples = open(os.path.join(args.out, 'samples.csv'), 'a')
        self.serialLog = open(os.path.join(args.out, 'serial.log'), 'a')
        self.targets = {'dut': args.dut, 'gw': args.gateway}
        if args.control:
            self.targets['ctl'] = args.control
        self.hist = {k: [] for k in self.targets}          # (t, rttMs or None)
        self.consecFail = {k: 0 for k in self.targets}
        self.outage = None                                  # dict while an outage is in progress
        self.outages = []
        self.serialLines = []                               # recent (t, line) for investigation
        self.ser = None
        self.stop = False
        self.serialLock = threading.Lock()

    # ---------------------------------------------------------------- logging
    def event(self, kind, **kw):
        rec = {'t': ts(), 'kind': kind}
        rec.update(kw)
        with self.lock:
            self.events.write(json.dumps(rec) + '\n')
            self.events.flush()
        print('%s EVENT %s %s' % (rec['t'], kind, json.dumps(kw)), flush=True)

    # ---------------------------------------------------------------- probes
    @staticmethod
    def ping(ip):
        try:
            out = subprocess.run(['ping', '-c1', '-W1', ip], capture_output=True, text=True, timeout=3).stdout
            m = re.search(r'time=([\d.]+) ms', out)
            return float(m.group(1)) if m else None
        except Exception:
            return None

    @staticmethod
    def http(ip, path, timeout=3):
        try:
            with urllib.request.urlopen('http://%s/api/%s' % (ip, path), timeout=timeout) as r:
                return json.loads(r.read().decode(errors='replace'))
        except Exception as e:
            return None

    def hostLink(self):
        try:
            out = subprocess.run(['iw', 'dev', self.args.hostif, 'link'], capture_output=True, text=True, timeout=3).stdout
            sig = re.search(r'signal: (-?\d+)', out)
            rate = re.search(r'tx bitrate: ([\d.]+)', out)
            return {'sig': int(sig.group(1)) if sig else None, 'txMbps': float(rate.group(1)) if rate else None}
        except Exception:
            return {}

    # ---------------------------------------------------------------- serial
    def serialThread(self):
        import serial
        buf = b''
        while not self.stop:
            try:
                if self.ser is None:
                    self.ser = serial.Serial(self.args.port, 115200, timeout=0.2)
                d = self.ser.read(4096)
            except Exception as e:
                self.event('serialError', err=str(e))
                self.ser = None
                time.sleep(2)
                continue
            if not d:
                continue
            buf += d
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                s = line.decode(errors='replace').rstrip('\r')
                t = now()
                with self.lock:
                    self.serialLog.write('%s %s\n' % (ts(t), s))
                    self.serialLog.flush()
                    self.serialLines.append((t, s))
                    if len(self.serialLines) > 2000:
                        del self.serialLines[:1000]
                # Notable device lines
                if re.search(r'WiFiDiag|wifi:.*(DELBA|disconnect|bcn|beacon|reason|state:)|disconnected|rst:|Guru|abort|WiFi station', s):
                    self.event('dutLog', line=s[:300])

    def serialCmd(self, cmd, waitS=2.0, match=None):
        """Send a command line to the DUT's serial console and return response lines containing match"""
        if not self.ser:
            return []
        start = now()
        with self.serialLock:
            try:
                self.ser.write((cmd + '\n').encode())
            except Exception as e:
                return ['serialWriteError %s' % e]
        time.sleep(waitS)
        with self.lock:
            lines = [l for (t, l) in self.serialLines if t >= start]
        return [l for l in lines if (match is None or match in l)]

    def dutDiagSerial(self, opts=''):
        lines = self.serialCmd('wifidiag' + (('?' + opts) if opts else ''), 2.0, '"diag"')
        for l in lines:
            try:
                return json.loads(l[l.index('{'):])['diag']
            except Exception:
                pass
        return None

    # ---------------------------------------------------------------- outage investigation
    def investigate(self, outage):
        self.event('outageInvestigate', start=outage['start'])
        d0 = self.dutDiagSerial()
        self.event('dutDiagAtOutage', diag=d0)
        # Ping the gateway from the DUT
        self.dutDiagSerial('pingMs=500')
        time.sleep(8)
        d1 = self.dutDiagSerial()
        self.event('dutGatewayPing', ping=(d1 or {}).get('ping'), rssi=(d1 or {}).get('rssi'))
        # Wait for self recovery
        deadline = now() + self.args.selfRecoverS
        while now() < deadline and not self.stop:
            if self.consecFail['dut'] == 0:
                self.event('outageSelfRecovered', afterS=round(now() - outage['t0'], 1))
                self.dutDiagSerial('pingMs=0')
                return
            time.sleep(1)
        d2 = self.dutDiagSerial()
        self.event('dutDiagBeforeReconnect', diag=d2)
        # Force a reconnect and see if that recovers it
        self.dutDiagSerial('reconnect=1')
        deadline = now() + 60
        while now() < deadline and not self.stop:
            if self.consecFail['dut'] == 0:
                self.event('outageRecoveredByReconnect', afterS=round(now() - outage['t0'], 1))
                self.dutDiagSerial('pingMs=0')
                return
            time.sleep(1)
        self.event('outageNotRecoveredByReconnect', diag=self.dutDiagSerial())
        if self.args.resetOnFail:
            self.serialCmd('reset', 1)
            self.event('dutResetRequested')

    # ---------------------------------------------------------------- main loops
    def pingLoop(self):
        while not self.stop:
            t0 = now()
            res = {}
            ths = []
            for k, ip in self.targets.items():
                th = threading.Thread(target=lambda k=k, ip=ip: res.__setitem__(k, self.ping(ip)))
                th.start(); ths.append(th)
            for th in ths:
                th.join()
            with self.lock:
                for k, r in res.items():
                    self.hist[k].append((t0, r))
                    self.consecFail[k] = 0 if r is not None else self.consecFail[k] + 1
                self.samples.write('%s,%s\n' % (ts(t0), ','.join('%s=%s' % (k, '' if res[k] is None else res[k]) for k in self.targets)))
                self.samples.flush()
            self.checkOutage(t0)
            time.sleep(max(0, 1.0 - (now() - t0)))

    def checkOutage(self, t):
        dutDown = self.consecFail['dut'] >= self.args.outageFails
        gwUp = self.consecFail['gw'] == 0
        if self.outage is None and dutDown:
            self.outage = {'t0': t - self.args.outageFails, 'start': ts(t - self.args.outageFails),
                           'gwUp': gwUp, 'ctlUp': self.consecFail.get('ctl', 0) == 0}
            self.event('outageStart', gwUp=gwUp, ctlUp=self.outage['ctlUp'],
                       dutRttBefore=[r for (_, r) in self.hist['dut'][-20:-self.args.outageFails]])
            if gwUp and self.args.port:
                threading.Thread(target=self.investigate, args=(self.outage,), daemon=True).start()
        elif self.outage is not None and self.consecFail['dut'] == 0:
            dur = round(t - self.outage['t0'], 1)
            self.outages.append(dur)
            self.event('outageEnd', durS=dur)
            self.outage = None
        # Degraded RTT (but reachable)
        recent = [r for (_, r) in self.hist['dut'][-10:] if r is not None]
        if len(recent) >= 8 and statistics.median(recent) > self.args.slowMs:
            if not getattr(self, '_slowFlag', False):
                self._slowFlag = True
                self.event('dutSlow', medianMs=statistics.median(recent))
        else:
            self._slowFlag = False

    def httpLoop(self):
        while not self.stop:
            for k in ('dut', 'ctl'):
                if k in self.targets:
                    t0 = now()
                    r = self.http(self.targets[k], 'v')
                    if r is None and self.consecFail[k] == 0:
                        self.event('httpFail', target=k)
            hl = self.hostLink()
            with self.lock:
                self.samples.write('%s,hostLink=%s\n' % (ts(), json.dumps(hl)))
            for _ in range(10):
                if self.stop: break
                time.sleep(1)

    def summary(self, final=False):
        with self.lock:
            out = []
            for k in self.targets:
                rs = [r for (_, r) in self.hist[k]]
                ok = [r for r in rs if r is not None]
                loss = 100.0 * (len(rs) - len(ok)) / len(rs) if rs else 0
                p = lambda q: sorted(ok)[min(len(ok) - 1, int(q * len(ok)))] if ok else None
                out.append('%s n=%d loss=%.1f%% p50=%s p95=%s max=%s' % (k, len(rs), loss, p(0.5), p(0.95), max(ok) if ok else None))
        print('%s %s %s outages=%s' % (ts(), 'FINAL' if final else 'SUMMARY', ' | '.join(out), self.outages), flush=True)

    def run(self):
        self.event('start', args=vars(self.args))
        ths = [threading.Thread(target=self.pingLoop, daemon=True), threading.Thread(target=self.httpLoop, daemon=True)]
        if self.args.port:
            ths.append(threading.Thread(target=self.serialThread, daemon=True))
        for th in ths:
            th.start()
        end = now() + self.args.minutes * 60
        nextSum = now() + self.args.summaryS
        while now() < end:
            time.sleep(1)
            if now() >= nextSum:
                nextSum = now() + self.args.summaryS
                self.summary()
        self.stop = True
        time.sleep(1.5)
        self.summary(final=True)
        self.event('end', outages=self.outages)

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--dut', required=True)
    ap.add_argument('--port', default='')
    ap.add_argument('--control', default='')
    ap.add_argument('--gateway', default='192.168.86.1')
    ap.add_argument('--hostif', default='wlp2s0')
    ap.add_argument('--minutes', type=float, default=60)
    ap.add_argument('--out', default='results')
    ap.add_argument('--outageFails', type=int, default=5)
    ap.add_argument('--slowMs', type=float, default=100)
    ap.add_argument('--selfRecoverS', type=int, default=90)
    ap.add_argument('--summaryS', type=int, default=300)
    ap.add_argument('--resetOnFail', action='store_true')
    Monitor(ap.parse_args()).run()
