#!/usr/bin/env node
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device identification and polling bench
//
// Measures, on a Raft device with I2C devices attached, how closely devices are polled at their configured
// intervals, where the I2C worker spends its time, how long devices took to be identified at boot, and the
// sample rate a client actually receives. Results are written as JSON so runs can be compared.
//
// Usage:
//   node device-bench.js --ip <device IP> --raftjs <path to raftjs> [--out results.json] [--label name]
//        [--duration <s per scenario>] [--devices 1_68:50000,1_39:100000] [--reboot]
//
//   --devices  the devices to exercise, as <deviceId>:<default interval us>. Scenarios set each device's
//              interval with devman/devconfig and restore the defaults at the end
//   --reboot   reset the device first, so identification timing is from this boot
//
// Requires Node 22+ (global WebSocket and fetch) and a built raftjs (npm ci && npm run build in raftjs).
// See README.md.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

'use strict';
const path = require('path');
const fs = require('fs');
const Module = require('module');

// ---------------------------------------------------------------------------------------------------------------
// Arguments
// ---------------------------------------------------------------------------------------------------------------

function parseArgs(argv) {
    const args = { out: null, label: 'run', duration: 20, devices: '1_68:50000,1_39:100000', reboot: false };
    for (let i = 2; i < argv.length; i++) {
        const a = argv[i];
        if (a === '--reboot') { args.reboot = true; continue; }
        if (!a.startsWith('--') || (i + 1 >= argv.length)) throw new Error(`bad argument ${a}`);
        args[a.substring(2)] = argv[++i];
    }
    if (!args.ip || !args.raftjs) throw new Error('--ip and --raftjs are required');
    args.duration = Number(args.duration);
    args.deviceList = args.devices.split(',').map(d => {
        const [id, us] = d.split(':');
        return { id, defaultUs: Number(us) };
    });
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

// ---------------------------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------------------------

const sleep = ms => new Promise(r => setTimeout(r, ms));

// HTTP REST request, retried on failure (every request the bench makes can safely be repeated)
async function http(apiPath, timeoutMs = 5000, attempts = 3) {
    for (let attempt = 1; ; attempt++) {
        const ctrl = new AbortController();
        const timer = setTimeout(() => ctrl.abort(), timeoutMs);
        try {
            const r = await fetch(`http://${args.ip}/api/${apiPath}`, { signal: ctrl.signal });
            return await r.json();
        } catch (e) {
            if (attempt >= attempts) throw e;
            console.log(`  retrying ${apiPath} after ${e.name}`);
            await sleep(500);
        } finally {
            clearTimeout(timer);
        }
    }
}

async function waitForDevice(timeoutMs) {
    const end = Date.now() + timeoutMs;
    while (Date.now() < end) {
        try { if ((await http('v', 2000, 1)).rslt === 'ok') return true; } catch (e) { /* not up yet */ }
        await sleep(1000);
    }
    return false;
}

// The first bus that reports poll timing (the I2C bus)
function firstBusWith(busStatus, field) {
    const buses = (busStatus && busStatus.buses) || {};
    for (const [name, b] of Object.entries(buses))
        if (b && b[field] !== undefined) return { name, status: b };
    return null;
}

function summarise(values) {
    if (!values.length) return null;
    const s = [...values].sort((a, b) => a - b);
    const mean = s.reduce((x, y) => x + y, 0) / s.length;
    return { n: s.length, min: s[0], mean: Math.round(mean * 10) / 10, max: s[s.length - 1] };
}

// ---------------------------------------------------------------------------------------------------------------
// Connection with a device manager fed from the devbin stream
// ---------------------------------------------------------------------------------------------------------------

async function connect() {
    const deviceManager = new raftjs.RaftDeviceManager();
    let systemUtils = null;
    const sysType = {
        nameForDialogs: 'Bench',
        connectorOptions: { wsSuffix: 'ws', syncTimeOnConnect: false },
        setup: (su) => { systemUtils = su; deviceManager.setup(su); },
        subscribeForUpdates: null,
        stateIsInvalid: () => { },
        rxOtherMsgType: (payload) => { deviceManager.handleClientMsgBinary(payload); },
        deviceMgrIF: deviceManager,
    };
    const connector = new raftjs.RaftConnector(async () => sysType);
    await connector.initializeChannel('WebSocket');
    if (!await connector.connect(args.ip)) throw new Error('WebSocket connect failed');
    const msgHandler = connector.getRaftSystemUtils().getMsgHandler();
    const subscribe = async (rateHz) => msgHandler.sendRICRESTCmdFrame(
        `{"cmdName":"subscription","action":"update","pubRecs":[{"name":"devbin","trigger":"timeorchange","rateHz":${rateHz},"minMs":10}]}`);
    return { connector, deviceManager, subscribe };
}

// ---------------------------------------------------------------------------------------------------------------
// Scenario
// ---------------------------------------------------------------------------------------------------------------

// Set each device's poll interval (and a sample buffer large enough that publishing doesn't lose samples)
async function setIntervals(intervals) {
    for (const [id, us] of Object.entries(intervals)) {
        const numSamples = Math.max(1, Math.ceil(200000 / us));
        const r = await http(`devman/devconfig?deviceid=${id}&intervalUs=${us}&numSamples=${numSamples}`);
        if (r.rslt !== 'ok') throw new Error(`devconfig ${id} failed: ${JSON.stringify(r)}`);
    }
}

async function runScenario(conn, name, intervals) {
    console.log(`scenario ${name}: ${JSON.stringify(intervals)}`);
    await setIntervals(intervals);
    await sleep(1500);

    // Start: reset poll timing (reported values discarded) and client sample counts
    await http('devman/busstatus?reset=1');
    const deviceKeys = Object.keys(intervals);
    for (const k of deviceKeys) conn.deviceManager.resetDeviceStats(k);
    const startMs = Date.now();

    // Sample the activity window every 2s
    const activity = [];
    while (Date.now() - startMs < args.duration * 1000) {
        await sleep(2000);
        const bus = firstBusWith(await http('devman/busstatus'), 'activity');
        if (bus && bus.status.activity && bus.status.activity.windowMs) activity.push(bus.status.activity);
    }
    const elapsedS = (Date.now() - startMs) / 1000;

    // End: poll timing per device, delivered samples per device
    const bus = firstBusWith(await http('devman/busstatus'), 'pollTiming');
    const pollTiming = bus ? bus.status.pollTiming : {};
    const devices = {};
    for (const id of deviceKeys) {
        const addr = id.split('_')[1];
        const fw = pollTiming[addr] || null;
        const stats = conn.deviceManager.getDeviceStats(id);
        const cfgHz = 1e6 / intervals[id];
        devices[id] = {
            configuredUs: intervals[id],
            configuredHz: Math.round(cfgHz * 100) / 100,
            firmware: fw,
            pollRateErrorPct: fw && fw.meanUs ? Math.round((intervals[id] / fw.meanUs - 1) * 10000) / 100 : null,
            delivered: { samples: stats.totalSamples, rateHz: Math.round(stats.totalSamples / elapsedS * 100) / 100 },
        };
    }
    return {
        name, durationS: elapsedS, devices,
        pollLoadPct: bus && (bus.status.pollLoadPct !== undefined) ? bus.status.pollLoadPct : null,
        activity: {
            windows: activity.length,
            busyPct: summarise(activity.map(a => a.busyPct)),
            passesPerS: summarise(activity.map(a => a.passes * 1000 / a.windowMs)),
            maxPassUs: summarise(activity.map(a => a.maxPassUs)),
            scanProbesPerS: summarise(activity.map(a => a.scanProbes * 1000 / a.windowMs)),
            pollStepsPerS: summarise(activity.map(a => a.pollSteps * 1000 / a.windowMs)),
            usPerS: Object.fromEntries(['handler', 'scan', 'requests', 'muxIO', 'poll'].map(k =>
                [k, summarise(activity.map(a => a.us[k] * 1000 / a.windowMs))])),
        },
    };
}

// ---------------------------------------------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------------------------------------------

(async () => {
    const results = { label: args.label, when: new Date().toISOString(), ip: args.ip, scenarios: [] };
    const defaults = Object.fromEntries(args.deviceList.map(d => [d.id, d.defaultUs]));
    const ids = args.deviceList.map(d => d.id);

    if (args.reboot) {
        console.log('rebooting device');
        try { await http('reset', 2000, 1); } catch (e) { /* the device resets before replying */ }
        await sleep(3000);
        if (!await waitForDevice(60000)) throw new Error('device did not come back after reset');
        await sleep(8000);
    }

    const v = await http('v');
    results.firmware = { version: v.SystemVersion, sysType: v.SystemName };
    results.listdevs = (await http('devman/listdevs')).devices;
    const identBus = firstBusWith(await http('devman/busstatus'), 'identTiming');
    results.identTiming = identBus ? identBus.status.identTiming : null;
    results.mainLoopStart = (await http('sysmoddebug/SysMan')).debug || null;

    const conn = await connect();
    let disconnected = false;
    conn.connector.setEventListener((type, code, name) => { if ((type === 'conn') && (name === 'DISCONNECTED')) disconnected = true; });
    await conn.subscribe(100);
    await sleep(2000);

    const scenarios = [
        ['defaults', defaults],
        ...[100000, 50000, 25000, 10000].map(us => [`all_${us / 1000}ms`, Object.fromEntries(ids.map(id => [id, us]))]),
        [`mixed_10ms_${defaults[ids[ids.length - 1]] / 1000}ms`, Object.fromEntries(ids.map((id, i) => [id, i === 0 ? 10000 : defaults[id]]))],
    ];
    try {
        for (const [name, intervals] of scenarios)
            results.scenarios.push(await runScenario(conn, name, intervals));
    } finally {
        // Restore the default intervals and sample buffers
        for (const d of args.deviceList)
            await http(`devman/devconfig?deviceid=${d.id}&intervalUs=${d.defaultUs}&numSamples=1`).catch(() => { });
        await conn.subscribe(0).catch(() => { });
    }

    results.listdevsEnd = (await http('devman/listdevs')).devices;
    results.mainLoopEnd = (await http('sysmoddebug/SysMan')).debug || null;
    // A reboot or network failure drops the WebSocket connection
    results.health = {
        connectionKept: !disconnected,
        devicesStable: JSON.stringify(results.listdevs) === JSON.stringify(results.listdevsEnd),
    };

    const json = JSON.stringify(results, null, 1);
    if (args.out) fs.writeFileSync(args.out, json);
    console.log(json);
    await conn.connector.disconnect();
    process.exit(0);
})().catch(e => { console.error(`bench failed: ${e.stack || e}`); process.exit(1); });
