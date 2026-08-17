# W9 需求文档：领域与 SQLite 持久化

> 日期：2026-08-12 | 项目：ChatHub | 阶段：W9 切片 3 · 持久化
> 前置：W8 全部完成（私聊、确认、送达回执、在线快照、会话列表）
> 协议字段、帧正文和状态流向总览见：[ChatHub 协议字段与状态流向总图](ChatHub协议字段与状态流向总图.md)。任何字段或状态变更需同步更新该总图。

---

## 一、目标与边界

把聊天消息**持久化到服务端 SQLite**：重启服务后历史可查，网络断开或数据库失败时不崩溃；同一发送请求不会重复写入或重复创建气泡。

W9 交付的是单机、单 ChatServer 下的“已接受消息历史”。它不等于离线推送或跨设备可靠投递。

### 本阶段做

- 服务端 SQLite 建表、迁移、写入和最近历史查询；
- 持久 `message_id`、客户端重试 ID `local_id` 与幂等规则；
- 认证后加载当前用户最近 50 条消息，客户端按会话分组显示；
- 受帧容量约束的历史分块协议、客户端去重合并；
- SQLite 打开、写入、读取失败的可见错误与测试。

### 明确不做

- 离线推送、未读消息服务、多端同步、已读回执；
- 重启后恢复 `Delivered` 状态或待送达内存表；
- 无限滚动历史 UI、按关键字搜索、删除/撤回消息；
- 多 ChatServer、Redis 在线路由、数据库连接池。

> 接收者在转发排队后立刻断开时，已提交的消息会留在历史中，之后可被该用户的历史首屏读取；这不是实时离线推送，更不承诺 `Delivered`。

---

## 二、已确认的设计决策

### 1. 消息容量契约

- `content` 纯文本上限：**1024 字节**（`kMaxChatContentBytes`）；
- 单帧 body 总上限：**2048 字节**（`kMaxFrameBodyLength`）；
- 两者集中定义在客户端和服务端共用的 `chat_protocol.h`；
- W9-1 先完成这一拆分，因为一条合法聊天消息加 JSON 字段会超过 1024B；W10 继续补容量边界测试。

历史结果不能假定“50 条 = 一帧”。服务端逐块序列化，**每一个** `history_result` 的实际字节数都必须 `<= kMaxFrameBodyLength`；若一条已验证合法的记录仍无法编码进一帧，返回 `history_item_too_large`，不得绕过帧上限。

### 2. 两种消息身份

| 字段 | 谁生成 | 生命周期与用途 |
|---|---|---|
| `local_id` | 发送端 Qt 客户端 | 一次发送请求的稳定重试 ID；用于现有 ack、失败气泡和重试；同一用户重试必须保持不变。 |
| `message_id` | ChatServer 在首次 SQLite 成功提交时生成 | 持久业务身份；用于 B 的送达回执、历史记录去重、稳定排序的平手决胜和后续扩展；不得使用 `SessionId` 代替。 |

一条消息只存一行，不为发送者和接收者各复制一行。数据库约束 `UNIQUE(sender, client_local_id)`：同一用户用同一个 `local_id` 重发同一请求时，只能得到既有记录；若 `recipient`、`content` 或 `client_send_at` 与既有记录不同，返回 `idempotency_conflict`，不得静默覆盖。

### 3. 时间与排序

- `client_send_at`：客户端传来的 ISO 时间，仅供 UI 展示；保持现有格式和校验，不作为可信排序依据；
- `server_received_at_ms`：ChatServer 在首次入库前生成的 UTC 毫秒时间戳；历史查询唯一使用它排序；
- 相同毫秒按 `message_id` 决定稳定顺序；客户端传未来/过去时间也不能改变服务端历史顺序。

### 4. 持久化与确认语义

正常在线路由的顺序固定为：

```text
校验帧与认证身份
  → 校验接收者当前在线
  → SQLite 插入并提交
  → 以 message_id 记录 PendingDelivery
  → 向 B 排队转发 chat
  → 向 A 发送 chat_ack（含 message_id）
```

