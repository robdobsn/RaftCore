#!/usr/bin/env node
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Capture decoded samples from a Raft host, for checking a breakout's data
//
// Connects over WebSocket, subscribes to the devbin stream, decodes it with raftjs (as a dashboard does) and after
// the given time prints, as JSON, each device's attributes with the number of samples and their min, mean, max and
// last value. breakouts.py bench runs it with --samples.
//
// Usage:
//   node capture-samples.js --ip <host IP> --raftjs <path to raftjs> [--duration <s>] [--devices 1_177,1_258]
//        [--series 1]
//
//   --series 1  also gives each attribute's values as [seconds from the start, value] (the time it arrived)
//
// Requires Node 22+ (global WebSocket and fetch) and a built raftjs (npm ci && npm run build in raftjs).
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

'use strict';
const path = require('path');
const Module = require('module');

function parseArgs(argv) {
    const args = { duration: 10, devices: '', series: '' };
    for (let i = 2; i < argv.length; i++) {
        const a = argv[i];
        if (!a.startsWith('--') || (i + 1 >= argv.length)) throw new Error(`bad argument ${a}`);
        args[a.substring(2)] = argv[++i];
    }
    if (!args.ip || !args.raftjs) throw new Error('--ip and --raftjs are required');
    args.duration = Number(args.duration);
    args.deviceList = args.devices ? args.devices.split(',') : [];
    return args;
}

const args = parseArgs(process.argv);

// raftjs selects its BLE channel at bundle time; plain Node needs the web variant
const origResolve = Module._resolveFilename;
Module._resolveFilename = function (request, parent, ...rest) {
    if (request === './RaftChannelBLE' && parent && parent.filename.includes(path.join('dist', 'web')))
        request = './RaftChannelBLE.web';
    return origResolve.call(this, request, parent, ...rest);
};
const raftjs = require(path.resolve(args.raftjs, 'dist', 'web', 'main.js'));
raftjs.RaftLog.setLogLevel(1);

const sleep = ms => new Promise(r => setTimeout(r, ms));

async function main() {
    const deviceManager = new raftjs.RaftDeviceManager();
    const sysType = {
        nameForDialogs: 'Capture',
        connectorOptions: { wsSuffix: 'ws', syncTimeOnConnect: false },
        setup: (su) => { deviceManager.setup(su); },
        subscribeForUpdates: null,
        stateIsInvalid: () => { },
        rxOtherMsgType: (payload) => { deviceManager.handleClientMsgBinary(payload); },
        deviceMgrIF: deviceManager,
    };
    const connector = new raftjs.RaftConnector(async () => sysType);
    await connector.initializeChannel('WebSocket');
    if (!await connector.connect(args.ip)) throw new Error('WebSocket connect failed');

    // Collect each new value as it arrives (an attribute's values[] is a window that is trimmed)
    const collected = {};
    const startMs = Date.now();
    deviceManager.addAttributeDataCallback((deviceKey, attr) => {
        if (args.deviceList.length && !args.deviceList.includes(deviceKey)) return;
        const per = attr.elemsPerSample || 1;
        const n = attr.numNewValues;  // values (elements), not samples
        if (!n) return;
        const dev = collected[deviceKey] = collected[deviceKey] || {};
        const a = dev[attr.name] = dev[attr.name] || { units: attr.units, range: attr.range, elemsPerSample: per, values: [], times: [] };
        for (const v of attr.values.slice(-n)) a.values.push(v);
        const t = Math.round((Date.now() - startMs) / 100) / 10;
        for (let i = 0; i < n; i++) a.times.push(t);
    });

    const msgHandler = connector.getRaftSystemUtils().getMsgHandler();
    await msgHandler.sendRICRESTCmdFrame(
        '{"cmdName":"subscription","action":"update","pubRecs":[{"name":"devbin","trigger":"timeorchange","rateHz":10,"minMs":10}]}');
    await sleep(args.duration * 1000);

    const out = { durationS: args.duration, devices: {} };
    const states = deviceManager.getDevicesState();
    for (const [key, attrs] of Object.entries(collected)) {
        const st = states[key] || {};
        const dev = out.devices[key] = { typeKey: st.deviceTypeKey || '', attributes: {} };
        for (const [name, a] of Object.entries(attrs)) {
            const nums = a.values.filter(v => typeof v === 'number' && Number.isFinite(v));
            const sum = nums.reduce((x, y) => x + y, 0);
            dev.attributes[name] = {
                units: a.units, range: a.range, elemsPerSample: a.elemsPerSample, n: a.values.length / a.elemsPerSample,
                min: nums.length ? nums.reduce((x, y) => Math.min(x, y)) : null,
                mean: nums.length ? Math.round(sum / nums.length * 1000) / 1000 : null,
                max: nums.length ? nums.reduce((x, y) => Math.max(x, y)) : null,
                last: a.values.slice(-a.elemsPerSample),
            };
            if (args.series) dev.attributes[name].series = a.values.map((v, i) => [a.times[i], v]);
        }
    }
    process.stdout.write(JSON.stringify(out, null, 1) + '\n');
    try { await connector.disconnect(); } catch (e) { /* closing anyway */ }
    process.exit(0);
}

main().catch(e => { console.error(e.message || e); process.exit(1); });
