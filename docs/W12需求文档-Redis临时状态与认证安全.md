# W12 需求文档：Redis 临时状态、登录限流与认证安全边界

> 文档状态：待用户确认
> 实施状态：W12-1 已通过；W12-2 的 Redis client 边界、`login_rate_limiter.js`、`app.js` 登录调用链和 `server.js` 启动编排均已实现、验证并完成掌握确认；临时 SQLite + 真实 Redis + HTTP 最小联调已实现、通过新鲜验证并完成掌握确认。W12-1 已完成 WSL Redis CLI 的连通与 TTL 首轮人工实验、Windows Node.js `PING`、String/TTL、100 并发原子计数、`MULTI/EXEC + EXPIRE ... NX` 固定窗口变式、Redis key 类型错误、非整数 String 的 `INCR` 值格式错误与受控连接失败
> 当前状态：**W12-2 Redis 并发失败计数精确性（R12-2-11）已实现、验证并完成掌握确认，等待用户说“下一步”**
> 当前授权：`app.js` 的依赖校验、`/register`、`/me` 和真实 Redis 合同测试已按用户授权代写并完成掌握确认；本轮在既有真实 Redis 合同测试基础上补充 R12-2-11 并发失败计数测试，仍不提交 Git。
> 教学约束：用户尚未系统学习 Redis。每个实现能力点必须先讲概念和 API，再由用户优先编写；只有用户明确要求“帮我实现/修改/修复”时才代写生产代码。直接代写后必须继续追问调用顺序、失败路径和关键参数，用户回答通过后才能记录为“已掌握”，代码完成不等于学习完成。

---

## 0. 本文的独立判断

### 0.1 结论

W12 不应从“把 Redis 接进 ChatHub”开始，而应按以下顺序推进：

```text
W12-1 独立 Redis 最小实验
  String / TTL / INCR / MULTI-EXEC / 连接失败
                     ↓ 通过后才进入
W12-2 Auth Service 失败登录限流
  用户维度 + IP 维度 / 固定窗口 / 429 / Redis 故障 503
                     ↓ 通过后才进入
W12-3 JWT jti 与撤销架构门禁
  先解决 Auth Service 与 ChatServer 如何共享撤销判断
                     ↓ 方案确认后才可能进入
W12-4 对应实现、集成验收、文档与交付
```

理由：

1. Redis 在 ChatHub V1 中有明确价值的职责是保存**会自动过期的认证临时状态**，不是替代 SQLite/MySQL；
2. 当前真正消费登录限流的是 Node.js Auth Service，因此先用 Node.js 官方推荐客户端学习，避免同时引入 C++ Redis 客户端、CMake 和线程模型；
3. JWT 由 Auth Service 签发、由 Auth Service 和 ChatServer 分别验证。只让 Auth Service 查询撤销状态会形成“`/me` 拒绝、ChatServer 仍接受”的安全假象，因此撤销必须先完成跨进程设计；
4. 当前机器在 WSL2 Ubuntu 中已安装并运行 Redis，Windows Node 探针经 `127.0.0.1:6379` 可访问它；Docker 仍未安装，且 Docker 编排不属于 W12；
5. W11 交接仅用于恢复当前架构和 Git 边界，不作为 W12 技术选型的决定依据。

### 0.2 W12 的真实完成口径

| 状态 | 可以宣称 | 不得宣称 |
|---|---|---|
| 仅本文完成 | W12 需求已形成，等待确认 | 已学习 Redis、已接入 Redis |
| W12-1 通过 | 已完成独立 Redis 最小实验 | Auth Service 已具备限流 |
| W12-2 通过 | Auth Service 已具备 Redis 登录限流及故障边界 | JWT 已可撤销 |
| W12-3 仅设计通过 | `jti`、TTL、消费者和故障策略已有确认设计 | 撤销链路已实现 |
| W12-3/4 端到端通过 | 所有 token 消费者都拒绝已撤销 token | 仅凭 `/me` 拒绝就声称完整撤销 |

如果用户决定暂缓跨进程撤销，必须记录为“**W12 登录限流已完成，JWT 撤销未实现**”，不能把路线中的“受控撤销”标成完成。

---

## 1. 任务合同

### 1.1 目标与用户价值

W12 解决两个问题：

1. **学习问题**：用户能够从零解释 Redis 的键值模型、TTL、原子计数、事务边界和连接失败，不只是运行别人给出的命令；
2. **产品问题**：Auth Service 对短时间内的重复失败登录进行可解释、可恢复的限制，降低暴力猜测和密码喷洒风险，同时不把 Redis 变成用户或聊天数据的真相源。

### 1.2 W12 分阶段交付物

| 阶段 | 唯一能力点 | 交付物 | 当前状态 |
|---|---|---|---|
| W12-1 | Redis 最小实验 | 独立 Node.js spike、可重复测试、实验说明 | 已通过：正常、到期、并发、固定窗口、错误与连接失败证据齐全 |
| W12-2 | 失败登录限流 | Auth Service Redis 客户端边界、限流模块、HTTP 行为与测试 | 进行中：Redis client、限流模块、`app.js`、`server.js` 和真实 HTTP/Redis 基础/阈值/用户隔离/IP/TTL/未知账号/非法输入/并发测试已验证并完成对应掌握 |
| W12-3 | `jti` 与撤销设计 | 跨 Auth Service / ChatServer 的书面方案、合同、失败语义 | 未开始 |
| W12-4 | 验收与交付 | 新鲜验证、README/需求/交接同步、范围审查 | 未开始 |

### 1.3 完整 W12 范围

- 学习 Redis `String`、键命名、`SET` / `GET`、TTL、`INCR` 和 `MULTI` / `EXEC`；
- 使用 Node.js `redis`（node-redis）客户端验证连接、命令、关闭和失败处理；
- Auth Service 仅对**登录失败**建立用户与来源 IP 两个固定窗口计数；
- 定义 400、401、429、503 的 HTTP 行为和稳定错误语义；
- 定义 Redis 启动失败、运行期断开、恢复与人工重启边界；
- 定义 JWT `jti`、`exp` 与撤销记录 TTL 的数据合同；
- 在所有 token 消费者共享撤销判断之前，不声称撤销完成；
- 为已授权实现补充可重复自动化或集成验收，并记录学习证据。

### 1.4 明确不做

- 不把聊天正文、用户密码、密码哈希、JWT 正文、refresh token 或 SQLite/MySQL 业务真相保存到 Redis；
- 不用 Redis 替换 `users.sqlite`、`IMessageRepository`、SQLite 或 MySQL；
- 不把当前单机在线用户、`PendingDeliveryMap` 或离线消息迁移到 Redis；
- W12-1 不修改 `auth-service/`、`chat-server/`、`client-qt/` 或根 CMake；
- 不在 W12-1 引入 Lua、分布式锁、Pub/Sub、Stream、Cluster、Sentinel、持久化调优或缓存淘汰策略；
- 不在 W12 安装 Docker；MySQL/Redis 的 Docker Compose 编排仍属于 W13；
- 不信任任意 `X-Forwarded-For`；未配置受信任反向代理时只使用直接连接来源；
- 不通过 `FLUSHDB` / `FLUSHALL` 清理测试数据；
- 不因“Redis 很快”而新增没有用户故事支持的缓存；
- 不在撤销架构确认前给 ChatServer 增加 Redis/C++ 客户端、HTTP introspection 或新协议帧。

### 1.5 权限边界与实施门禁