- SQLite 写入失败：不转发、不登记待送达、不发送 `chat_ack`；向 A 返回带 `local_id` 的 `database_write_failed`；
- 完全相同的重复请求：不新增行、不重复转发给 B；向 A 重发既有 `chat_ack`，其中携带已有 `message_id`；
- 接收者离线时保持 W8 的 `recipient_offline` 行为，不入库；
- SQLite 已提交但 B 随后断开的竞态不回滚已提交记录，也不得伪造 `Delivered`；
- `PendingDeliveryMap` 以 `message_id` 为键，值保存 A 的 `sender_local_id`、发送者会话、发送者身份和应回执的 B；B 的回执只携带 `message_id`，服务器验证 B 身份后才用保存的 `sender_local_id` 通知 A。
- 服务重启后 `PendingDeliveryMap` 仍是空的，历史中本人发送的消息只能恢复为 `Accepted`，不能恢复为 `Delivered`。

### 5. 历史首屏策略

认证成功后客户端自动请求“当前认证用户参与的最近 50 条消息”，不是“一开始就对未知的每个会话各拉 50 条”。服务端依据认证身份限制查询范围；客户端根据每条记录的 `sender/recipient` 计算 peer，复用 `m_conversations` 分组、排序和渲染。

本期只显示这 50 条首屏数据；协议保留游标和 `has_more`，但不要求 W9 做“加载更早记录”的按钮。

---

## 三、SQLite 表与迁移设计

数据库文件归属于 `chat-server`，路径必须来自配置或启动参数；测试使用独立临时数据库，不能连接开发数据。

```sql
CREATE TABLE IF NOT EXISTS messages (
    message_id            TEXT PRIMARY KEY,      -- 服务端 UUID，持久业务身份
    sender                TEXT NOT NULL,         -- 认证发送者用户名
    recipient             TEXT NOT NULL,         -- 目标用户名
    participant_low       TEXT NOT NULL,         -- 两个用户名按既定字节序排序后的较小者
    participant_high      TEXT NOT NULL,         -- 两个用户名按既定字节序排序后的较大者
    client_local_id       TEXT NOT NULL,         -- 发送方的重试/幂等 ID
    content               TEXT NOT NULL,         -- C++ 已校验 ≤1024 字节
    client_send_at        TEXT NOT NULL,         -- 客户端展示时间，不参与排序
    server_received_at_ms INTEGER NOT NULL,
    UNIQUE(sender, client_local_id)
);

CREATE INDEX IF NOT EXISTS idx_messages_conversation_order
    ON messages(participant_low, participant_high,
                server_received_at_ms DESC, message_id DESC);

CREATE INDEX IF NOT EXISTS idx_messages_user_order
    ON messages(sender, server_received_at_ms DESC, message_id DESC);
CREATE INDEX IF NOT EXISTS idx_messages_recipient_order
    ON messages(recipient, server_received_at_ms DESC, message_id DESC);
```

### 建表、连接与事务约束

1. `MessageRepository` 启动时打开连接、设置 `busy_timeout`，执行建表和 `PRAGMA user_version = 1`；是否启用 WAL 必须写入启动日志与需求实现记录。
2. W9 使用一个 Repository 连接，并且所有 Repository 调用都在现有 `Server::m_strand` 上串行执行；不在本阶段引入连接池、额外 DB 线程或多把锁。
3. 插入使用预编译语句和显式事务。插入、查询既有幂等记录和返回结果必须形成一个可解释的原子业务结果。
4. `participant_low/high` 由服务端从认证发送者和 `recipient` 生成，禁止相信客户端提供的会话归属。
5. 数据库不可用时，受影响请求返回错误，但其他 Session 的连接、心跳和认证状态保持可用；不承诺数据库故障期间的聊天写入成功。

---

## 四、Repository 职责

`MessageRepository` 隔离 SQLite API；网络路由、JSON、Session 映射和 Qt UI 不能直接调用 sqlite3。

| 接口语义 | 成功结果 | 失败结果 |
|---|---|---|
| `storeOrGetExisting(message)` | `Stored`（新插入）或 `DuplicateSame`（同一 sender + local_id 的完全相同请求），都返回完整持久记录 | `IdempotencyConflict`、`DatabaseError` |
| `loadRecentForUser(username, cursor, limit)` | 返回不超过 `limit` 条记录、`has_more` 和下一页游标 | `DatabaseError`、非法游标 |

