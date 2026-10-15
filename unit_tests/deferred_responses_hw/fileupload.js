// Upload a file over RICSerial (WebSocket) with raftjs a number of times and report success and timing
// Usage: ./run.sh fileupload.js <ip> [count] [sizeKB]
const { RaftConnector, RaftLog } = require('./.raftjs/dist/web/main.js');
RaftLog.setLogLevel(1);
const ip = process.argv[2]; const count = Number(process.argv[3] || 3); const sizeKB = Number(process.argv[4] || 100);
const sysType = { nameForDialogs: 'U', connectorOptions: { wsSuffix: 'ws', syncTimeOnConnect: false }, setup: () => {}, subscribeForUpdates: null, stateIsInvalid: () => {}, rxOtherMsgType: () => {} };
const http = async p => (await fetch(`http://${ip}/api/${p}`)).json();
(async () => {
  const c = new RaftConnector(async () => sysType); await c.initializeChannel('WebSocket'); if (!await c.connect(ip)) throw new Error('connect');
  const data = new Uint8Array(sizeKB * 1024); for (let i = 0; i < data.length; i++) data[i] = (i * 7 + 3) & 0xff;
  const s0 = (await http('sysmoddebug/SysMan')).debug.slow;
  let ok = 0; const times = [];
  for (let i = 0; i < count; i++) {
    const t0 = Date.now();
    const r = await c.sendFile(`rictest${i}.bin`, data, undefined).catch(e => { console.log('error', e && e.message); return false; });
    times.push((Date.now() - t0) / 1000); if (r) ok++;
    console.log(`upload ${i}: ${r ? 'ok' : 'FAILED'} ${times[i].toFixed(1)}s ${(sizeKB / times[i]).toFixed(1)} kB/s`);
  }
  const s1 = (await http('sysmoddebug/SysMan')).debug.slow;
  console.log(JSON.stringify({ ok, count, fsSlowWrites: s1.fsSlowWrites - s0.fsSlowWrites, fsSlowWriteMs: s1.fsSlowWriteMs - s0.fsSlowWriteMs,
    totalS: times.reduce((a, b) => a + b, 0) }));
  for (let i = 0; i < count; i++) await http(`filedelete/local/rictest${i}.bin`).catch(() => null);
  await c.disconnect(); process.exit(ok === count ? 0 : 1);
})().catch(e => { console.log('ERROR', e); process.exit(2); });