| 用户动作 | 允许执行 |
|---|---|
| 当前“生成 W12 需求文档” | 只创建本文并做文档级验证 |
| 用户确认本文 | 只关闭 W12 需求门禁，不等于安装或编码授权 |
| 用户说“下一步” | 只进入 W12-1 的概念讲解、环境确认和最小实验任务书 |
| 用户自行提交 W12-1 实现并说“检查” | 只审查 W12-1 验收范围 |
| 用户再次说“下一步” | W12-1 有证据通过后，才进入 W12-2 |
| 用户明确说“帮我实现/修改/修复” | 只代写当次明确授权的单一能力点 |
| 用户明确说“提交并推送” | 通过范围、验证和秘密扫描后，才执行指定 Git 操作 |

---

## 2. Redis 零基础最小知识包

### 2.1 Redis 在本项目中解决什么问题

SQLite/MySQL 保存“即使服务重启也必须保留”的事实，例如用户和聊天消息。W12 的 Redis 保存“到期后应该自动消失”的临时状态，例如一分钟内某个账号失败了几次。

```text
永久事实                              TTL 临时状态
users.sqlite / SQLite / MySQL          Redis
-----------------------------          ---------------------------
用户、密码哈希、聊天消息                 登录失败次数、撤销标记
不能因缓存丢失而消失                    到期自动删除
需要 Schema/事务/迁移                   通过 key + value + TTL 建模
```

Redis 不是“只能做缓存的数据库”。它是提供多种数据结构和原子命令的内存数据服务；W12 只学习最小的 `String` 路径。

### 2.2 五个必须先理解的概念

| 概念 | 本步含义 | 常见误解 |
|---|---|---|
| key | Redis 全局键空间中的名字，如 `chathub:probe:<run-id>:counter` | 把不同测试都写进同一个固定 key |
| String | Redis 的字节序列值；数字计数也是以 String 形式保存 | 认为它等同于 JavaScript `String` 类 |
| TTL | key 剩余生存时间；到期后 key 自动删除 | 认为 `SET` 覆盖后会自动保留旧 TTL |
| 原子命令 | 单条 `INCR` 不会被其他客户端拆开，因此并发加一不丢计数 | 认为两条独立命令天然也是一个原子操作 |
| 真相源 | Redis 丢失后，用户和聊天事实仍在 SQLite/MySQL；只丢临时保护状态 | 把 Redis 当作所有数据的更快替代品 |

### 2.3 最小调用顺序

```text
创建 client
  → 先注册 error 监听器
  → connect()
  → PING / SET / GET / TTL / INCR
  → 在 finally 中关闭连接
```

### 2.4 不超过 30 行的最小 CommonJS 示例

> 这是 API 认识示例，不是 W12-1 的完整答案，也不能直接复制进生产 Auth Service。

```js
const {createClient} = require('redis');

async function main() {
    const redisUrl = process.env.CHATHUB_REDIS_TEST_URL;
    if (!redisUrl) throw new Error('missing_redis_test_url');
    const client = createClient({
        url: redisUrl,
        socket: {connectTimeout: 2000, reconnectStrategy: false},
        disableOfflineQueue: true
    });
    client.on('error', err => console.error('redis_error', err.code));

    try {
        await client.connect();
        const key = `chathub:probe:${process.pid}:counter`;
        await client.set(key, '0', {EX: 10});
        console.log(await client.incr(key));
        console.log(await client.ttl(key));
        await client.del(key);
    } finally {
        if (client.isOpen) client.destroy();
    }
}

main().catch(err => {
    console.error('probe_failed', err.code ?? err.name);
    process.exitCode = 1;
});
```

### 2.5 本次 API 清单

| API / 命令 | 输入 | 成功输出 | 失败与边界 | 常见误用 |
|---|---|---|---|---|
| `createClient(options)` | URL、socket、离线队列策略 | 未连接 client | URL/选项错误可能在连接时拒绝 | 创建后忘记 `connect()` |
| `client.on('error', fn)` | 错误监听函数 | 无 | node-redis 没有 `error` 监听器时，错误事件可能导致进程退出 | 只用外层 `try/catch`，不监听事件 |
| `await client.connect()` | 无 | 已就绪连接 | 拒绝连接、超时、认证失败 | 默认重连导致失败测试一直等待 |
| `PING` / `client.ping()` | 可选消息 | `PONG` | 连接未就绪时拒绝 | 用“进程存在”代替真实 PING |
| `SET key value {EX}` | key、值、秒级 TTL | `OK` | 覆盖同名 key；默认覆盖旧 TTL | 认为覆盖 value 后 TTL 不变 |
| `GET key` | key | String 或 `null` | key 不存在返回 `null` | 把 `null` 当连接失败 |
| `TTL key` | key | 正数、`-1` 或 `-2` | `-1` 表示无 TTL，`-2` 表示 key 不存在 | 只断言恰好等于设定秒数 |
| `INCR key` | 保存整数文本的 key | 增加后的整数 | 不存在时从 0 开始；非整数值报错 | `GET` 后在 JS 中 `+1` 再 `SET`，产生并发丢失 |
| `EXPIRE key seconds NX` | key、秒、只在无 TTL 时设置 | 是否设置成功 | key 不存在则失败 | 每次失败都重置 TTL，形成滑动锁死 |
| `multi()...exec()` | 一组排队命令 | 按顺序返回结果数组 | 保证命令连续执行，但不是关系数据库式自动回滚 | 把 pipeline 当 transaction，或忽略每个结果 |
| `DEL key` | 一个或多个 key | 删除数量 | 不存在返回 0 | 使用 `FLUSHDB` 清理共享测试库 |
| `client.destroy()` | 无 | 连接关闭 | 未正确释放会让 Node 进程不退出 | 成功路径关闭，失败路径泄漏 |

### 2.6 本步必须能解释的边界

1. `INCR` 单条命令是原子的，但独立发送 `INCR` 和 `EXPIRE` 中间仍可能断线；
2. W12 固定窗口必须让 TTL 从第一次失败开始，后续失败不能把窗口不断延长；
3. TTL 到期时间是服务端状态，测试应在有限截止时间内轮询，不依赖某一毫秒恰好删除；
4. Redis 命令失败和 `GET` 返回 `null` 是不同语义；
5. pipeline 主要减少往返，`MULTI` / `EXEC` 才提供事务内不被其他客户端插入命令的保证；
6. Redis 事务不等于 MySQL 事务：某条已执行命令不会因为后续命令运行时报错而自动回滚；
7. 临时状态可丢失不代表故障可以伪装为“允许登录”。

---

## 3. 环境与依赖基线

### 3.1 当前机器事实（2026-08-24 只读检查）

| 项目 | 当前事实 |
|---|---|
| Windows Node.js | `v24.15.0` |
| npm | `11.12.1` |
| WSL | WSL2，Ubuntu 26.04 |
| Windows `redis-server` / `redis-cli` | 未发现 |
| WSL `redis-server` / `redis-cli` | 未发现 |
| Docker | 未发现 |

这些事实只决定 W12-1 的起点，不等于授权安装软件。

### 3.2 W12-1 运行环境决策

- Redis Server 优先运行在现有 WSL2 Ubuntu 中；这是当前机器上改动最小、符合 Redis 官方 Windows 指引的路径；
- Node.js spike 运行在 Windows，连接显式的 `CHATHUB_REDIS_TEST_URL`；
- 安装后必须记录 Redis Server、`redis-cli` 和 npm `redis` 包的精确版本；
- W12 只依赖基础命令，要求 Redis 支持 `EXPIRE ... NX`；不依赖 Redis 8.8 才新增的 `INCREX`；
- npm `redis` 依赖必须锁定到 `package-lock.json`，不能在需求文档中永久写死“latest”；
- 端口只允许本机开发访问，不把 6379 暴露到公网或局域网；
- W13 再决定 Docker Compose，不在 W12 为“以后会用”提前安装 Docker。

