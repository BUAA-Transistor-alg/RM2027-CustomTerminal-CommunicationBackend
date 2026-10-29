/**
 * 交叉验证客户端：用【完全不同的技术栈】实现同一个 V1.0.0 协议（32 条：上行 9 / 下行 23），
 * 连到我们的 C++ 服务器，用来验证 C++ 侧的 broker 与 protobuf 编解码。
 *
 *   MQTT 栈      : mqtt.js            (对比 C++ / Paho)
 *   Protobuf 栈  : protobufjs         (对比 C++ / libprotobuf)
 *
 * 用法:
 *   node tests/node/crosscheck.js --host 127.0.0.1 --port 13400 \
 *        --down tests/scenarios/all_down.json --up tests/scenarios/all_up.json \
 *        --duration 12
 *
 * stdout 输出与 C++ 客户端完全一致的 JSON 行格式，供 check_records.py 比对。
 */
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..', '..');
const NM = path.join(ROOT, '..', 'SharkDataSever', 'node_modules');
const mqtt = require(path.join(NM, 'mqtt'));
const protobuf = require(path.join(NM, 'protobufjs'));

function arg(name, def) {
    const i = process.argv.indexOf('--' + name);
    return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : def;
}
const HOST = arg('host', '127.0.0.1');
const PORT = parseInt(arg('port', '13400'), 10);
const DOWN = arg('down', path.join(ROOT, 'tests', 'scenarios', 'all_down.json'));
const UP = arg('up', path.join(ROOT, 'tests', 'scenarios', 'all_up.json'));
const DURATION = parseInt(arg('duration', '12'), 10) * 1000;

// keepCase: true —— 字段名保持官方 proto 里的 snake_case。
// 这样 fromObject() 能直接吃场景文件里的 snake_case；输出给 check_records.py
// 时再用 toJsonName() 转成 C++ protobuf 的 JSON 名（见下），两边就能逐字段对齐。
const root = new protobuf.Root();
for (const f of ['rm2027_client_up.proto', 'rm2027_client_down.proto']) {
    protobuf.parse(fs.readFileSync(path.join(ROOT, 'proto', f), 'utf8'), root,
                   { keepCase: true });
}
const upPkg = root.lookup('rm_client_up');
const downPkg = root.lookup('rm_client_down');

// C++ protobuf 的 JSON 字段名（ToJsonName）：删掉下划线并把其后的字母/数字大写，
// 例如 outpost_R_health -> outpostRHealth、modular_42mm -> modular42mm。
// 注意 protobufjs 默认的 camelCase 规则不同（outpost_R_health -> outpost_RHealth），
// 所以这里显式按 C++ 的规则转换，才能与 check_records.py 的期望键名一致。
function toJsonName(name) {
    return name.replace(/_([a-zA-Z0-9])/g, (_, c) => c.toUpperCase());
}
function jsonNames(v) {
    if (Array.isArray(v)) return v.map(jsonNames);
    if (v && typeof v === 'object') {
        const o = {};
        for (const [k, val] of Object.entries(v)) o[toJsonName(k)] = jsonNames(val);
        return o;
    }
    return v;
}

function emit(obj) {
    process.stdout.write(JSON.stringify(obj) + '\n');
}
function ts() {
    return new Date().toISOString().replace('Z', '');
}

(async () => {
    const downSteps = JSON.parse(fs.readFileSync(DOWN, 'utf8')).steps;
    const upSteps = JSON.parse(fs.readFileSync(UP, 'utf8')).steps;
    const downTopics = downSteps.map(s => s.topic);

    const client = mqtt.connect(`mqtt://${HOST}:${PORT}`, {
        clientId: 'node-crosscheck', clean: true, protocolVersion: 4,
        reconnectPeriod: 0, connectTimeout: 5000,
    });

    let received = 0, published = 0, unknown = 0, decodeFailed = 0;

    await new Promise((resolve, reject) => {
        client.on('connect', resolve);
        client.on('error', reject);
        setTimeout(() => reject(new Error('连接超时')), 6000);
    });
    emit({ ts: ts(), ev: 'connected', clientId: 'node-crosscheck' });

    // 注意：message 处理器必须在订阅【之前】挂上。
    // mqtt.js 只会把消息派发给已经注册的监听器，订阅完成后再挂 handler，
    // 那个窗口里到达的消息会被直接丢掉（实测会丢掉第一条 GameStatus）。
    client.on('message', (topic, payload) => {
        received++;
        const T = downPkg.lookupType(topic);
        if (!T) { unknown++; emit({ ts: ts(), dir: 'down', topic, size: payload.length, error: 'unknown-message' }); return; }
        let data;
        try {
            data = jsonNames(T.toObject(T.decode(payload),
                                         { longs: String, bytes: String, defaults: false }));
        } catch (e) {
            decodeFailed++;
            emit({ ts: ts(), dir: 'down', topic, size: payload.length, error: 'decode-failed' });
            return;
        }
        emit({ ts: ts(), dir: 'down', topic, size: payload.length, data });
    });

    await new Promise((res, rej) => client.subscribe(downTopics, { qos: 1 }, e => e ? rej(e) : res()));
    emit({ ts: ts(), ev: 'subscribed', ok: downTopics.length, total: downTopics.length });


    // 等服务器先把下行发过来，再发上行（避免抢跑）
    await new Promise(r => setTimeout(r, 400));

    for (const step of upSteps) {
        const T = upPkg.lookupType(step.topic);
        const err = T.verify(step.data);        // 场景文件就是官方 snake_case 字段名
        if (err) { console.error(`❌ ${step.topic} verify: ${err}`); continue; }
        const buf = T.encode(T.fromObject(step.data)).finish();
        await new Promise((res, rej) => client.publish(step.topic, buf, { qos: 1 }, e => e ? rej(e) : res()));
        published++;
        emit({ ts: ts(), dir: 'up', topic: step.topic, size: buf.length,
               data: jsonNames(step.data) });
        await new Promise(r => setTimeout(r, 60));
    }

    await new Promise(r => setTimeout(r, DURATION > 400 ? DURATION - 400 : 1000));

    emit({ ts: ts(), ev: 'summary', received, published, unknownTopic: unknown, decodeFailed });
    client.end(true);
    process.exit(0);
})().catch(e => { console.error('❌', e.message); process.exit(1); });
