// Measures main-loop / CommsMan stall while REST requests are handled (1s stats windows sampled continuously)
const { RaftConnector, RaftLog } = require('./.raftjs/dist/web/main.js');
RaftLog.setLogLevel(1);
const ip = process.argv[2]; const label = process.argv[3];
const sleep = ms => new Promise(r => setTimeout(r, ms));
const sysType = { nameForDialogs: 'S', connectorOptions: { wsSuffix: 'ws', syncTimeOnConnect: false }, setup: () => {}, subscribeForUpdates: null, stateIsInvalid: () => {}, rxOtherMsgType: () => {} };
const http = async p => (await fetch(`http://${ip}/api/${p}`)).json();
async function phase(c, cmd, n, viaHttp) {
  let stop = false; const samples = [];
  const sampler = (async () => { await sleep(1100); while (!stop) { const d = (await http('sysmoddebug/SysMan')).debug; samples.push(d); await sleep(1000); } })();
  for (let i = 0; i < n; i++) { if (viaHttp) await http(cmd).catch(() => null); else await c.sendRICRESTMsg(cmd, {}).catch(() => null); }
  stop = true; await sampler;
  const mod = viaHttp ? 'WebServer' : 'CommsMan'; const comms = samples.map(s => (s.slowUs && s.slowUs[mod]) || 0).filter(x => x > 0);
  const loopMax = samples.map(s => s.maxUs || 0); const loopAvg = samples.map(s => s.avgUs || 0);
  const pct = (a, p) => { if (!a.length) return null; const s = [...a].sort((x, y) => x - y); return s[Math.min(s.length - 1, Math.floor(p / 100 * s.length))]; };
  return { module: mod, windows: samples.length, commsManInTop2: comms.length, commsManMaxUs: comms.length ? Math.max(...comms) : null, commsManP50Us: pct(comms, 50),
           loopMaxUs: { p50: pct(loopMax, 50), max: Math.max(...loopMax) }, loopAvgUs: Math.round(loopAvg.reduce((a, b) => a + b, 0) / loopAvg.length) };
}
(async () => {
  await http('sysman?interval=1');
  const c = new RaftConnector(async () => sysType); await c.initializeChannel('WebSocket'); if (!await c.connect(ip)) process.exit(1);
  const res = { label };
  res.control_v = await phase(c, 'v', 200);
  res.cmdraw = await phase(c, 'devman/cmdraw?deviceid=1_68&hexWr=75&numToRd=1', 200);
  res.cmdrawNack = await phase(c, 'devman/cmdraw?deviceid=1_50&hexWr=00&numToRd=1', 100);
  res.http_v = await phase(c, 'v', 150, true);
  res.http_cmdraw = await phase(c, 'devman/cmdraw?deviceid=1_68&hexWr=75&numToRd=1', 150, true);
  await http('sysman?interval=10');
  console.log(JSON.stringify(res)); await c.disconnect(); process.exit(0);
})();