### 3.3 W12 配置合同

| 配置 | 使用阶段 | 规则 |
|---|---|---|
| `CHATHUB_REDIS_TEST_URL` | W12-1 和真实 Redis 测试 | 必须指向专用测试实例/逻辑库；值不得写进 Git 或日志 |
| `CHATHUB_REDIS_URL` | W12-2 Auth Service | 生产接入时显式提供；可以包含凭据，因此只记录变量名 |
| `CHATHUB_LOGIN_USER_LIMIT` | W12-2 | 默认 5，正整数；实现时定义并测试上限 |
| `CHATHUB_LOGIN_IP_LIMIT` | W12-2 | 默认 20，且不得小于 user limit |
| `CHATHUB_LOGIN_WINDOW_SECONDS` | W12-2 | 默认 60，正整数；固定窗口从第一次失败开始 |
| `CHATHUB_REDIS_KEY_PREFIX` | 测试/隔离 | 测试必须用每次运行唯一前缀；生产默认 `chathub:auth:v1` |

配置解析失败必须在监听 HTTP 端口前非零退出；不得悄悄改用内存计数器或无 Redis 模式。

---

## 4. W12-1：Redis 最小实验需求

### 4.1 能力点目标

用户能够独立完成并解释四条数据流：

1. 写入 String → 读回；
2. 写入 TTL → 观察剩余时间 → 到期消失；
3. 多个并发请求执行 `INCR` → 最终计数精确；
4. 连接不可用 → 在有限时间内失败、退出非零、不伪造成功。

### 4.2 实验与生产隔离

W12-1 建议使用独立目录，不接入 ChatHub 生产目录：

```text
D:\CppLearn\spikes\chathub-redis-basics-node\
├── package.json
├── package-lock.json
├── README.md
├── src\redis_probe.js
└── test\redis_probe.test.js
```

该 spike 是学习/可行性证据，不等于 Auth Service 已集成。是否提交到独立仓库由用户另行决定；不得混入 ChatHub 项目提交。

### 4.3 建议函数与合同

| 文件 | 函数 | 输入 | 输出 / 失败 |
|---|---|---|---|
| `src/redis_probe.js` | `createProbeClient(redisUrl)` | 非空 Redis URL | 返回已配置但未连接的 client；URL 无效则抛错 |
| 同上 | `runStringTtlScenario(client, keyPrefix)` | ready client、唯一前缀 | 返回写入值和 TTL 观察结果；命令失败则 reject |
| 同上 | `runAtomicCounterScenario(client, keyPrefix, concurrency)` | 正整数并发数 | 返回最终计数；必须精确等于并发数 |
| 同上 | `runFixedWindowScenario(client, keyPrefix, windowSeconds)` | 窗口秒数 | 返回两次 TTL；第二次不得重置为完整窗口 |
| 同上 | `destroyClient(client)` | client | 成功/失败路径都终止 socket |
| `test/redis_probe.test.js` | 各 `node:test` case | `CHATHUB_REDIS_TEST_URL` | 0 表示通过；缺配置必须明确 skip，不得假装通过 |

### 4.4 key 约定

```text
chathub:probe:<run-id>:string
chathub:probe:<run-id>:ttl
chathub:probe:<run-id>:counter
chathub:probe:<run-id>:window
```

- `<run-id>` 每次运行唯一，例如 `crypto.randomUUID()`；
- 测试只删除自己记录过的完整 key；
- 禁止扫描后删除不属于自己的 key；
- value 不得包含用户名、密码、token、真实聊天正文或本机秘密。

### 4.5 固定实现顺序

1. 先验证 WSL Redis `PING → PONG`；
2. 再从 Windows Node.js 建立连接并 `PING`；
3. 完成 `SET` / `GET` / `TTL` 到期实验；
4. 用 `Promise.all()` 并发调用 `INCR`，验证最终值；
5. 用 `MULTI` / `EXEC` 将 `INCR` 与 `EXPIRE ... NX` 放在一个事务中，验证后续调用不延长固定窗口；
6. 对非整数 String 执行 `INCR`，验证命令拒绝且旧值不变；
7. 指向确定未监听的本机端口，验证连接失败；
8. 在 `finally` 中关闭连接并精确清理本次 key；
9. 用户口述数据流、失败路径和 Redis/SQLite/MySQL 的职责差异；
10. 通过后才记录 W12-1 学习证据并等待“下一步”。

### 4.6 W12-1 验收矩阵

| ID | 场景 | 可观察结果 |
|---|---|---|
| R12-1-01 | Redis CLI 健康 | `PING` 返回 `PONG`，记录服务端版本 |
| R12-1-02 | Node 健康 | `client.ping()` 返回 `PONG` |
| R12-1-03 | String | `SET` 后 `GET` 精确等于原值 |
| R12-1-04 | TTL 正常 | 写入后 TTL 为正且不大于窗口 |
| R12-1-05 | TTL 到期 | 在有限截止时间内 `GET` 返回 `null`、`TTL` 返回 `-2` |
| R12-1-06 | 无 TTL 反例 | 故意创建无 TTL key，`TTL` 返回 `-1`；随后精确删除 |
| R12-1-07 | 原子计数 | 至少 100 个并发 `INCR` 后最终值恰好为 100 |
| R12-1-08 | 固定窗口 | 第二次计数不会把 TTL 重置为完整窗口 |
| R12-1-09 | 类型错误 | 对非整数 String 执行 `INCR` 明确失败，旧值未被伪造为计数 |
| R12-1-10 | 连接拒绝 | 2 秒级有界失败、退出码非 0、无无限重连、无未处理 `error` 事件 |
| R12-1-11 | 清理 | 只删除本次 `<run-id>` keys，测试库其他 key 不变 |
| R12-1-12 | 自解释 | 用户能解释 `null` / `-1` / `-2`、单命令原子性和两命令竞态 |

### 4.7 破坏式变式

完成正常实验后，只做一个变式：把固定窗口实现故意改成“每次 `INCR` 后都执行无条件 `EXPIRE`”。连续发送失败并观察 TTL 被反复拉回完整窗口，再说明为什么这会让正常用户更难恢复。

### 4.8 W12-1 完成门禁

必须同时具备：

- 源码/测试可重复运行；
- 正常、到期、并发、类型错误、连接失败五类证据；
- 唯一 Redis 学习笔记已记录真实观察和陷阱；
- 用户能用自己的话解释数据流；
- ChatHub 生产目录未发生 Redis 相关改动。

仅安装成功、看到 `PONG` 或复制运行示例，都不能判定 W12-1 掌握。

---

## 5. W12-2：Auth Service 失败登录限流需求

> 本节是 W12-1 通过后的后续蓝图，不是当前编码授权。

### 5.1 用户故事

作为 ChatHub 用户，我希望短时间内针对账号或来源地址的连续错误密码尝试会被暂时限制；窗口到期后可以恢复，正常登录不会因为其他账号的少量失败而被误伤。

### 5.2 固定窗口策略

| 维度 | 默认阈值 | 窗口 | 作用 |
|---|---:|---:|---|
| username | 5 次失败 | 60 秒 | 防止针对单一账号的暴力猜测 |
| source IP | 20 次失败 | 60 秒 | 限制同一来源对大量账号的密码喷洒 |

行为定义：