持久记录至少包含：`message_id`、`sender`、`recipient`、`client_local_id`、`content`、`client_send_at`、`server_received_at_ms`。Repository 不保存 `SessionId`、`PendingDelivery` 或 `Delivered`。

---

## 五、历史协议

### 1. 新增消息类型

| type | 消息 | 方向 |
|---|---|---|
| 9 | `history_query` | 客户端 → 服务端 |
| 10 | `history_result` | 服务端 → 客户端 |

实现时同时更新 `isKnownMessageType()`、客户端入站分派和帧解码测试；9、10 已确定，不再保留“待确认 type 值”。

### 2. `history_query` 请求

```json
{
  "request_id": "uuid-or-stable-request-id",
  "limit": 50,
  "before": {
    "server_received_at_ms": 1723456789000,
    "message_id": "optional-cursor-id"
  }
}
```

- `request_id`：非空字符串，最大 64 字节，用于让客户端区分本次历史请求；
- `limit`：整数，服务端钳制到 `1..50`；首屏固定请求 50；
- `before`：首屏省略；未来加载更早历史时使用上一次结果中最早记录的 `(server_received_at_ms, message_id)`；
- 查询用户永远从认证 Session 推导，正文不得携带或覆盖 `sender`、`recipient`；未认证、格式错误或非法游标只返回错误，不执行查询。

### 3. `history_result` 响应分块

```json
{
  "request_id": "uuid-or-stable-request-id",
  "messages": [
    {
      "message_id": "server-uuid",
      "local_id": "sender-client-local-id",
      "from": "alice",
      "to": "bob",
      "content": "hello",
      "send_at": "2026-08-12T10:00:00.000Z",
      "server_received_at_ms": 1786490400000
    }
  ],
  "is_last_chunk": true,
  "has_more": false,
  "next_cursor": null
}
```

- 一个查询可得到多个 `history_result`；每块的 `messages` 按 `server_received_at_ms, message_id` 正序；
- 最后一块才给出本次请求整体的 `has_more` 与 `next_cursor`；无下一页时 `next_cursor = null`；
- 服务器按“先取最近 N 条，再反转为正序”的方式保证首次显示从旧到新；
- 历史中的 `local_id` 仅用于兼容现有消息模型和发送者状态关联；客户端去重必须使用 `message_id`；
- 历史加载不触发 `delivery_receipt`；若 `from == 当前用户名`，客户端可把状态显示为 `Accepted`，绝不能显示 `Delivered`。

---

## 六、服务端与客户端改动点

| 模块 | 改动 |
|---|---|
| `chat_protocol.h` | `kMaxFrameBodyLength = 2048`；新增 type=9/10；同步已知类型判断。 |
| 新增 `MessageRepository` | 打开/迁移 SQLite，封装写入、幂等查询、最近历史查询和错误转换。 |
| `server.cpp` / `server.h` | 路由按“提交 → 待送达 → 转发 → ack”顺序；ack 与转发正文携带 `message_id`；新增仅认证用户可调用的历史查询。 |
| `chat_payload.*` | 保持 `content` 的 1024B 校验；新增 history 请求字段与游标校验。 |
| `chatclient.*` | 认证成功后请求首屏历史；解析历史分块；接收实时 chat/ack 时接收 `message_id`。 |
| `ChatMessage` / `mainwindow.cpp` | 增加 `message_id`；以 `message_id` 合并历史与实时消息；按 `from/to` 复用现有会话模型分组。 |

---

## 七、实现顺序（分块教学）

1. **W9-1：合同与接口**
   - 先完成容量常量、type=9/10、`message_id/local_id` 责任表、Repository 头文件与结果类型；
   - 只设计首屏历史请求/分块响应，不写 SQLite 业务实现。
2. **W9-2：SQLite 写入与幂等**
   - 建表、迁移、预编译插入、同一 `(sender, local_id)` 查询；
   - 修改路由顺序，验证新写入、完全重复、冲突重复和数据库写失败。
3. **W9-3：历史首屏协议与客户端合并**
   - 实现 `history_query/history_result` 分块、认证归属限制和首屏 50 条；
   - 客户端按 `message_id` 去重、按会话分组、正序显示。
4. **W9-4：验收与故障复盘**
   - 重启、幂等、容量、数据库失败、历史分块和 UI 回归；
   - 记录“客户端时间不可信、持久 ID 与重试 ID 分离、历史与实时并发合并”三个取舍。

