// Integration tests for deferred REST responses (cmdraw) over WebSocket/raftjs and HTTP
const { RaftConnector, RaftLog } = require('./.raftjs/dist/web/main.js');
RaftLog.setLogLevel(1);
const ip = process.argv[2]; const only = process.argv[3];
const sleep = ms => new Promise(r => setTimeout(r, ms));
const sysType = () => ({ nameForDialogs: 'T', connectorOptions: { wsSuffix: 'ws', syncTimeOnConnect: false }, setup: () => {},
  subscribeForUpdates: null, stateIsInvalid: () => {}, rxOtherMsgType: () => {} });
async function http(path) { const r = await fetch(`http://${ip}/api/${path}`); return r.json(); }
async function stats() { return (await http('sysmoddebug/RestAPI')).debug; }
const outcome = r => r ? (r.rdData !== undefined ? 'rdData=' + r.rdData : (r.error || r.rslt)) : 'null(clientTimeout)';
const tally = rs => rs.reduce((t, r) => { const k = outcome(r); t[k] = (t[k] || 0) + 1; return t; }, {});
const READ = 'devman/cmdraw?deviceid=1_68&hexWr=75&numToRd=1';
let fails = 0; const results = [];
function check(name, cond, detail) { results.push({ name, pass: !!cond, detail }); if (!cond) fails++; console.log(`${cond ? 'PASS' : 'FAIL'} ${name} ${JSON.stringify(detail)}`); }
async function connect() { const c = new RaftConnector(async () => sysType()); await c.initializeChannel('WebSocket'); if (!await c.connect(ip)) throw new Error('connect'); return c; }
(async () => {
  const c = await connect();
  const send = (cmd, tmo) => c.sendRICRESTMsg(cmd, {}, undefined, tmo).catch(() => null);
  const s0 = await stats();
  const run = n => !only || only.split(',').includes(n);

  if (run('T1')) { // basic deferred reads
    const before = await stats(); const rs = []; const t0 = Date.now();
    for (let i = 0; i < 20; i++) rs.push(await send(READ));
    const after = await stats();
    check('T1 20 sequential deferred reads all rdData=68', tally(rs)['rdData=68'] === 20, { tally: tally(rs), ms: Date.now() - t0 });
    check('T1 registry counted 20 deferred+completed, none pending', after.def - before.def === 20 && after.cmpl - before.cmpl === 20 && after.pend === 0, { before, after });
  }
  if (run('T2')) { // error paths complete promptly with bus error
    const nack = []; const slot = []; let t0 = Date.now();
    for (let i = 0; i < 10; i++) nack.push(await send('devman/cmdraw?deviceid=1_50&hexWr=00&numToRd=1'));
    const nackMs = Date.now() - t0; t0 = Date.now();
    for (let i = 0; i < 10; i++) slot.push(await send('devman/cmdraw?deviceid=1_168&hexWr=75&numToRd=1'));
    check('T2 NACK reads return BUS_ACK_ERROR (no timeouts)', tally(nack)['BUS_ACK_ERROR'] === 10, { tally: tally(nack), totalMs: nackMs });
    check('T2 bad-slot reads return a bus error (no timeouts)', !tally(slot)['timeout'] && !tally(slot)['readTimeout'] && !tally(slot)['null(clientTimeout)'], { tally: tally(slot), totalMs: Date.now() - t0 });
  }
  if (run('T3')) { // concurrency overlaps
    const t0 = Date.now(); const rs = await Promise.all([...Array(10)].map(() => send(READ))); const ms = Date.now() - t0;
    const st = await stats();
    check('T3 10 concurrent reads all succeed', tally(rs)['rdData=68'] === 10, { tally: tally(rs), wallMs: ms, maxPend: st.maxPend });
  }
  if (run('T4')) { // capacity: more than MAX_PENDING (16) concurrent
    const before = await stats(); const rs = await Promise.all([...Array(24)].map(() => send(READ))); const after = await stats();
    check('T4 24 concurrent reads all answered correctly', tally(rs)['rdData=68'] === 24, { tally: tally(rs) });
    check('T4 capacity enforced (maxPend<=16)', after.maxPend <= 16, { maxPend: after.maxPend, fullRejects: after.full - before.full });
  }
  if (run('T5')) { // forced timeouts with a 1ms limit; late completions ignored
    const before = await stats(); const rs = []; for (let i = 0; i < 10; i++) rs.push(await send(READ + '&timeoutMs=1'));
    await sleep(200); const after = await stats();
    const t = tally(rs);
    check('T5 every request answered (timeout or data), none lost', (t['timeout'] || 0) + (t['rdData=68'] || 0) === 10, { tally: t });
    check('T5 timeouts counted and late completions rejected', (after.tout - before.tout) === (t['timeout'] || 0) && (after.unk - before.unk) === (t['timeout'] || 0) && after.pend === 0,
      { tout: after.tout - before.tout, unk: after.unk - before.unk, pend: after.pend });
  }
  if (run('T6')) { // duplicate (resent) request with same msgNum while pending -> one response
    const mh = c.getRaftSystemUtils().getMsgHandler(); const ch = c.getRaftChannel();
    const seen = []; const orig = mh._handleResponseMessages.bind(mh);
    mh._handleResponseMessages = (restStr, rxMsgNum) => { seen.push({ rxMsgNum, restStr }); orig(restStr, rxMsgNum); };
    const before = await stats();
    for (const msgNum of [201, 202, 203]) {
      const cmd = new TextEncoder().encode(READ); const payload = new Uint8Array(cmd.length + 2); payload[0] = 0; payload.set(cmd, 1); payload[payload.length - 1] = 0;
      const frame = new Uint8Array(payload.length + 2); frame[0] = msgNum; frame[1] = (0 << 6) | 2; frame.set(payload, 2);
      const enc = mh._miniHDLC.encode(frame);
      await ch.sendTxMsg(enc, false); await ch.sendTxMsg(enc, false); // back-to-back duplicate
    }
    await sleep(500); mh._handleResponseMessages = orig;
    const counts = [201, 202, 203].map(n => seen.filter(s => s.rxMsgNum === n).length);
    const after = await stats();
    check('T6 duplicate request answered exactly once per msgNum', counts.every(n => n === 1), { counts, deferred: after.def - before.def, sample: seen[0] && seen[0].restStr });
  }
  if (run('T7')) { // client disconnects while response pending
    const c2 = await connect(); c2.sendRICRESTMsg(READ + '&timeoutMs=500', {}).catch(() => null); await c2.disconnect();
    await sleep(800); const st = await stats(); const v = await http('v');
    check('T7 device healthy after client vanished with pending response', v.rslt === 'ok' && st.pend === 0, { pend: st.pend, ver: v.SystemVersion });
  }
  if (run('T8')) { // unaffected commands
    const wo = await send('devman/cmdraw?deviceid=1_68&hexWr=6b01'); const ld = await send('devman/listdevs'); const vv = await send('v');
    check('T8 write-only cmdraw immediate ok, listdevs and v unaffected', wo && wo.rslt === 'ok' && ld && Array.isArray(ld.devices) && vv && vv.rslt === 'ok', { wo, devs: ld && ld.devices });
  }
  if (run('T9')) { // HTTP reads are deferred
    const before = await stats(); const rs = []; for (let i = 0; i < 10; i++) rs.push(await http(READ)); const after = await stats();
    check('T9 HTTP cmdraw reads deferred and answered', tally(rs)['rdData=68'] === 10 && after.def - before.def === 10 && after.cmpl - before.cmpl === 10, { tally: tally(rs), def: after.def - before.def });
  }
  if (run('T11')) { // HTTP error paths
    const nack = []; for (let i = 0; i < 5; i++) nack.push(await http('devman/cmdraw?deviceid=1_50&hexWr=00&numToRd=1'));
    check('T11 HTTP NACK reads return a bus error (no timeouts)', nack.every(r => r && typeof r.error === 'string' && r.error.startsWith('BUS_')), { tally: tally(nack) });
  }
  if (run('T12')) { // HTTP deferred timeout returns JSON timeout with HTTP 200
    const before = await stats(); const r = await fetch(`http://${ip}/api/${READ}&timeoutMs=1`); const j = await r.json(); await sleep(100); const after = await stats();
    check('T12 HTTP deferred timeout -> 200 + {"rslt":"fail","error":"timeout"}', r.status === 200 && j.rslt === 'fail' && j.error === 'timeout' && after.tout - before.tout === 1, { status: r.status, j });
  }
  if (run('T13')) { // concurrent HTTP requests (several deferred responders at once)
    const before = await stats(); const rs = await Promise.all([...Array(6)].map(() => http(READ))); const after = await stats();
    check('T13 6 concurrent HTTP reads all answered', tally(rs)['rdData=68'] === 6, { tally: tally(rs), maxPend: after.maxPend, def: after.def - before.def });
  }
  if (run('T14')) { // HTTP client disconnects while its response is pending
    const net = require('net'); const before = await stats();
    for (let i = 0; i < 3; i++) await new Promise(res => { const sk = net.connect(80, ip, () => { sk.write(`GET /api/${READ}&timeoutMs=300 HTTP/1.1\r\nHost: ${ip}\r\n\r\n`); setTimeout(() => { sk.destroy(); res(); }, 2); }); sk.on('error', res); });
    await sleep(1000); const after = await stats(); const v = await http('v'); const ok = await http(READ);
    check('T14 HTTP client gone while pending - device healthy, nothing pending', v.rslt === 'ok' && after.pend === 0 && ok.rdData === '68', { pend: after.pend, def: after.def - before.def, tout: after.tout - before.tout });
  }
  if (run('T15')) { // deferred HTTP response has correct Content-Length and body
    const r = await fetch(`http://${ip}/api/${READ}`); const body = await r.text(); const cl = Number(r.headers.get('content-length'));
    check('T15 HTTP deferred response Content-Length matches body', r.status === 200 && cl === body.length && JSON.parse(body).rdData === '68', { status: r.status, contentLength: cl, bodyLen: body.length, ctype: r.headers.get('content-type') });
  }
  if (run('T10')) { // command-frame (JSON) form of the request is also deferred
    const before = await stats(); const mh = c.getRaftSystemUtils().getMsgHandler();
    const r = await mh.sendRICRESTCmdFrame('{"cmdName":"devman/cmdraw","deviceid":"1_68","hexWr":"75","numToRd":1}');
    const after = await stats();
    check('T10 command-frame cmdraw deferred and answered', r && r.rdData === '68' && after.def - before.def === 1, { r });
  }
  const sEnd = await stats();
  console.log('STATS start', JSON.stringify(s0)); console.log('STATS end  ', JSON.stringify(sEnd));
  console.log(fails === 0 ? 'ALL PASSED' : `${fails} FAILED`);
  await c.disconnect(); process.exit(fails ? 1 : 0);
})().catch(e => { console.log('ERROR', e); process.exit(2); });