1. 输入格式非法时返回 400，不计入失败登录；
2. 请求进入密码校验前，先检查两个维度是否已达到阈值；
3. 未达到阈值时才查询用户并校验 bcrypt；
4. 用户不存在与密码错误使用同一 401 文案，并都计入两个维度；
5. 某一维度本次达到阈值时，本次失败返回 429；后续请求在窗口到期前也返回 429，不再执行 bcrypt；
6. 正确密码在阈值达到前登录成功，并清除该 username 的失败计数；不得清除来源 IP 的累计失败；
7. username 与 IP 同时受限时，`retry_after_seconds` 取仍受限维度中较大的剩余 TTL；
8. TTL 从该 key 第一次失败开始，后续失败不得延长窗口；
9. 不做永久账号锁定、不做指数锁定、不做 CAPTCHA；这些属于后续安全增强。

### 5.3 身份与 key 设计

W12 直接使用当前已经受格式约束的 username 和可信来源 IP 组成 key，不额外引入哈希规则：

```text
chathub:auth:v1:login-fail:user:<username>
chathub:auth:v1:login-fail:ip:<source-ip>
```

不变量：

- username 保持当前 3–20 位 ASCII、大小写敏感语义；
- 未配置受信任反向代理时，不读取客户端可伪造的 `X-Forwarded-For`；
- source IP 必须来自 Express/底层连接给出的规范字符串，并设置合理长度上限；冒号等字符只是 key 内容，不能用字符串拼接 Redis 命令；
- value 只保存十进制失败次数；
- key 必须始终有 TTL；发现 `TTL == -1` 视为数据不变量损坏，不得无限保留；
- Redis 中不得出现密码、密码哈希、JWT、Authorization 头或完整请求正文。

### 5.4 HTTP 合同

| 场景 | HTTP | JSON 最小字段 | 说明 |
|---|---:|---|---|
| 输入非法 | 400 | `{ "error": "..." }` | 保持现有输入校验合同，不计数 |
| 凭据错误且未达阈值 | 401 | `{ "error": "用户名或密码错误" }` | 不区分用户不存在/密码错误 |
| 达到或已处于限制 | 429 | `{ "error": "登录尝试过于频繁，请稍后再试", "code": "login_rate_limited", "retry_after_seconds": N }` | 同时设置 HTTP `Retry-After: N` |
| Redis 无法完成安全判断 | 503 | `{ "error": "认证服务暂时不可用", "code": "authentication_dependency_unavailable" }` | 不签发 token，不降级为内存计数或无限制登录 |
| 登录成功 | 200 | 保持现有 `message`、`username`、`token` | username 失败计数已清除 |

Qt Client 当前会显示响应中的 `error`，所以 429/503 不要求修改网络协议；是否单独显示倒计时属于 UI 增强，不纳入 W12-2。

### 5.5 Redis 故障策略

限流是认证安全依赖，不是普通页面缓存：

- **启动时 Redis 不可用**：Auth Service 在 `app.listen()` 前失败并非零退出；
- **运行期断开**：相关登录请求返回 503，不进行“默认允许”；
- **离线队列**：关闭，避免请求在 Redis 恢复前无限悬挂；
- **重连**：W12 V1 使用有界、可测试策略；超过边界后要求恢复 Redis 并人工重启 Auth Service；
- **恢复**：Redis 与 Auth Service 恢复后，新的登录请求重新按 TTL 状态处理；
- **日志**：只记录组件、阶段、事件和稳定 code，不记录 Redis URL、完整 key、username、IP、密码或 token。

### 5.6 建议文件与函数

| 文件 | 建议职责 | 建议入口 |
|---|---|---|
| `auth-service/src/redis_client.js` | 创建、配置、连接和关闭唯一 Redis client | `createRedisClient(config)`、`connectRedis(client)` |
| `auth-service/src/login_rate_limiter.js` | key 推导、检查、原子记失败、成功清理 | `createLoginRateLimiter(options)` |
| 同上 | 查询两个维度的当前限制 | `inspect({username, sourceIp})` |
| 同上 | 在一个 `MULTI/EXEC` 中更新两个计数与首个 TTL | `recordFailure({username, sourceIp})` |
| 同上 | 正确登录后只清 user key | `clearUserFailures(username)` |
| `auth-service/src/app.js` | 创建 Express app，并通过参数接收 db、limiter、JWT 依赖 | `createApp(dependencies)` |
| `auth-service/src/server.js` | 解析配置、先连接依赖、再监听、统一关闭 | `main()` |
| `auth-service/src/db.js` | 保持 SQLite 用户事实；为测试提供显式路径/工厂 | `createDatabase(path)` |
| `auth-service/test/login_rate_limiter.test.js` | 真实 Redis 的模块合同 | `node:test` cases |
| `auth-service/test/login_rate_limit_http.test.js` | 临时 SQLite + 真实 Redis + 临时 HTTP 端口 | 端到端 HTTP cases |
| `auth-service/package.json` / lock | 锁定 `redis` 依赖并增加测试脚本 | `npm test` |

`login_rate_limiter.js` 不得依赖 Express 的 `req` / `res`，只接收已验证的业务输入；Express 路由只负责调用顺序和 HTTP 映射。

### 5.7 原子更新合同

每次失败至少要在同一个 Redis `MULTI/EXEC` 中完成：

```text
INCR user-key
EXPIRE user-key window-seconds NX
TTL user-key
INCR ip-key
EXPIRE ip-key window-seconds NX
TTL ip-key
```

要求：

- 两个 `INCR` 的返回值决定是否达到阈值；
- `EXPIRE ... NX` 只为尚无 TTL 的 key 设置窗口；
- 每个返回值都必须校验，不能只判断 `EXEC` 是否返回数组；
- `TTL == -1` 或命令错误必须映射为依赖失败/数据不变量失败，不能放行；`TTL == -2` 表示检查期间已到期，应按不存在重读/处理；受限 key 的 `TTL == 0` 时对外 `Retry-After` 取 1 秒；
- W12 不使用新版本专属 `INCREX`，避免把学习和生产合同绑定到非必要的新命令；
- 如果真实客户端 API 对 `EXPIRE NX` 的签名不同，实施时以锁定版本官方 API 为准，并在唯一笔记记录，不得猜参数。

### 5.8 登录调用顺序

```text
POST /login
  → 校验 username/password 结构
      失败：400，不计数
  → 获取受信任 source IP
  → limiter.inspect()
      已受限：429
      Redis 失败：503
  → SQLite 查询 + bcrypt.compare
      凭据错误：limiter.recordFailure()
          未达阈值：401
          达到阈值：429
          Redis 失败：503
      凭据正确：limiter.clearUserFailures()
          Redis 失败：503，不签 token
          成功：签发 JWT → 200
```

### 5.9 W12-2 验收矩阵

| ID | 场景 | 可观察结果 |
|---|---|---|
| R12-2-01 | 1–4 次错误密码 | 均为通用 401，未泄露账号是否存在 |
| R12-2-02 | 第 5 次错误密码 | 返回 429、稳定 code、正数 `Retry-After` |
| R12-2-03 | 限制期间正确密码 | 仍返回 429，不执行登录成功路径 |
| R12-2-04 | TTL 到期 | 正确密码恢复 200；旧计数 key 已消失 |
| R12-2-05 | 成功重置 user | 阈值前成功登录后，同 user 新失败从 1 开始 |
| R12-2-06 | 不重置 IP | 某账号成功不能清除同 IP 对其他账号产生的失败累计 |
| R12-2-07 | 用户隔离 | user A 达到账号阈值时，user B 在未达 IP 阈值下仍可正常登录 |
| R12-2-08 | IP 限制 | 同 IP 跨多个用户名达到 IP 阈值后统一 429 |
| R12-2-09 | 不存在账号 | 与错误密码相同 401/429 外部语义，且 key 有 TTL |
| R12-2-10 | 非法输入 | 400 且两个计数都不变 |
| R12-2-11 | 并发失败 | 并发结果后 Redis 计数精确、TTL 未被延长、无永久 key |
| R12-2-12 | 启动依赖失败 | Redis 不可达时 Auth Service 非零退出且 3000 端口未监听 |
| R12-2-13 | 运行期断开 | 登录返回 503，不签发 token，不无限等待 |
| R12-2-14 | 恢复 | 恢复 Redis 并按已定义方式重启后可重新登录 |
| R12-2-15 | 注册与 `/me` 回归 | 未授权改变的现有 HTTP 行为保持 |
| R12-2-16 | 秘密与隐私 | 日志/测试/Git 不含密码、token、Authorization、Redis URL 值或真实身份 key |

