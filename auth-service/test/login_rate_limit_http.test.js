'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const {randomUUID} = require('node:crypto');
const {mkdtemp, rm} = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const bcrypt = require('bcryptjs');
const jwt = require('jsonwebtoken');

const {createRedisClient, connectRedis, closeRedis} = require('../src/redis_client');
const {createLoginRateLimiter} = require('../src/login_rate_limiter');
const {createApp} = require('../src/app');
const {createDatabase} = require('../src/db');
const {closeHttpServer} = require('../src/server');

const redisUrl = process.env.CHATHUB_REDIS_TEST_URL;

if (typeof redisUrl !== 'string' || redisUrl.trim().length === 0) {
    throw new Error('CHATHUB_REDIS_TEST_URL_missing');
}

// 功能：让操作系统分配一个空闲端口，并把“开始监听”转换为可 await 的 Promise。
function listenOnLoopback(app) {
    return new Promise((resolve, reject) => {
        let httpServer;

        const onStartupError = error => {
            if (httpServer) {
                httpServer.removeListener('error', onStartupError);
            }
            reject(error);
        };

        httpServer = app.listen(0, '127.0.0.1', () => {
            httpServer.removeListener('error', onStartupError);
            resolve(httpServer);
        });
        httpServer.once('error', onStartupError);
    });
}

// 功能：统一发出 HTTP 请求并读取 JSON 响应，避免测试正文重复处理 response.json()。
async function requestJson(baseUrl, route, options = {}) {
    const response = await fetch(`${baseUrl}${route}`, options);
    return {
        status: response.status,
        body: await response.json()
    };
}

// 功能：在读取 JSON 的同时读取 429 合同中的 Retry-After 响应头。
async function requestJsonWithRetryAfter(baseUrl, route, options = {}) {
    const response = await fetch(`${baseUrl}${route}`, options);
    return {
        status: response.status,
        body: await response.json(),
        retryAfter: response.headers.get('retry-after')
    };
}

// 功能：按依赖关系的反方向清理资源；即使某一步失败，后续资源也必须继续释放。
async function closeTestResources({httpServer, redisClient, redisKeys, db, tempDirectory}) {
    let firstError;

    const rememberError = error => {
        if (!firstError) {
            firstError = error;
        }
    };

    if (httpServer) {
        try {
            await closeHttpServer(httpServer);
        } catch (error) {
            rememberError(error);
        }
    }

    if (redisClient && redisClient.isOpen && redisKeys.length > 0) {
        try {
            await redisClient.del(redisKeys);
        } catch (error) {
            rememberError(error);
        }
    }

    if (redisClient) {
        try {
            await closeRedis(redisClient);
        } catch (error) {
            rememberError(error);
        }
    }

    if (db) {
        try {
            db.close();
        } catch (error) {
            rememberError(error);
        }
    }

    if (tempDirectory) {
        try {
            await rm(tempDirectory, {recursive: true, force: true});
        } catch (error) {
            rememberError(error);
        }
    }

    if (firstError) {
        throw firstError;
    }
}

