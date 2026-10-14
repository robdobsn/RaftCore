// Raft device benchmark: main-loop stats, publish timing and REST latency over WebSocket (raftjs) and HTTP
const { RaftConnector, RaftLog } = require('./.raftjs/dist/web/main.js');
RaftLog.setLogLevel(1);
const ip = process.argv[2]; const label = process.argv[3] || 'run';
const STATS_INTERVAL_S = 5;
const sleep = ms => new Promise(r => setTimeout(r, ms));
let pubTimes = []; let recordPubs = false;
const sysType = { nameForDialogs: 'Bench', connectorOptions: { wsSuffix: 'ws', syncTimeOnConnect: false }, setup: () => {},
  subscribeForUpdates: null, stateIsInvalid: () => {}, rxOtherMsgType: () => { if (recordPubs) pubTimes.push(Date.now()); } };
const pct = (a, p) => { if (!a.length) return null; const s = [...a].sort((x, y) => x - y); return s[Math.min(s.length - 1, Math.floor(p / 100 * s.length))]; };
const summ = a => a.length ? { n: a.length, min: Math.min(...a), p50: pct(a, 50), p95: pct(a, 95), max: Math.max(...a) } : { n: 0 };
async function http(path) { const t0 = Date.now(); const r = await fetch(`http://${ip}/api/${path}`); const j = await r.json(); return { ms: Date.now() - t0, j }; }
async function loopStats() { await sleep(STATS_INTERVAL_S * 1000 + 300); return (await http('sysmoddebug/SysMan')).j.debug; }
function pubGaps() { const g = []; for (let i = 1; i < pubTimes.length; i++) g.push(pubTimes[i] - pubTimes[i - 1]); return summ(g); }
const outcome = r => r ? (r.rdData !== undefined ? 'rdData=' + r.rdData : (r.error || r.rslt)) : 'null(timeout)';
async function wsTimed(c, cmd, timeoutMs) { const t0 = Date.now(); let r = null; try { r = await c.sendRICRESTMsg(cmd, {}, undefined, timeoutMs); } catch (e) { r = { error: 'exception' }; } return { ms: Date.now() - t0, r }; }
function tally(rs) { const t = {}; for (const x of rs) { const k = outcome(x.r || x.j); t[k] = (t[k] || 0) + 1; } return t; }
(async () => {
  const res = { label, when: new Date().toISOString() };
  res.version = (await http('v')).j.SystemVersion;
  await http(`sysman?interval=${STATS_INTERVAL_S}`);
  const c = new RaftConnector(async () => sysType);
  await c.initializeChannel('WebSocket'); if (!await c.connect(ip)) { console.log('connect failed'); process.exit(1); }
  const sub = async rate => c.getRaftSystemUtils().getMsgHandler().sendRICRESTCmdFrame('{"cmdName":"subscription","action":"update","pubRecs":[{"name":"devjson","trigger":"timeorchange","rateHz":' + rate + '}]}');
  await sub(0); await sleep(500);
  // P1 idle
  res.idleLoop = await loopStats();
  // P2 streaming only
  await sub(10); await sleep(500); pubTimes = []; recordPubs = true;
  res.streamLoop = await loopStats(); recordPubs = false; res.streamPubGapsMs = pubGaps();
  // P3 streaming + 100 sequential cmdraw reads (MPU6050 WHO_AM_I)
  pubTimes = []; recordPubs = true; const seq = []; const tP3 = Date.now();
  for (let i = 0; i < 100; i++) seq.push(await wsTimed(c, 'devman/cmdraw?deviceid=1_68&hexWr=75&numToRd=1'));
  res.seqCmdrawLatencyMs = summ(seq.map(x => x.ms)); res.seqCmdrawOutcomes = tally(seq); res.seqDurationMs = Date.now() - tP3;
  res.seqLoop = await loopStats(); recordPubs = false; res.seqPubGapsMs = pubGaps();
  // P4 streaming + concurrent bursts (5 outstanding at once) x 10
  pubTimes = []; recordPubs = true; const conc = [];
  for (let b = 0; b < 10; b++) conc.push(...await Promise.all([0, 1, 2, 3, 4].map(() => wsTimed(c, 'devman/cmdraw?deviceid=1_68&hexWr=75&numToRd=1'))));
  res.concCmdrawLatencyMs = summ(conc.map(x => x.ms)); res.concCmdrawOutcomes = tally(conc);
  res.concLoop = await loopStats(); recordPubs = false; res.concPubGapsMs = pubGaps();
  await sub(0);
  // P5 error paths
  const nack = []; for (let i = 0; i < 10; i++) nack.push(await wsTimed(c, 'devman/cmdraw?deviceid=1_50&hexWr=00&numToRd=1'));
  res.nackLatencyMs = summ(nack.map(x => x.ms)); res.nackOutcomes = tally(nack);
  const slot = []; for (let i = 0; i < 10; i++) slot.push(await wsTimed(c, 'devman/cmdraw?deviceid=1_168&hexWr=75&numToRd=1'));
  res.slotLatencyMs = summ(slot.map(x => x.ms)); res.slotOutcomes = tally(slot);
  const wo = []; for (let i = 0; i < 10; i++) wo.push(await wsTimed(c, 'devman/cmdraw?deviceid=1_50&hexWr=00'));
  res.writeOnlyAbsentOutcomes = tally(wo);
  // P6 HTTP
  const hc = []; for (let i = 0; i < 50; i++) hc.push(await http('devman/cmdraw?deviceid=1_68&hexWr=75&numToRd=1'));
  res.httpCmdrawLatencyMs = summ(hc.map(x => x.ms)); res.httpCmdrawOutcomes = tally(hc);
  const hl = []; for (let i = 0; i < 50; i++) hl.push(await http('devman/listdevs'));
  res.httpListdevsLatencyMs = summ(hl.map(x => x.ms));
  const wl = []; for (let i = 0; i < 50; i++) wl.push(await wsTimed(c, 'devman/listdevs'));
  const vv = []; for (let i = 0; i < 50; i++) vv.push(await wsTimed(c, 'v'));
  res.wsVLatencyMs = summ(vv.map(x => x.ms));
  const hv = []; for (let i = 0; i < 50; i++) hv.push(await http('v'));
  res.httpVLatencyMs = summ(hv.map(x => x.ms));
  res.wsListdevsLatencyMs = summ(wl.map(x => x.ms));
  await http('sysman?interval=10');
  console.log(JSON.stringify(res, null, 1));
  await c.disconnect(); process.exit(0);
})();