### 5.10 W12-2 停止条件

出现以下任一情况必须停止并回到设计：

- 需要信任反向代理头但没有可信代理拓扑；
- node-redis 锁定版本不支持本文所需命令合同；
- 为测试必须操作真实 `auto.db` 或共享 Redis key；
- 429/503 需要改变 Qt Client 公共响应合同；
- Redis 失败策略被要求改为 fail-open；
- 一次实现同时开始 JWT 撤销、ChatServer Redis 接入或 Docker Compose。

---

## 6. W12-3：JWT `jti` 与受控撤销设计门禁

### 6.1 新概念

- `exp`：JWT 到期时间；到期或之后不能再接受；
- `jti`：一个 JWT 的唯一标识，必须具有足够低的碰撞概率；
- 撤销记录：以 `jti` 为身份的短期拒绝标记，其 TTL 只需覆盖 token 剩余有效期；
- token 消费者：当前至少包括 Auth Service `/me` 和 ChatServer TCP `auth`；二者必须采用一致判断，才能称为撤销。

### 6.2 候选 Redis 合同（仅设计）

```text
key   = chathub:auth:v1:revoked:jti:<jti>
value = "1"
ttl   = max(1, exp - current_unix_seconds)
write = SET key 1 EX ttl NX
read  = EXISTS key
```

不变量：

- `jti` 每次签发唯一，不复用；
- 撤销 TTL 不得超过 token 原 `exp`，也不得创建无 TTL 记录；
- token 已过期时不再写撤销 key；
- Redis key/value 不保存完整 JWT；
- 签名、`exp`、必要 claim 校验必须先完成，再信任 `jti`；
- Redis 查询失败不能伪装成“token 未撤销”。

### 6.3 当前架构冲突

当前 Auth Service 和 ChatServer 都直接验证 JWT，但只有 Auth Service 是 Node.js：

```text
Auth Service /me  ── JWT verify ──┐
                                  ├── 必须得到同一个 revoked(jti) 结论
ChatServer auth   ── JWT verify ──┘
```

只实现 `POST /logout` + Auth Service Redis 查询是不完整的：旧 token 仍可直接连接 ChatServer。

### 6.4 方案比较

| 方案 | 优点 | 代价/风险 | W12 判断 |
|---|---|---|---|
| A. ChatServer 直接查询 Redis | 两个消费者共享同一拒绝集合；一次 TCP 认证只查一次 | 新增 C++ Redis 客户端、CMake/运行配置、同步/异步和故障语义 | 不在 W12-1/2 偷渡；需独立设计 |
| B. ChatServer 调 Auth introspection HTTP | token 策略集中在 Auth Service；C++ 不接触 Redis | ChatServer 新增 HTTP 客户端、Auth 可用性依赖、超时与错误映射 | 当前无现成边界；需独立设计 |
| C. 只让 Auth Service 检查 | 实现最少 | ChatServer 可绕过撤销；安全语义不一致 | 拒绝作为“完整撤销” |
| D. 只依赖短期 `exp`，客户端丢弃 token | 无共享状态和新依赖 | 不是真正即时撤销，泄露 token 在到期前仍有效 | 可作为明确延期策略，不能记完成 |

### 6.5 本文判断与待确认门禁

W12 默认只完成 `jti`/TTL/消费者/故障语义的书面设计，不直接实施撤销。进入生产实现前，用户必须从 A、B、D 中确认一种路线；C 不可选。

如果选择 A 或 B，还必须先补充：

- 启动顺序与配置；
- 认证查询超时；
- Redis/Auth 不可用时 ChatServer 的稳定 wire error；
- 已连接 Session 是否立即踢下线，还是只拒绝新的认证；
- Qt Client 的 logout 行为；
- 旧的无 `jti` token 的兼容/失效策略；
- Auth Service `/me` 与 ChatServer 的共享合同测试；
- 恢复、重启、时钟偏差和日志边界。

在这些问题关闭前，不修改 `jwt.sign()`、`jwt.verify()`、ChatServer JWT 解析或聊天协议。

### 6.6 撤销闭环最低验收（仅在后续获授权时适用）

1. 新 token 含唯一 `jti` 和有效 `exp`；
2. logout 前，`/me` 与新的 ChatServer TCP 认证都成功；
3. logout 后，同一 token 的 `/me` 与**新的** TCP 认证都拒绝；
4. 撤销记录 TTL 不大于 token 剩余寿命，到期后自动删除；
5. 重启 Auth Service/ChatServer 后，撤销仍在 TTL 内生效；
6. Redis/Introspection 故障时不把 token 当作未撤销；
7. 旧 token 兼容策略有明确测试；
8. 已建立的 TCP Session 是否断开与设计一致，不得默认为已解决。

---

## 7. 测试组织与验证命令合同

### 7.1 W12-1 独立实验

实施后应提供等价命令：

```powershell
$env:CHATHUB_REDIS_TEST_URL = '<仅当前终端的专用测试 URL>'
npm ci
npm test
```

- 命令从 `D:\CppLearn\spikes\chathub-redis-basics-node` 运行；
- URL 的值不得出现在命令日志、截图、需求文档或 Git；
- 缺少 URL 时真实 Redis 测试应明确 `SKIP` 或非零失败，由任务合同提前固定，不能输出假 PASS；
- 连接失败测试使用确定未监听的独立本机端口，不停止用户其他服务。

### 7.2 W12-2 Auth Service

实施后至少提供：

```powershell
node --check src/server.js
node --check src/app.js
node --check src/redis_client.js
node --check src/login_rate_limiter.js
npm test
```

真实 Redis 集成测试必须：

- 使用专用 `CHATHUB_REDIS_TEST_URL`；
- 使用每次运行唯一 prefix；
- SQLite 使用临时路径，不读写开发 `auto.db`；
- HTTP 监听临时端口，不占用用户运行中的 3000；
- 测试结束先关闭 HTTP/Redis/SQLite 句柄，再清理临时文件；
- 不使用 sleep 猜测 TTL，使用有截止时间的轮询；
- 不把 `SKIP` 计为真实 Redis 已通过。

### 7.3 回归边界

- W12-1 不运行 ChatHub CMake/CTest 来证明 Redis；两者没有覆盖关系；
- W12-2 若未改 C++/Qt，Node 自动化与双账户人工 HTTP 验收是主要证据；
- 如果实际改动触及 Qt HTTP 展示、ChatServer JWT 或公共协议，则必须升级范围并增加对应新鲜构建/CTest/人工验收，不能沿用旧结果；
- 完整 W12 交付前必须重新检查当前 Git diff，不能借用 W11 的历史构建和测试作为 W12 证据。

---

## 8. 安全、隐私与可观测性