### 实现与验证记录

#### 2026-08-14｜W9-3 / B1：`history_query` 协议解析（已完成）

- 已在 `chat_payload.*` 定义历史查询解析结果、错误码和协议层游标，并实现 `request_id`、整数 `limit` 钳制、可选复合 `before` 游标及伪造身份字段的校验。
- 本步修复了首屏缺少 `before` 时的空指针解引用、合法游标时间戳的无条件错误返回，以及未构造 `std::optional` 即解引用的问题。
- 已统一 `chat_payload` 校验模块的类型命名和布局：`chatPayloadResult` 更名为 `ChatPayloadResult`，可能失败的 limit 规范化辅助函数更名为 `tryNormalizeHistoryLimit`；未改变 JSON 字段、错误码或校验语义。
- `chat_payload` 模块的说明性代码注释统一使用中文；API、JSON 字段和代码标识符仍保留其既定英文名称。
- 三类正文校验函数均按实际执行顺序补充中文步骤说明，明确每个字段校验和失败分支所防止的输入风险。
- `frame_decoder_test` 已直接覆盖合法首屏、完整 `before`、limit 上下界钳制，以及 JSON、请求 ID、limit、伪造身份字段和游标字段的拒绝路径。
- 验证：`cmake --build cmake-build-debug --parallel 2` 成功；`ctest --test-dir cmake-build-debug --output-on-failure` 为 2/2 通过。
- 下一步：B2 在认证后的 Session 消息分派中识别 `history_query`，并沿用现有协议错误响应路径。

#### 2026-08-16｜W9-3 / B2：Session 历史查询分派（已完成）

- `Session` 对未认证 `history_query` 返回 `scope=history`、`code=authentication_required` 的协议错误；该路径不关闭当前 Session，也不调用 `m_on_message`，因此不会进入 Server 或数据库。
- 已认证的 `history_query` 先调用唯一的 `parseHistoryQueryPayload()` 校验；校验失败返回 `scope=history` 的对应错误且不转交，校验成功才以原始协议帧调用一次 `m_on_message(m_id, message)`。
- B2 不查询数据库、不从请求正文读取用户身份，也不新增回调；现有回调会把请求投递到 `Server` 的 strand，认证身份推导、Repository 查询和 `history_result` 分块留给 B3。
- 验证：`cmake --build cmake-build-debug --parallel 2` 成功；`ctest --test-dir cmake-build-debug --output-on-failure` 为 2/2 通过。
- 已知测试缺口：现有 CTest 已直接覆盖历史正文解析，但尚未覆盖真实 `Session` 的认证后网络分派。当前 `verifyJwt()` 依赖硬编码密钥且没有测试注入点；在不复制密钥、也不扩大本步生产代码范围的前提下，该集成测试留待认证配置整改后补齐。

#### 2026-08-16｜W9-3 / B3：Server 历史查询与安全分块响应（已完成）

- `Server` 在自身 strand 上接收 `history_query`，再次复用 `parseHistoryQueryPayload()` 取得强类型请求；会话不存在、未登记认证身份、已被同名新会话接管、数据库不可用或读取失败时，仅向当前会话返回 `scope=history` 错误。
- 查询用户名只从 `m_session_to_username` 推导；协议层 `before` 显式转换为 Repository 层游标后调用 `loadRecentForUser()`，不在 Server 编写 SQL，也不信任客户端声明的用户身份。
- `StoredMessage` 统一映射为历史协议记录，并按实际序列化后的 JSON body 字节数分块。所有消息块保持 Repository 的旧到新顺序；只有最后块带 `has_more` 和 `next_cursor`，空查询也回一个 `messages=[]` 的最终块。
- 单条记录无法装入合法 2048B body 时返回 `history_message_too_large` 并记录日志；所有块先在内存中确定，再开始发送，避免出现半截成功响应。
- 新增 `history_response_test`，直接验证 Server 使用的响应序列化逻辑：字段映射、最终/中间块字段边界、空查询最终响应及真实 JSON body 超限判断。
- 验证：`cmake --build cmake-build-debug --parallel 2` 成功；`ctest --test-dir cmake-build-debug --output-on-failure` 为 3/3 通过。
- 剩余缺口：认证后的端到端网络历史查询仍依赖可注入的 JWT 测试配置；B3 已完成服务端实现，下一步进入 C1，由 Qt 客户端在认证成功后请求并解析历史响应。

