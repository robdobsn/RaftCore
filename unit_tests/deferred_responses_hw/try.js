// Send a REST command to a Raft device over WebSocket (raftjs) and show the response, timing and
// the device's deferred-response counters before/after
// Usage: node try.js <ip> "<command>" [count]
const { RaftConnector, RaftLog } = require('./.raftjs/dist/web/main.js');
RaftLog.setLogLevel(1);
const [ip, cmd, countStr] = process.argv.slice(2);
const count = Number(countStr || 1);
const sysType = { nameForDialogs: 'Try', connectorOptions: { wsSuffix: 'ws', syncTimeOnConnect: false }, setup: () => {},
  subscribeForUpdates: null, stateIsInvalid: () => {}, rxOtherMsgType: () => {} };
const stats = async () => (await (await fetch(`http://${ip}/api/sysmoddebug/RestAPI`)).json()).debug;
(async () => {
  if (!ip || !cmd) { console.log('Usage: node try.js <ip> "<command>" [count]'); process.exit(1); }
  const c = new RaftConnector(async () => sysType);
  await c.initializeChannel('WebSocket');
  if (!await c.connect(ip)) { console.log('connect failed'); process.exit(1); }
  console.log('RestAPI stats before', JSON.stringify(await stats()));
  for (let i = 0; i < count; i++) {
    const t0 = Date.now();
    const r = await c.sendRICRESTMsg(cmd, {}).catch(e => ({ exception: String(e) }));
    console.log(`${Date.now() - t0}ms`, JSON.stringify(r));
  }
  console.log('RestAPI stats after ', JSON.stringify(await stats()));
  await c.disconnect(); process.exit(0);
})();