### 8.1 Redis 网络边界

- Redis 只允许可信本机开发客户端访问；
- 不关闭 protected mode 来换取“能连上”；
- 不把 6379 直接暴露到互联网或局域网；
- 如 URL 含用户名/密码，只从环境变量读取；
- CLI 密码不得作为可见命令行参数或进入 PowerShell 历史；
- W13 部署化时再设计 ACL、容器网络和健康检查。

### 8.2 日志合同

允许：

```text
component=redis phase=startup event=connect_failed code=redis_unavailable
component=auth phase=login event=rate_limited dimension=user
```

禁止：

- Redis URL 或密码；
- 完整 Redis key、username、IP；
- password、bcrypt hash、JWT、Authorization；
- 把 node-redis 原始错误对象全部 JSON 序列化到生产日志；
- 将登录响应的正文或凭据写进测试失败输出。

### 8.3 依赖失败不变量

- 不能把 Redis 错误当成 `GET == null`；
- 不能在 Redis 故障时退回进程内 `Map`，否则重启/多实例语义会变化；
- 不能返回 401 冒充密码错误；依赖故障使用 503；
- 不能因限流状态临时丢失而删除/修改用户数据库；
- 不能让自动重连或离线队列使 HTTP 请求无限等待。

---

## 9. 分步任务清单与授权门禁

### W12-0：需求确认

- [x] 读取工程协作总章程和学习教练提示词；
- [x] 核对 W11 交接、当前路线、Auth Service 和机器环境；
- [x] 形成本文；
- [ ] 用户确认 W12 范围、完成口径和撤销门禁。

完成本文不自动进入 W12-1。

### W12-1：Redis 最小实验

- [x] 先讲 Redis 进程、key/String、TTL、`INCR`、`MULTI/EXEC` 和连接失败；
- [x] 用户确认/执行 WSL Redis 安装与本机安全边界；
- [x] 创建独立 Node spike，不改 ChatHub 生产代码；
- [x] 用户实现 String/TTL 场景；
- [x] 用户实现并发原子计数和固定窗口变式；
- [x] 用户实现受控连接失败与清理；
- [x] 用户实现非整数 String 的 `INCR` 值格式错误，确认旧值不变；
- [x] 教练只按 R12-1-01～12 审查；
- [x] 对已完成的 String/TTL、原子计数、固定窗口、类型错误、非整数 `INCR` 和连接失败完成新鲜运行、更新唯一笔记和能力证据；
- [x] 等待“下一步”并进入 W12-2。

### W12-2：Auth Service 登录限流

- [x] 先讲固定窗口、用户/IP 维度、429、fail-closed 和依赖注入；
- [x] 先确认 HTTP/Redis/key/故障合同；
- [x] 用户实现 `redis_client.js` 的配置校验、client 创建、有限重连、`connect()` 与 `PING`；
- [x] 用户实现 `closeRedis()` 正常关闭与失败兜底；
- [x] 用户实现 `login_rate_limiter.js` 的 key 推导、`inspect()` 查询与稳定错误码；
- [x] 用户完成 `inspect()` 的 username/source IP 类型边界、IPv4/IPv6 校验和 `ipLimit >= userLimit` 配置边界；
- [x] 用户实现 `recordFailure()` 的双维度原子 `INCR`、首次 `EXPIRE ... NX`、TTL 和阈值结果；
- [x] 用户实现 `clearUserFailures()` 只删除 username key，并验证 IP key 保留；
- [x] 完成 `app.js` 的依赖注入、输入校验、`inspect()` → bcrypt → `recordFailure()`/`clearUserFailures()` → JWT 调用顺序和 HTTP 映射；
- [x] 代写并验证 `app.js` 的 `/register` 依赖校验、密码哈希、唯一冲突和成功响应；
- [x] 用户能独立解释 `/register` 的 hash、SQLite `run()`、409/500 边界并完成迁移变式；
- [x] 代写并验证 `/me` 的 Bearer/JWT 过期与无效 token 行为；
- [x] 用户能独立解释 `/me` 的授权头、`jwt.verify()` 返回/异常、401 映射和无状态边界；
- [x] 用户能独立解释 `app.js` 的依赖注入、可信 source IP、输入/限流/凭据分支、错误映射、`Retry-After` 和 JWT 签发前置条件；
- [x] 完成 `server.js` 的依赖解析、启动前连接和统一关闭；
- [x] 代写并验证真实 Redis `login_rate_limiter.js` 合同测试；
- [x] 用户能独立解释真实 Redis 测试的 URL 门禁、唯一 prefix、断言和清理顺序；
- [x] 代写并验证临时 SQLite + 真实 Redis + HTTP 的注册/登录/`/me` 集成测试；
- [x] 用户能独立解释数据库工厂、临时端口、完整请求链和反向清理顺序；
- [x] 只修本能力点问题；
- [x] 新鲜验证、笔记/路线同步；
- [x] 当前能力点代码完成并通过新鲜验证；
- [x] 用户完成真实 Redis/SQLite HTTP 集成的掌握确认；
- [x] 讲清真实 HTTP 阈值、`Retry-After` 与“观察型 bcrypt 包装器”的合同；
- [x] 代写并验证 R12-2-01～03：连续失败 401/429、受限正确密码 429、bcrypt 不再调用；
- [x] 用户能独立解释阈值次与已受限次的调用差异、真实 bcrypt 包装器与 Redis 最终状态；
- [ ] 讲清 username key 所有权、共享 source IP key 与 user A/user B 的真实 HTTP 数据流；
- [x] 代写并验证 R12-2-07：user A 达到 username 阈值时，user B 在同 IP 未达阈值下仍可 200 登录，且 A 仍受限；
- [x] 用户能独立解释 A/B username key、共享 IP key、成功清理边界和再次 429 证据；
- [ ] 讲清共享 source IP key 达到阈值后的跨用户名短路数据流；
- [x] 代写并验证 R12-2-08：同 IP 使 IP count 达到 5 后，新 username 即使无失败历史也返回 429，且不调用 bcrypt；
- [x] 用户能独立解释 B 失败如何触发共享 IP 阈值、C 为何在 username 空时仍 429；
- [x] 代写并验证 R12-2-04：username/IP key TTL 到期后正确密码恢复 200，并重新进入 bcrypt/JWT 成功路径；
- [x] 用户能独立解释截止时间轮询、`TTL -2`、两个 key 到期和恢复调用顺序；
- [x] 代写并验证 R12-2-09：未知账号与已注册账号错误密码均返回 401，未知账号仍建立 username/IP 失败 key；
- [x] 用户能独立解释未知账号不泄露存在性、bcrypt 路径差异和两个维度计数；
- [x] 代写并验证 R12-2-10：缺字段、类型/长度/字符集错误和 malformed JSON 均返回 400，SQLite/bcrypt/两个失败 key 均不发生变化；
- [x] 用户能独立解释输入校验、依赖调用顺序和三类“未发生”证据；
- [x] 代写并验证 R12-2-11：20 个并发 `recordFailure()` 计数精确为 1～20，两个维度各只首次设置 TTL，延迟失败不重置窗口，最终两个 key 均过期；
- [x] 用户能独立解释 Redis 原子计数、`EXPIRE ... NX` 和有界过期轮询的证据关系；

### W12-3：JWT 撤销设计

- [ ] 先讲 `exp`、`jti`、撤销 marker 与 token 消费者；
- [ ] 确认 A/B/D 路线；
- [ ] 写明协议、API、超时、失败和旧 token 兼容；
- [ ] 用户确认书面设计；
- [ ] 未确认前不编码。

