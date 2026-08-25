'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const {randomUUID} = require('node:crypto');

const {
    createRedisClient,
    connectRedis,
    closeRedis
} = require('../src/redis_client');
const {createLoginRateLimiter} = require('../src/login_rate_limiter');

const redisUrl = process.env.CHATHUB_REDIS_TEST_URL;
if (typeof redisUrl !== 'string' || redisUrl.trim().length === 0) {
    throw new Error('CHATHUB_REDIS_TEST_URL_missing');
}

test('真实 Redis 固定窗口限流合同', async t => {
    const runId = randomUUID().replace(/-/g, '');
    const username = `redis_test_${runId.slice(0, 8)}`;
    const sourceIp = '127.0.0.1';
    const keyPrefix = `chathub:test:${runId}`;
    const userKey = `${keyPrefix}:login-fail:user:${username}`;
    const sourceIpKey = `${keyPrefix}:login-fail:ip:${sourceIp}`;

    const client = createRedisClient({
        url: redisUrl.trim(),
        connectTimeoutMs: 2000,
        maxReconnectAttempts: 0,
        reconnectDelayMs: 0
    });

    t.after(async () => {
        if (client.isOpen && client.isReady) {
            await client.del(userKey, sourceIpKey);
        }
        await closeRedis(client);
    });

    await connectRedis(client);
    const limiter = createLoginRateLimiter({
        client,
        keyPrefix,
        userLimit: 2,
        ipLimit: 3,
        windowSeconds: 30
    });

    const initial = await limiter.inspect({username, sourceIp});
    assert.equal(initial.limited, false);
    assert.equal(initial.username.count, 0);
    assert.equal(initial.username.ttl_seconds, -2);
    assert.equal(initial.source_ip.count, 0);
    assert.equal(initial.source_ip.ttl_seconds, -2);

    const first = await limiter.recordFailure({username, sourceIp});
    assert.equal(first.username.count, 1);
    assert.equal(first.username.expiry_set, 1);
    assert.ok(first.username.ttl_seconds > 0);
    assert.equal(first.source_ip.count, 1);
    assert.equal(first.source_ip.expiry_set, 1);
    assert.ok(first.source_ip.ttl_seconds > 0);
    assert.equal(first.limited, false);

    const second = await limiter.recordFailure({username, sourceIp});
    assert.equal(second.username.count, 2);
    assert.equal(second.username.expiry_set, 0);
    assert.ok(second.username.ttl_seconds > 0);
    assert.equal(second.source_ip.count, 2);
    assert.equal(second.source_ip.expiry_set, 0);
    assert.ok(second.source_ip.ttl_seconds > 0);
    assert.equal(second.limited, true);
    assert.ok(second.retry_after_seconds >= 1);

    const inspectedLimited = await limiter.inspect({username, sourceIp});
    assert.equal(inspectedLimited.limited, true);
    assert.equal(inspectedLimited.username.count, 2);

    assert.deepEqual(
        await limiter.clearUserFailures(username),
        {deleted: 1}
    );

    const afterClear = await limiter.inspect({username, sourceIp});
    assert.equal(afterClear.username.count, 0);
    assert.equal(afterClear.username.ttl_seconds, -2);
    assert.equal(afterClear.source_ip.count, 2);
    assert.ok(afterClear.source_ip.ttl_seconds > 0);
});