test('真实 Redis、临时 SQLite 与 HTTP 的登录限流联调', async t => {
    const runId = randomUUID().replace(/-/g, '');
    const username = `http_test_${runId.slice(0, 8)}`;
    const password = 'password123';
    const sourceIp = '127.0.0.1';
    const keyPrefix = `chathub:test:http:${runId}`;
    const secretKey = `test-secret-${runId}`;
    const redisKeys = [
        `${keyPrefix}:login-fail:user:${username}`,
        `${keyPrefix}:login-fail:ip:${sourceIp}`
    ];

    let tempDirectory;
    let db;
    let redisClient;
    let httpServer;
    let compareCalls = 0;

    t.after(async () => {
        await closeTestResources({httpServer, redisClient, redisKeys, db, tempDirectory});
    });

    tempDirectory = await mkdtemp(path.join(os.tmpdir(), 'chathub-auth-http-'));
    db = createDatabase(path.join(tempDirectory, 'auth.db'));

    redisClient = createRedisClient({
        url: redisUrl.trim(),
        connectTimeoutMs: 2000,
        maxReconnectAttempts: 0,
        reconnectDelayMs: 0
    });
    await connectRedis(redisClient);

    const limiter = createLoginRateLimiter({
        client: redisClient,
        keyPrefix,
        userLimit: 3,
        ipLimit: 5,
        windowSeconds: 30
    });
    // 保留真实 bcrypt 行为，同时记录 compare() 是否被登录路由调用。
    const observedBcrypt = {
        hash: (...args) => bcrypt.hash(...args),
        compare: async (...args) => {
            compareCalls += 1;
            return bcrypt.compare(...args);
        }
    };
    const app = createApp({
        db,
        limiter,
        bcrypt: observedBcrypt,
        jwt,
        secretKey
    });
    httpServer = await listenOnLoopback(app);
    const port = httpServer.address().port;
    const baseUrl = `http://127.0.0.1:${port}`;

    assert.ok(Number.isInteger(port) && port > 0);

    const registration = await requestJson(baseUrl, '/register', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({username, password})
    });
    assert.deepEqual(registration, {
        status: 201,
        body: {message: '注册成功'}
    });

    const duplicateRegistration = await requestJson(baseUrl, '/register', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({username, password})
    });
    assert.deepEqual(duplicateRegistration, {
        status: 409,
        body: {error: '用户名已存在'}
    });

    const failedLogin = await requestJson(baseUrl, '/login', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({username, password: 'wrong-password'})
    });
    assert.deepEqual(failedLogin, {
        status: 401,
        body: {error: '用户名或密码错误'}
    });

    const stateAfterFailure = await limiter.inspect({username, sourceIp});
    assert.equal(stateAfterFailure.username.count, 1);
    assert.equal(stateAfterFailure.source_ip.count, 1);

    const successfulLogin = await requestJson(baseUrl, '/login', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({username, password})
    });
    assert.equal(successfulLogin.status, 200);
    assert.equal(successfulLogin.body.username, username);
    assert.equal(typeof successfulLogin.body.token, 'string');
    assert.ok(successfulLogin.body.token.length > 0);

    const currentUser = await requestJson(baseUrl, '/me', {
        headers: {Authorization: `Bearer ${successfulLogin.body.token}`}
    });
    assert.deepEqual(currentUser, {
        status: 200,
        body: {username}
    });

    const stateAfterSuccess = await limiter.inspect({username, sourceIp});
    assert.equal(stateAfterSuccess.username.count, 0);
    assert.equal(stateAfterSuccess.username.ttl_seconds, -2);
    assert.equal(stateAfterSuccess.source_ip.count, 1);
    assert.ok(stateAfterSuccess.source_ip.ttl_seconds > 0);

    const firstFailureAfterReset = await requestJson(baseUrl, '/login', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({username, password: 'wrong-password'})
    });
    assert.deepEqual(firstFailureAfterReset, {
        status: 401,
        body: {error: '用户名或密码错误'}
    });

    const secondFailureAfterReset = await requestJson(baseUrl, '/login', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({username, password: 'wrong-password'})
    });
    assert.deepEqual(secondFailureAfterReset, {
        status: 401,
        body: {error: '用户名或密码错误'}
    });

    const compareCallsBeforeThresholdFailure = compareCalls;
    const thresholdFailure = await requestJsonWithRetryAfter(baseUrl, '/login', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({username, password: 'wrong-password'})
    });
    assert.equal(thresholdFailure.status, 429);
    assert.equal(thresholdFailure.body.error, '登录尝试过于频繁，请稍后再试');
    assert.equal(thresholdFailure.body.code, 'login_rate_limited');
    assert.ok(Number.isSafeInteger(thresholdFailure.body.retry_after_seconds));
    assert.ok(thresholdFailure.body.retry_after_seconds > 0);
    assert.equal(
        thresholdFailure.retryAfter,
        String(thresholdFailure.body.retry_after_seconds)
    );
    assert.equal(compareCalls, compareCallsBeforeThresholdFailure + 1);

    const compareCallsBeforeBlockedLogin = compareCalls;
    const blockedCorrectPassword = await requestJsonWithRetryAfter(baseUrl, '/login', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({username, password})
    });
    assert.equal(blockedCorrectPassword.status, 429);
    assert.equal(blockedCorrectPassword.body.error, '登录尝试过于频繁，请稍后再试');
    assert.equal(blockedCorrectPassword.body.code, 'login_rate_limited');
    assert.ok(Number.isSafeInteger(blockedCorrectPassword.body.retry_after_seconds));
    assert.ok(blockedCorrectPassword.body.retry_after_seconds > 0);
    assert.equal(
        blockedCorrectPassword.retryAfter,
        String(blockedCorrectPassword.body.retry_after_seconds)
    );
    assert.equal(compareCalls, compareCallsBeforeBlockedLogin);

    const stateAfterBlockedLogin = await limiter.inspect({username, sourceIp});
    assert.equal(stateAfterBlockedLogin.username.count, 3);
    assert.ok(stateAfterBlockedLogin.username.ttl_seconds > 0);
    assert.equal(stateAfterBlockedLogin.source_ip.count, 4);
    assert.ok(stateAfterBlockedLogin.source_ip.ttl_seconds > 0);
});