### W12-4：实现、验收与交付

- [ ] 只实施已确认的撤销范围；
- [ ] 真实跨进程端到端验证；
- [ ] 更新 README、需求、协议/架构（如实际改变）、交接；
- [ ] 范围审查、秘密扫描和新鲜验证；
- [ ] 只有用户明确授权后才提交/推送。

---

## 10. 文档、学习笔记与 Git

| 位置 | 何时更新 | Git 边界 |
|---|---|---|
| 本文 | 每个需求/设计/验收事实变化时 | 当前只作本地需求资料；是否提交由用户另行授权 |
| `docs/学习笔记/` 中唯一 Redis 笔记 | W12-1 出现真实实验、解释或踩坑证据后 | 仅本地维护，不暂存、不提交 |
| `docs/项目规划/C++Qt学习路线-完善版.md` | 一个能力点有新鲜证据后 | 学习资料，仅本地维护 |
| `docs/项目规划/C++_Qt学习路线-技能清单.md` | 有“能改、能讲、能迁移”证据时 | 学习资料，仅本地维护 |
| 根 `README.md` | W12-2 生产运行合同真实实现并验收后 | 项目交付文档，可随对应范围提交 |
| 协议/架构文档 | JWT/ChatServer 合同实际改变时 | 项目交付文档；不得提前写成已实现 |
| 最新 W12 交接 | 阶段结束或跨会话恢复时 | 按用户授权处理 |

Git 规则：

- 只显式暂存当次已授权范围，禁止 `git add .`；
- 不纳入 `.vs/`、`node_modules/`、构建目录、Redis dump/AOF、SQLite 测试库、`.env`、日志、密码、JWT 或学习笔记；
- W12-1 独立 spike 不混入 ChatHub 项目提交；
- 当前工作区已有大量与 W12 无关改动，任何后续 Git 操作前必须重新列出并隔离；
- 提交前运行工作区和 staged diff 检查，并扫描秘密与生成物；
- 没有提交/推送授权时，只报告候选文件，不改 index。

---

## 11. 需求追踪矩阵

| 需求 | 设计位置 | 最低验收 |
|---|---|---|
| Redis 只存 TTL 临时状态 | 1.3、1.4、2.1 | R12-1-03～06、数据扫描 |
| 原子计数不丢更新 | 2.5、4.5 | R12-1-07 |
| 固定窗口不延长 | 2.6、5.7 | R12-1-08、R12-2-11 |
| 连接失败明确 | 3.3、5.5 | R12-1-10、R12-2-12/13 |
| 用户/IP 登录限流 | 5.2、5.8 | R12-2-01～08 |
| 正常用户不过度误伤 | 5.2 | R12-2-04～07 |
| Redis 故障不绕过安全 | 5.5、8.3 | R12-2-12～14 |
| 密钥/隐私不泄漏 | 5.3、8 | R12-2-16、范围扫描 |
| JWT 撤销不做半闭环 | 6.3～6.5 | 两个消费者共同拒绝 |
| 学习达到可解释和可迁移 | 4.6～4.8、9 | 自解释 + 破坏式变式 + 独立实现 |

---

## 12. 参考依据

本文以当前项目代码和路线为项目事实，并用以下一手资料校准 Redis/JWT/API 边界：

