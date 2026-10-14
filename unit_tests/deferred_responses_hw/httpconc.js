// Concurrent HTTP requests: node httpconc.js <ip> "<cmd>" <concurrency> <rounds>
const [ip, cmd, concS, roundsS] = process.argv.slice(2);
const conc = Number(concS || 8), rounds = Number(roundsS || 3);
(async () => {
  const t = {};
  for (let r = 0; r < rounds; r++) {
    const rs = await Promise.all([...Array(conc)].map(async () => {
      const t0 = Date.now();
      try { const resp = await fetch(`http://${ip}/api/${cmd}`); const j = await resp.json(); return { k: j.rdData !== undefined ? 'rdData=' + j.rdData : (j.error || j.rslt), ms: Date.now() - t0 }; }
      catch (e) { return { k: 'fetchErr:' + (e.cause && e.cause.code || e.message), ms: Date.now() - t0 }; }
    }));
    for (const x of rs) t[x.k] = (t[x.k] || 0) + 1;
  }
  console.log(JSON.stringify({ conc, rounds, outcomes: t }));
})();