#### 2026-08-17｜W9-3 / C1：Qt 历史首屏请求与分块聚合（已完成）

- `ChatClient` 在 type=5 认证成功后生成 `request_id`，发送固定 `limit=50` 的 type=9 `history_query`；请求状态在写帧成功后保留，失败、重连、断开、超时、非法帧和 `scope=history` 错误时统一清理。
- type=10 `history_result` 先按 `request_id` 关联：缺失、空或不匹配的 ID 只报告错误，不能取消仍在等待的合法请求；仅已确认匹配当前 ID 的正文/字段错误才丢弃已暂存半页。
- 客户端严格校验消息必填字段、ISO 时间、非负整毫秒服务端时间、分块字段和最终翻页字段；每块先完整转换为临时列表，再在累计不超过 50 条后提交。仅最终块复制完整列表、清理请求状态并发射 `historyPageReceived`，不触及 UI 合并或送达回执。
- 修复帧长度解码：四个长度字节各自先转换为 `quint32` 再左移，避免损坏帧的有符号左移未定义行为。
- 新增 `history_client_test`：本地 TCP 对端验证认证后请求、`request_id/limit`、无 `request_id` 的响应不取消有效请求、非最终块不提前发射、`ping/pong` 因果同步，以及最终块一次性交付两条 `Received` 历史消息。
- 验证：`cmake --build cmake-build-debug --parallel 2` 成功；`ctest --test-dir cmake-build-debug --output-on-failure` 为 4/4 通过；C1 范围六维 Qt 审查无未解决高置信问题。
- 剩余缺口：C2 负责把 `historyPageReceived` 的完整页面按 `message_id` 去重、按会话合并并正序渲染到 `MainWindow`；本步不改 UI。

---

## 八、验收矩阵

| 场景 | 可观察结果 |
|---|---|
| 新写入 | A→B 成功后只有一条数据库记录；B 收到带 `message_id` 的 chat；A 的 ack 带相同 `message_id`。 |
| 完全重复请求 | A 用同一 `local_id` 和相同正文重发；数据库仍一条，B 不新增实时气泡，A 收到既有 `message_id` 的 ack。 |
| 冲突重复 | A 复用同一 `local_id` 但改正文/接收者/客户端时间；返回 `idempotency_conflict`，原记录不变。 |
| 重启 | 重启 ChatServer 后重新登录；最近 50 条按服务端接收顺序正序显示；本人历史不伪造“已送达”。 |
| 历史分块 | 至少 50 条且含接近 1024B 正文时，所有结果帧都不超过 2048B；客户端最终只插入每个 `message_id` 一次。 |
| 时间篡改 | 客户端提交未来/过去 `send_at` 不改变历史的服务端排序。 |
| 历史权限 | 未认证或篡改历史查询字段不能读取记录；服务端只返回当前认证用户参与的消息。 |
| 数据库失败 | SQLite 打开/写入/读取失败有明确日志与错误码；相关请求失败但 Server 不退出、其他连接保持可用。 |

当前自动化覆盖：`message_repository_test` 将临时目录作为数据库路径，验证 `MessageRepository::open()` 返回失败并完成临时目录清理；Server 在数据库不可用时的路由错误和持续监听属于后续集成验收。
| 容量 | 1024B `content` 加 JSON 封装后可发送；超过内容或帧上限的输入被拒绝。 |

## 九、最终验收标准

1. 两个客户端互发消息后，重启 ChatServer、重新登录，能看到最近 50 条以内的历史，顺序与会话分组正确；
2. 同一请求重复发送不重复入库、不重复给 B 创建消息；冲突复用 `local_id` 被拒绝；
3. 断网、重连、历史与实时消息交错到达时，不崩溃、不重复显示；
4. SQLite 打开、写入或读取失败时有可见错误，不影响服务端存活；
5. 所有历史响应遵守 2048B 帧 body 上限，1024B 聊天正文仍可正常发送；
6. 构建、CTest、SQLite Repository 测试和双客户端人工验收均通过。