- [Redis：Windows 上通过 WSL 安装](https://redis.io/docs/latest/operate/oss_and_stack/install/archive/install-redis/install-redis-on-windows/)
- [Redis：Node.js 连接与 `createClient()`](https://redis.io/docs/latest/develop/clients/nodejs/connect/)
- [node-redis 官方仓库：CommonJS、错误监听与连接关闭](https://github.com/redis/node-redis)
- [Redis：`SET` 命令与 TTL 选项](https://redis.io/docs/latest/commands/set/)
- [Redis：`EXPIRE` 与 `NX`/`XX` 边界](https://redis.io/docs/latest/commands/expire/)
- [Redis：`INCR` 与计数器竞态](https://redis.io/docs/latest/commands/incr/)
- [Redis：Node.js pipeline 与 transaction](https://redis.io/docs/latest/develop/clients/nodejs/transpipe/)
- [Redis：Node.js 错误处理](https://redis.io/docs/latest/develop/clients/nodejs/error-handling/)
- [Redis：网络与 protected mode 安全边界](https://redis.io/docs/latest/operate/oss_and_stack/management/security/)
- [OWASP Authentication Cheat Sheet：通用错误与 Login Throttling](https://cheatsheetseries.owasp.org/cheatsheets/Authentication_Cheat_Sheet.html)
- [RFC 7519：JWT `exp` 与 `jti`](https://datatracker.ietf.org/doc/rfc7519/)

外部示例只用于校准命令和安全不变量，不替代当前项目需求；实施时必须以锁定版本的官方 API 和本项目测试为准。

---

## 13. 当前结论与下一最小步

### 13.1 W12-1 已记录的人工实验（2026-08-24）

- 环境：WSL Ubuntu 本机 Redis；`redis-server 8.0.5`（`jemalloc-5.3.0`）与 `redis-cli 8.0.5`；
- 服务证据：`systemctl is-active redis-server` 返回 `active`，`redis-cli -h 127.0.0.1 -p 6379 PING` 返回 `PONG`；
- String / TTL：`SET chathub:w12:hello "hello redis" EX 20` 返回 `OK`，`GET` 精确读回值，`TTL` 为 `20`；等待过期后 `GET` 为 `(nil)`、`TTL` 为 `-2`；
- 无 TTL 变式：无 `EX` 的 `SET` 后 `TTL` 为 `-1`，`DEL` 返回 `1`；
- 用户解释：`PONG` 证明客户端与服务端实际通信；`-1` 是 key 存在但没有 TTL，`-2` 是 key 不存在；
- Windows Node.js：独立 spike `D:\CppLearn\spikes\chathub-redis-basics-node` 使用 `redis@6.2.1`；`node --check src\\redis_probe.js` 与显式设置专用 `CHATHUB_REDIS_TEST_URL` 后的 `node src\\redis_probe.js` 均以 `0` 退出，后者输出 `NODE_REDIS_PING=PONG`；
- Windows Node String / TTL 正常路径：`set(key, value, { EX: 3 })` 后读回 `before: 'w12-node-ttl'`、`ttl_before: 3`；等待 4 秒后得到 `after: null`、`ttl_after: -2`，退出码为 `0`；
- Windows Node 无 TTL 变式：去掉 `{ EX: 3 }` 后，独立复验得到 `before: 'w12-node-ttl'`、`ttl_before: -1`，等待 4 秒后仍为 `after: 'w12-node-ttl'`、`ttl_after: -1`，退出码为 `0`；函数只在 `finally` 中对本次 `randomUUID()` key 调用 `DEL`；
- Windows Node 原子计数：对一个独立 `randomUUID()` counter key 以 `Promise.all()` 并发发送 100 次 `INCR`；用户与独立复验均得到 `concurrency: 100`、`minReply: 1`、`maxReply: 100`、`final_count: '100'`，语法检查和运行均以 `0` 退出；
- Windows Node 固定窗口：同一独立 key 的首次 `MULTI → INCR → EXPIRE key 5 NX → EXEC` 得到 `first_count: 1`、`first_expiry_set: 1`、`first_ttl: 5`；等待约 1.1 秒后再次执行，得到 `second_count: 2`、`second_expiry_set: 0`、`second_ttl: 4`，新鲜独立复验退出码为 `0`。这证明后续计数没有重置首次 TTL；
- 固定窗口反例：仅去掉第二次 `EXPIRE` 的 `NX` 后，用户实测 `second_expiry_set: 1`、`second_ttl: 5`。这证明 TTL 被重新设置，会延长窗口；用户已恢复 `NX` 与固定窗口断言；
- Windows Node 类型错误：先以 `SET` 创建独立 String key，`TYPE` 返回 `'string'`；对同 key 执行 `LPUSH` 时，Promise 被拒绝并返回 `WRONGTYPE Operation against a key holding the wrong kind of value`；捕获预期错误后再次 `TYPE` 仍为 `'string'`，新鲜独立复验退出码为 `0`。这证明命令错误不会自动断开已连接 client；
- Windows Node 非整数 `INCR`：独立 String key 先写入 `'not-an-integer'`；`INCR` 被拒绝并返回 `ERR value is not an integer or out of range`，随后 `GET` 仍为 `'not-an-integer'`，新鲜复验退出码为 `0`。将值变式为 `'41'` 后 `INCR` 成功、读取为 `'42'`；验证后已恢复非整数值；
- 运行环境恢复：Windows 到 WSL 的 `127.0.0.1:6379` 端口转发在 WSL 发行版不活跃时曾出现 `ECONNREFUSED`；保持 Ubuntu 终端活跃后 Node 探针恢复。该环境现象不构成 `INCR` 逻辑失败；
- 首次 Node 失败根因：用户将模块导出的 `createClient` 拼为 `creatClient`，调用未定义值产生 `TypeError`；修正两处拼写后成功。此错误发生在 `connect()` 前，不能误判为 Redis 或 WSL 网络故障；
- 用户自解释已确认：`GET` 的 `null` 是命令成功但 key 不存在；TTL 是 Redis Server 的实时剩余时间，初次读到小于设定值是正常经过时间与秒级取整结果；去掉 `EX` 后 Server 不保存 TTL；Redis `String` 通过 Node `get()` 返回 JavaScript `string`；多条原子 `GET` / `SET` 的读改写组合仍会竞态丢失更新；首次 `EXPIRE ... NX` 设置 TTL，后续已有 TTL 时返回 `0`，因此不会延长固定窗口；同一 key 不能同时为 String 和 List，`WRONGTYPE` 是可捕获的命令错误，client 可继续使用；`error` 事件覆盖 client 生命周期中的异步 socket 错误，`connect()` reject 描述单次连接结果；非整数 String 的 `INCR` 返回值格式错误且旧值不变。W12-1 的独立实验已通过，但不代表 Auth Service 已接入 Redis。
- `closeRedis()` 生命周期新鲜复验：健康 Redis 上 `OPEN_BEFORE_CLOSE true`、关闭后 `OPEN_AFTER_CLOSE false`、重复关闭 `SECOND_CLOSE_OK true`；语法检查退出码为 `0`。内存假 client 的失败路径返回原始 `close_failed`，同时确认 `DESTROY_CALLED true`、`IS_OPEN_AFTER_FAILURE false`；这证明正常关闭具有幂等性，关闭异常会立即销毁连接并继续向上抛错。
- `login_rate_limiter.js` 稳定错误合同新鲜复验：真实 Redis 的不存在 key 返回两个 `ttl_seconds: -2`、`limited: false`、`retry_after_seconds: 0`；阈值 key 返回 `limited: true` 和对应 TTL；内存假 client 的 Redis 失败映射为 `code: redis_unavailable` 并保留 `cause.code: ECONNREFUSED`；无 TTL、非法回复映射为 `code: redis_data_invariant`；非法 client 配置映射为 `code: login_limiter_config_invalid`；语法检查和三组探针退出码均为 `0`。
- `login_rate_limiter.js` 输入边界新鲜复验：缺失输入、短 username、非法字符、数组/对象/`String` 包装对象 username、非法 IP 均返回 `code: login_limiter_input_invalid` 且不调用 Redis；合法 IPv4、IPv6 与真实 Redis 查询通过；username 阈值、IP 阈值和双维度较大 TTL 回归通过，退出码为 `0`。
- `recordFailure()` 新鲜复验：第一次调用两个维度均返回 `count: 1`、`expiry_set: 1`、`ttl_seconds: 5`；等待约 1.1 秒后第二次返回 `count: 2`、`expiry_set: 0`、TTL 从 `5` 降为 `4`；10 个并发失败最终 username/IP 均为 `10`，各维度只有一次 `expiry_set: 1`；第 5 次 username 失败返回 `limited: true`、`retry_after_seconds: 4`；回复长度错误返回 `redis_data_invariant`；语法和探针退出码均为 `0`。
- `clearUserFailures()` 新鲜复验：先记录同一 username/IP 两次失败，首次清理返回 `{deleted: 1}`；随后 username 为 `count: 0`、`ttl_seconds: -2`，IP 仍为 `count: 2`、TTL `60`；重复清理返回 `{deleted: 0}`；Redis 失败映射为 `redis_unavailable`，非法删除回复映射为 `redis_data_invariant`，非法 username 映射为 `login_limiter_input_invalid`；语法和探针退出码均为 `0`。
- `app.js` 新鲜复验：`node --check src/app.js` 退出码为 `0`；使用假的 db/limiter/bcrypt/JWT 依赖启动临时 HTTP 端口，`APP_LOGIN_MATRIX_PASS` 通过。非法输入不调用任何依赖；已限流直接返回 429 和 `Retry-After`；错误凭据按 `inspect → db → bcrypt → recordFailure` 返回 401/429；成功按 `inspect → db → bcrypt → clearUserFailures → jwt.sign` 返回 200；`inspect`、`recordFailure`、`clearUserFailures` 的 Redis 错误均返回 503，清理失败不签发 token。malformed JSON 另行验证返回 400；使用项目实际 `bcryptjs` 与 `jsonwebtoken` 的成功路径验证输出 `REAL_BCRYPT_JWT_APP_PASS`；携带伪造 `X-Forwarded-For` 仍输出 `TRUSTED_SOCKET_IP_PASS`，限流输入使用底层 socket 地址。随后仅按模块/公共入口补充中文注释，`node --check`、空白检查和最小回归仍通过。该探针不代表真实 HTTP 集成已完成。

- **当前能力点**：W12-2 Redis 并发失败计数精确性（R12-2-11）已实现、通过新鲜验证并完成掌握确认，等待用户说“下一步”；
- **已完成项**：W12-1 的 R12-1-01～12 均有对应证据；W12-2 已确认 username/IP 双维度、key 所有者、400/401/429/503、fail-closed 和调用顺序；Redis client 生命周期已通过；`login_rate_limiter.js` 的 key、`inspect()`、`recordFailure()`、`clearUserFailures()`、错误码、输入边界、TTL 和并发证据均已通过；`app.js` 的依赖合同、登录顺序、错误映射和 malformed JSON 边界已有新鲜 HTTP 探针证据；`server.js` 的配置边界、Redis 启动门禁、监听 Promise、`require.main` 门禁、HTTP→Redis→SQLite 关闭顺序和失败后继续清理已有新鲜证据；新增真实 Redis、临时 SQLite、临时 HTTP 端口的注册/登录/`/me` 联调测试已通过；
- **错误码记录**：W12-2 所有稳定模块错误码、HTTP 业务码、底层诊断码、Express `error.type` 与内部 reason/message 的区别，已整理进唯一学习笔记的“W12-2 错误码总表”；
- **当前未产生的证据**：Redis 故障映射与 JWT 撤销尚未完成；本步不扩大解释为这些能力已掌握。
- **下一待办项**：等待用户说“下一步”，再定义后续最小能力点；不提前进入 Redis 故障、JWT 撤销或 Docker。
