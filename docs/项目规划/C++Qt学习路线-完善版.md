# C++ / Qt 学习路线（完善版）

> **求职目标重定位（2026-08-12，更新）**：个人基线为双非本科、零实习。路线调整为“立即投递实习/秋招 + 用可验证的企业级单机全栈作品补齐春招竞争力”。ChatHub 扩展至 W14：W9–W13 完成 SQLite→MySQL、Redis 限流/受控撤销、可靠性与工程交付；W14 再完成联系人/好友闭环。llfcchat 只作为局部代码对照蓝图，不复制其单例或多服务架构；不做群聊、文件、多服务器、gRPC 或微服务堆砌。完整执行计划见 [`求职学习计划-2026秋至2027春.md`](./求职学习计划-2026秋至2027春.md) 与 [`llfcchat参考索引与对照规则.md`](./llfcchat参考索引与对照规则.md)。

> 08-09 学习证据：ChatHub 完成内部代码模块化整理；协议常量与聊天模型集中管理，Qt 客户端、UI、服务端会话按职责分组。构建成功，CTest 2/2 通过；当前仍为学习中，下一练习是将接收消息接口收敛为 `ChatMessage` 直传。

> 08-10 学习证据：已完成 `type=1` 收到消息的 `ChatMessage` 直传，并完成双客户端显示与非法 `send_at` 拒绝的人工验收；当前仍为学习中，下一练习是迁移发送与状态更新接口。

> 08-10 发送链路证据：已完成发送前本地消息建模、稳定 `local_id`/UTC 时间、失败关联与原消息重试；双客户端、断线失败和目标回退均已人工验收。当前仍为学习中，下一练习是确认与失败状态更新接口收敛。

> 08-10 状态更新证据：已完成确认和失败帧的 `ChatMessage` 状态更新直传，未知 ID 与无 ID 错误路径保持正确边界；构建、CTest 与人工验收通过。当前仍为学习中，下一练习是 UI 渲染接口接收完整消息模型。
>
> 08-10 UI 渲染证据：已独立将 `renderCurrentConversation()` 与 `appendMessageBubble()` 收敛为传递完整 `ChatMessage`；气泡自行读取展示字段，失败重试仅凭稳定 `local_id` 回查既有消息。构建和人工验收通过；当前仍为学习中，下一练习是整理气泡渲染中的最小数据捕获。
>
> 08-10 回调依赖证据：已独立将失败重试 Lambda 收敛为仅按值捕获 `local_id`，不再复制完整 `ChatMessage`，同时保持未知 ID 的安全返回和原有重试行为。构建与人工验收通过；当前仍为学习中。
>
> 08-10 状态与身份证据：已修复消息查询返回局部副本地址的风险，并为会话列表建立 `Qt::UserRole` 稳定 peer 身份；多会话创建和重复会话去重均已人工验收。未读计数状态仍在分步实现中。

> 08-10 W8-8 阶段证据：已独立实现 `QListWidget` 会话项按 `Qt::UserRole` 稳定身份置顶，并仅在本地发送写入完整 `ChatMessage` 后接入“写入模型 → 确保项 → 置顶 → 刷新”的顺序；Bob/Carol 连续发送与非排序更新均已人工验收，构建成功、CTest 2/2 通过。当前仍为学习中，下一练习是迁移接收消息路径。

> 08-10 W8-8 接收证据：已独立将接收消息按 `message.from` 迁移到会话置顶路径；当前会话接收不增加未读，非当前会话接收置顶且未读递增。人工验收、构建和 CTest 2/2 均通过；当前仍为学习中，下一练习是仅在防御性补建后写入的路径置顶。

> 08-10 W8-8 完成证据：已独立完成防御性补建路径置顶，并完成会话最近消息置顶的整体验收。仅完整新消息首次写入模型后才重排；已有消息状态更新、清未读与纯显示刷新不排序。本地发送、收消息和补建路径均已人工验收，构建成功、CTest 2/2 通过；当前仍为学习中。

> **制定**：2026-07-18 · **求职重排**：2026-08-12 · **当前周期**：2026 秋至 2027 春（按 T0 阶段推进，而非固定 12 周）
>
> **目标**：独立完成并讲清一个可演示的 Qt/C++ 即时聊天系统（ChatHub V1），按需扩展 V2。

---

## 进度总览

```
W1 工程基础     ████████████  100% [01-git-debug-lab ✅] [02-modern-cpp ✅] [03-qt-containers ✅]
W2 Qt 补缺      ████████████  100%  [02-adaptive-bubble ✅] [03-settings-export ✅] [04-thread-worker ✅] [05-lan-chat ✅] [06-http-client ✅]
W3 asio 核心    ██████████░░ 85% [01-tcp-concepts 🟡]
W4 asio 进阶    ████████░░░░ 65% [01-tcp-concepts 🟡，提前练习]
W5 Auth 服务    ████████████ 100%
W6 骨架集成     ████████████ 100% [三仓全通]
W7 切片1·握手   ████████░░░░  80% [客户端握手完成，双在线验收通过]
W8 切片2·聊天   ████████████  100% [私聊、确认帧、失败重试、断线 UI、会话排序、最终送达回执与在线用户快照均已验收]
W9 切片3·持久   ░░░░░░░░░░░░   0%
W10 交付稳定    ░░░░░░░░░░░░   0%
W11 MySQL 集成  ░░░░░░░░░░░░   0%
W12 Redis/安全  ░░░░░░░░░░░░   0%
W13 工程交付    ░░░░░░░░░░░░   0%
W14 联系人好友  ░░░░░░░░░░░░   0%
W15 复盘面试    ░░░░░░░░░░░░   0%
```

| 维度        | 当前                  | 目标        |
|-----------|---------------------|-----------|
| C++ 核心    | ✅ 100%              | —         |
| STL / 标准库 | ✅ 100%              | —         |
| 现代 C++    | ✅ 100%              | —         |
| Qt 框架     | 🟡 75% (25 笔记·9 项目) | ✅ 掌握      |
| 构建 / 工程化  | 🟡 15% (CMake 基础)   | ✅ V1 必须   |
| C++ 网络库   | ❌ 0%                | ✅ V1 必须   |
| Node.js   | ❌ 0%                | ✅ V1 必须   |
| 项目实战      | 🟡 20% (4 小型项目)     | ✅ V1 + V2 |

> **更新规则**：每完成一个实验 / 每周学习后，同步更新本文档的状态标记。
>
> 标记说明：`✅ 已掌握` · `🟡 学习中` · `❌ 未学` · `🔴 卡住`

---

## 1. 对现有材料的总结

两份计划的基础很好，已经清楚地把目标落在"C++ 服务端 + Qt 客户端"的聊天项目上，而不是继续堆叠零散控件练习。

**当前已具备：**

| 类别     | 具体内容                                                                    |
|--------|-------------------------------------------------------------------------|
| C++ 基础 | 核心语法 · 继承/多态 · 模板 · 异常处理 · 类型转换                                         |
| STL    | vector/deque/list/set/map/unordered · 迭代器 · 算法 · 智能指针 · lambda · 文件流    |
| 现代 C++ | auto/nullptr/override/final · 移动语义 · default/delete · explicit/decltype |
| Qt 基础  | 信号槽 · QObject · QString/QByteArray · QVariant · qDebug                  |
| Qt 控件  | 三大窗口 · 布局(H/V/Grid/Form/Splitter) · 按钮/输入/显示/对话框 · 菜单/工具栏/状态栏           |
| Qt 进阶  | QSS · 事件系统(eventFilter) · Model/View/Delegate · QSortFilterProxyModel   |
| 小型项目   | 计算器 · QQ登录 · 番茄钟 · 密码本                                                  |

**真正需要补齐的主干**：工程化（Git、CMake、依赖管理、调试/测试）→ Qt 网络与线程 → TCP 协议与异步 asio → HTTP/认证/SQLite → 一个完整、稳定的聊天闭环。

`llfcchat` 是合适的“架构和排错参考库”。它的开发文档从 HTTP、gRPC、验证服务、Qt UI、asio ChatServer，一直延伸到 Redis、多服务器、文件传输和分布式锁；本路线只吸收 Redis 的登录限流/受控撤销这一单机明确职责，其余扩展仍不作为当前项目的前置条件。

---

## 2. 对原计划的关键调整

| # | 原计划内容                                 | 调整                                                                           | 原因                       |
|---|---------------------------------------|------------------------------------------------------------------------------|--------------------------|
| 1 | 先学完 Beast、WebSocket、gRPC async 才开始主项目 | 第 7 周即启动 ChatHub V1；每周交付可运行纵向切片                                              | 更早获得真实联调反馈，避免"学完才会做"的断层  |
| 2 | C++20 coroutine 在 asio 早期强制学习         | 先用回调写稳 Session/生命周期/分帧；第 11 周再重构一个模块为协程                                      | 协程会掩盖异步控制流，初学阶段反而不利于定位问题 |
| 3 | 客户端同时写 Qt 网络和 asio 网络                 | Qt 客户端固定使用 `QNetworkAccessManager + QTcpSocket`；asio 专注服务端                   | 每一层只学一套网络抽象，降低认知负担       |
| 4 | V1 同时使用 gRPC、Redis、多 ChatServer、文件续传  | 当前只做 `Qt + Node Auth + asio Chat + SQLite/MySQL + Redis 限流/JWT 撤销`；其余仍后置 | Redis 仅在明确职责下引入，避免把 3~4 个高风险系统问题同时塞入项目 |
| 5 | QPainter 与设计模式独立占较长时间                 | QPainter 限时 2 天完成气泡；模式只在产生实际问题时引入                                            | 聊天项目优先级是网络、协议、边界和可维护性    |
| 6 | 缺少测试、日志、协议边界                          | 从第 1 周加入 CTest、日志、错误码、最大包长和异常输入测试                                            | 这些才是网络项目可讲、可排错、可维护的基础    |

---

## 3. 目标架构：ChatHub V1

```
┌─────────────────────────────────────────────────────────┐
│  Qt 6 客户端（MinGW + CMake）                            │
│                                                          │
│  HTTP：注册 / 登录 ─────────► Node.js Auth Service        │
│                                ├─ Express 路由            │
│                                ├─ bcrypt 密码哈希         │
│                                ├─ JWT 签发 / 验证         │
│                                ├─ users.sqlite（当前）      │
│                                └─ Redis：登录限流 / 撤销状态 │
│                                                          │
│  TCP：JWT 聊天连接 ─────────► C++ asio ChatServer         │
│                                ├─ Session 管理            │
│                                ├─ 定长头 + JSON 体协议    │
│                                ├─ 在线路由                │
│                                ├─ MessageRepository        │
│                                ├─ SQLite：开发/测试         │
│                                └─ MySQL：集成交付           │
└─────────────────────────────────────────────────────────┘
```

**边界规则：**

| 规则     | 说明                                                            |
|--------|---------------------------------------------------------------|
| 数据归属   | 用户数据只由 Auth 服务管理；聊天数据只由 ChatServer 管理                         |
| 禁止直连   | 客户端不直接访问数据库                                                   |
| JWT 验证 | ChatServer 使用同一签名密钥验证 JWT；撤销检查在 `jti` 生命周期设计完成后再接入 |
| 数据库边界 | 客户端不直连数据库；Repository 隔离 SQLite 测试实现与 MySQL 集成交付实现 |
| Redis 边界 | Redis 只保存 TTL 临时状态（登录限流、受控撤销），不保存聊天正文真相 |
| 交付边界  | Docker Compose 只编排 MySQL/Redis；Qt 客户端仍由本机 CMake 构建             |

### V1 必做 vs 不做

| V1 必做              | V1 不做（→ V2 Backlog）   |
|--------------------|-----------------------|
| ✅ 注册、登录、JWT 过期处理   | ❌ 好友系统                |
| ✅ 两客户端文字聊天         | ❌ 群聊                  |
| ✅ 上线 / 下线 / 断线提示   | ❌ 图片 / 文件传输           |
| ✅ SQLite/MySQL 聊天记录持久化与读取 | ❌ 多 ChatServer / 分布式锁 |
| ✅ Redis 登录限流与受控 JWT 撤销 | ❌ WebSocket / gRPC / Protobuf |
| ✅ 错误提示、日志、测试、CI 与可重复启动 | ❌ 音视频 / 群聊 / 文件传输 |

---

## 4. 12 周可执行计划

> 若每周仅有 8–10 小时，将每周拆为两周；若每周有 30 小时，可保持周次不变，但**不要跳过验收**。

---

### W1 · 7/18–7/26 · 工程基础

| # | 任务                                                                | 状态      |
|---|-------------------------------------------------------------------|---------|
| 1 | `01-git-debug-lab`：Git 工作流 · 分支 · 破坏→diff→修复→commit（≥6 条提交）       | ✅ 07-18 |
| 2 | CMake Presets 配置 · CTest 最小测试 · 断点/调用栈定位                          | ❌       |
| 3 | 现代 C++ 补缺：`enum class` · `constexpr` · `string_view` · `optional` | ✅ 07-20 |
| 4 | Qt 容器对照 STL：QVector/QMap/QHash vs std 对比速查                        | ✅ 07-20 |

**本周验收（不通过不进入 W2）：**

- [ ] 一个 `cpp-lab` 仓库可从空目录配置、构建、测试
- [x] 至少 6 个语义化提交（✅ 11 条）
- [ ] 能独立定位一个编译错误和一个运行时错误

---

### W2 · 7/27–8/2 · Qt 补缺

| # | 任务                                                                             | 状态      |
|---|--------------------------------------------------------------------------------|---------|
| 1 | `02-adaptive-bubble`：`paintEvent + QPainter + QFontMetrics` 绘制自适应聊天气泡          | ✅ 07-20 |
| 2 | `03-settings-export`：`QFile / QSettings / QStandardPaths` 配置持久化 + UTF-8 CSV 导出 | ✅ 07-20 |
| 3 | `04-thread-worker`：`QThread` worker-object + `moveToThread` 后台任务 + 进度条 + 取消    | ✅ 07-20 |
| 4 | `05-lan-chat`：`QTcpSocket / QTcpServer` + 文件传输（TCP 协议设计）                       | ✅ 07-21 |
| 5 | `QNetworkAccessManager` HTTP GET/POST 封装 + 超时处理                                | ✅ 07-27 |

**本周验收：**

- [x] 气泡能根据文字自动调整尺寸；中英文/emoji/多行不裁切
- [x] 两进程互发消息；网络/后台任务不阻塞 UI
- [x] 主动断开、错误和重连提示正确

> 📖 **llfc 对照**：day02（客户端 HTTP 管理类设计）· day15（客户端 TCP 管理类设计）

---

### W3 · 8/3–8/9 · asio 核心

| # | 任务                                                              | 状态                                               |
|---|-----------------------------------------------------------------|--------------------------------------------------|
| 1 | TCP 协议基础：端口、字节序、粘包/半包原理                                         | ✅ 07-28（长度字段分帧、半包与粘包实验已验证）                  |
| 2 | asio `io_context` · `steady_timer` · `strand` · `post/dispatch` | 🟡 07-31（心跳已完成；`strand_demo` 已验证同 strand 内 `dispatch` 的输出顺序。仍需独立解释并迁移 `post/dispatch` 的选择。） |
| 3 | `async_accept / async_read / async_write` · 缓冲区管理               | 🟡 07-31（真实 Server 已具备分帧、写队列背压、心跳与 `operation_aborted` 收尾；全量构建与 FrameDecoder CTest 通过。仍需独立修改异常路径。） |
| 4 | 🛠️ 异步 echo server：多连接支持 + 统一日志                                 | 🟡 07-31（双客户端广播、连接 ID、断开移除及双 strand 均已验收；日志尚未升级为结构化统一格式。） |

**本周验收：**

- [x] 异步 echo server 支持多连接
- [x] 能解释"一次 `read` 不等于一条消息"
- [x] 日志可关联连接 ID 和事件

> 📖 **llfc 对照**：day16（asio 实现 TCP 服务器）· day42（Qt 粘包血案）

---

### W4 · 8/10–8/16 · asio 进阶

| # | 任务                                                          | 状态 |
|---|-------------------------------------------------------------|----|
| 1 | `Session` 生命周期 · `enable_shared_from_this` · 写队列            | 🟡 07-31（`shared_from_this`、异步回调持有 self、单连接写队列、断开后在线表移除均已实现并记录；仍需独立迁移。） |
| 2 | 设计消息头：`magic` + `version` + `type` + `body_length` · 最大包长限制 | 🟡 07-31（8 字节头、chat/ping/pong/error、1024 字节上限已实现；FrameDecoder 的半包、粘包、非法字段和边界长度共 7 场景已通过。） |
| 3 | 恶意输入处理：超长包拒绝 · 非法 type 返回错误 · JSON 字段校验                     | 🟡 07-31（协议错误关闭当前连接；JSON 格式、content、空白、伪造 sender_id 和 200 字节上限均有业务错误返回；待独立补充 JSON 验收。） |
| 4 | 🛠️ 多客户端广播服务器                                               | 🟡 07-31（A 发消息仅 B 收到、断开后在线表移除已验证；当前规则为不回显发送者。） |

**本周验收：**

- [x] 连续发送、拆包、粘包均正确处理
- [x] 拒绝超长包/非法类型且进程不崩溃
- [x] 客户端异常断开时服务端不崩溃

> 功能验收已通过，但 W4 保持 🟡：还需要学习者能独立修改并解释协议、生命周期与路由，才标记为已掌握。

> 📖 **llfc 对照**：day16（Session 管理）· day35（心跳逻辑）

---

### W5 · 8/17–8/23 · Auth 服务

| # | 任务                                 | 状态 |
|---|------------------------------------|----|
| 1 | HTTP 原理：REST · 状态码 · JSON · 幂等性    | ✅ 08-02（已讲解：方法/状态码/JSON/幂等性；GET /health 与 / 两个接口实战） |
| 2 | Node.js 基础：npm · 模块 · 异步模型         | ✅ 08-02（npm install、package.json、require 模块、同步 vs 异步） |
| 3 | Express 路由 + 中间件 + 错误处理            | ✅ 08-02（app.get/post、express.json() 中间件、res.status().json()、统一 400/401/409 错误返回） |
| 4 | SQLite（better-sqlite3）用户表设计 + 迁移脚本 | 🟡 08-02（建表、查询、插入、唯一约束 409、? 占位符防注入；占位符密码练习已做，迁移脚本未做） |
| 5 | bcrypt 密码哈希 + JWT 签发/验证 + exp 过期   | ✅ 08-02（bcrypt 哈希/比对、注册→登录闭环、JWT 签发/验证/过期 7 项测试全通过；08-03 密钥改环境变量 dotenv + 输入校验加强） |

**本周验收：**

- [x] `POST /register` · `POST /login` · `GET /health` 可用
- [x] 密码非明文存储
- [x] token 过期与错误密码返回明确错误
- [x] 输入校验（长度/字符集）覆盖 register 与 login

> W5 完整收官（08-03）：HTTP/Express/SQLite/bcrypt/JWT/环境变量/输入校验全部验收通过。

> 📖 **llfc 对照**：day04–05（Beast HTTP/JSON 解析）· day08（邮箱认证服务，我们改 JWT）· day14（登录功能）

---

### W6 · 8/24–8/30 · 骨架集成

| # | 任务                                                              | 状态 |
|---|-----------------------------------------------------------------|----|
| 1 | Qt 登录页使用真实 HTTP（`QNetworkAccessManager` 调用 Auth API）            | ✅ 08-04（client-qt 登录/注册页 + HttpClient 真实 HTTP） |
| 2 | 学习 SQLite C++ 基础 API（sqlite3 库）                                 | ❌ 未做（chat-server 持久化 W9 做） |
| 3 | 建立 ChatHub 仓库骨架：`client-qt/` · `chat-server/` · `auth-service/` | ✅ 08-04（三仓 + GitHub ChatHub） |
| 4 | 配置 CMake + 启动脚本（一个命令启动所有服务）                                     | ✅ 08-04（start_all.bat 一键启动 auth-service + chat-server） |

**本周验收：**

- [x] Qt 登录页能完成真实注册和登录
- [x] 失败不会卡住 UI
- [ ] 请求/响应日志可定位问题
- [x] 服务可由一个命令启动（start_all.bat）

---

### W7 · 8/31–9/6 · ChatHub 切片 1：握手

| # | 任务                                         | 状态 |
|---|--------------------------------------------|----|
| 1 | ChatHub 仓库骨架 · 共享协议定义（消息头格式）               | ✅ 08-04（8字节头 + auth/chat/ping/pong/error） |
| 2 | Qt TCP 客户端：连接 + 发送 JWT + 接收认证结果            | ✅ 08-04（ChatClient 类：connectToHost → auth帧 → 收认证结果；Qt Creator 双实例验证通过） |
| 3 | asio ChatServer：接受连接 + 验证 JWT + 注册 Session | ✅ 08-04（jwt-cpp 验证 HS256，好token通过/坏token断开/未认证断开） |
| 4 | 协议分帧在真实客户端-服务端联调                           | ✅ 08-04（Qt 客户端组帧/解帧，修复 magic 截断 bug 后双客户端在线数=2） |

**本周验收：**

- [x] 两个 Qt 客户端能登录并建立被认证的 ChatServer TCP 连接
- [x] 无效 token 被拒绝，有可理解错误提示

> 📖 **llfc 对照**：day17（登录验证）· day18–19（聊天主界面 + 搜索框）

---

### W8 · 9/7–9/13 · ChatHub 切片 2：聊天

| # | 任务                                          | 状态 |
|---|---------------------------------------------|----|
| 1 | 私聊路由：uid → Session 映射 + 消息转发                | ✅ 08-05（认证回调登记 + 双向映射表 + sendToUser 私聊路由；Python 验证 A→B 只转发给 B） |
| 2 | Qt 聊天窗口：QListWidget + QStackedWidget + 气泡列表 | 🟡 08-12（左右气泡、发送者、时间、失败重试、会话排序、最终送达回执与在线用户快照均已验收；仍待后续持久化场景迁移后评定掌握度） |
| 3 | 消息确认机制（ack）                                 | ✅ 08-06（queued → ack / error；local_id 关联 pending 气泡，双客户端与离线重试验收通过） |
| 4 | 在线用户列表 / 上线 / 下线通知                          | 🟡 08-12（`online_users=8` 全量快照、同名登录新连接接管、断线广播、Qt 缓存与列表回填、点击仅回填接收者均已人工验收；待独立迁移该状态快照模式后评定掌握度） |

**本周验收：**

- [x] A → B → A 文字消息可达（Python 端到端验证：私聊送达含 from、ack 确认）
- [x] 未登录/离线用户有可理解反馈（recipient_offline + 错误码）
- [x] 聊天气泡渲染正确（GUI 实测通过：左右气泡、时间、失败红点与重试）
- [x] 最终送达回执正确（A 的既有消息从 Accepted 更新为 Delivered；不新建气泡、不改变未读或会话顺序）

> 📖 **llfc 对照**：day20–22（聊天列表 + 气泡对话框）

---

### W9 · T+3–T+6 周 · ChatHub 切片 3：持久化

| # | 任务                                                       | 状态 |
|---|----------------------------------------------------------|----|
| 1 | SQLite 消息表、索引与持久 `message_id` 设计                         | ❌  |
| 2 | ChatServer 消息存储 + 按会话加载最近历史                            | ❌  |
| 3 | 重连后显式加载历史；明确不做离线推送/多端同步                         | ❌  |
| 4 | 区分短期回执索引和持久消息状态；补数据库失败与重复请求边界              | ❌  |

**本周验收：**

- [ ] 重启服务后历史记录仍可查询，顺序与数量上限正确
- [ ] 断网后不会崩溃或重复写入；重新认证后能显式加载历史

> 📖 **llfc 对照**：day29（好友认证和聊天通信）· day35（心跳）· day37（聊天信息存储方案）

---

### W10 · T+7–T+8 周 · 交付稳定

| # | 任务                            | 状态 |
|---|-------------------------------|----|
| 1 | 统一消息/在线快照容量契约，补协议与路由边界测试 | ❌  |
| 2 | 客户端/服务端认证阶段超时；3 账户连续消息回归   | ❌  |
| 3 | 配置、日志、错误码与可复现故障记录             | ❌  |
| 4 | README：架构图、编译运行、协议、测试与演示说明   | ❌  |

**本周验收：**

- [ ] 新环境按 README 在 30 分钟内跑起并执行测试
- [ ] 至少 3 个测试账户连续发消息、历史加载和异常断开均不崩溃

---

### W11 · T+9–T+11 周 · MySQL 集成与存储抽象

| # | 任务 | 状态 |
|---|---|---|
| 1 | 设计 `MessageRepository` 接口与 SQLite 测试实现 | ❌ |
| 2 | 新增 MySQL 集成实现、建表/索引/初始化脚本 | ❌ |
| 3 | 用同一组业务测试校验 SQLite 与 MySQL 的查询、事务和错误边界 | ❌ |
| 4 | 记录 Repository 隔离、索引和事务取舍 | ❌ |

**本周验收：**

- [ ] 重启 MySQL 后聊天历史仍正确加载；重复 `message_id` 不重复写入；
- [ ] 能解释为什么 SQLite 适合本地测试、MySQL 适合集成交付，以及事务保护的业务边界。

---

### W12 · T+12–T+14 周 · Redis、安全与认证边界

| # | 任务 | 状态 |
|---|---|---|
| 1 | Redis 最小实验：String、TTL、原子计数与失败处理 | ❌ |
| 2 | Auth 服务接入按用户/IP 的登录限流 | ❌ |
| 3 | 设计 JWT `jti`、过期清理与撤销检查边界 | ❌ |
| 4 | 为限流/撤销补自动化或可重复集成验收 | ❌ |

**本周验收：**

- [ ] 多次错误登录被限流，TTL 到期后恢复；正常账号不被误伤；
- [ ] 若完成撤销链路，被撤销 Token 的新认证被拒绝且错误可解释。

---

### W13 · T+15–T+18 周 · 工程交付与验证

| # | 任务 | 状态 |
|---|---|---|
| 1 | Docker Compose 编排 MySQL/Redis；配置模板和启动说明 | ❌ |
| 2 | CMake Presets、CTest 路由/数据库测试、GitHub Actions | ❌ |
| 3 | 结构化日志、3–20 模拟客户端压测与故障复盘 | ❌ |
| 4 | README、架构/API 文档、演示录屏与 Release Checklist | ❌ |

**本周验收：**

- [ ] 新环境按 README 启动依赖、构建客户端/服务端并完成端到端测试；
- [ ] CI 自动构建和测试；压测、异常断开与数据库失败均有记录。

---

### W14 · T+19–T+22 周 · 联系人与好友闭环

> 先完成 W13 的工程验收，再以 llfcchat 的好友流程为局部对照；先自己设计，再阅读 [`llfcchat参考索引与对照规则.md`](./llfcchat参考索引与对照规则.md) 指定的局部源码。不得复制全局 `Singleton`、UI 类名或消息协议。

| # | 任务 | 状态 |
|---|---|---|
| 1 | 设计用户搜索、好友申请、接受/拒绝、解除关系的状态与关系表 | ❌ |
| 2 | 明确 HTTP 与 TCP 分工；新建私聊前校验双方好友关系 | ❌ |
| 3 | Qt 联系人模型/列表与“从联系人进入既有会话”交互 | ❌ |
| 4 | 重复申请、交叉申请、拒绝、解除关系和非好友私聊的自动化/端到端验收 | ❌ |
| 5 | 记录 llfcchat 对照差异：职责边界、数据一致性和测试取舍 | ❌ |

**本周验收：**

- [ ] 非好友不能新建私聊；好友建立后双方联系人和私聊入口一致；
- [ ] 关系状态的重复/并发边界可解释，有测试或可重复验收；
- [ ] 能说明为什么好友关系属于持久业务数据，而不是在线列表或 UI 过滤条件。

---

### W15 · T+23 周至春招 · 复盘、面试与滚动投递

| # | 任务 | 状态 |
|---|---|---|
| 1 | 项目复盘：架构、协议、数据一致性、安全边界与三个核心故障 | ❌ |
| 2 | Qt/桌面客户端版与通用 C++ 版中文简历 | ❌ |
| 3 | 高频算法、C++、网络、SQL、Redis、OS 的错题与问答库 | ❌ |
| 4 | 5 分钟演示视频 / 录屏与 GitHub 作品集整理 | ❌ |
| 5 | 每周投递、面试复盘；只按真实 JD 做小型技术 spike | ❌ |

**本周验收：**

- [ ] 陌生人可按 README 运行 ChatHub 完整作品；
- [ ] 能清晰解释架构、协议、数据库、Redis、线程模型和一个真实 Bug 的定位过程；
- [ ] 有持续更新的投递表、两份简历与至少两次模拟面试记录。

---

## 5. 每周工作模板

| 星期          | 内容                                  | 产出      |
|-------------|-------------------------------------|---------|
| **周一**      | 阅读官方文档与最小例子，写 30~60 行实验代码           | 实验笔记    |
| **周二 ~ 周四** | 实现当周唯一验收目标；每天至少一次小提交                | 可运行代码   |
| **周五**      | 补测试、异常路径、README；写一页复盘（问题→原因→修复→仍不懂） | 复盘文档    |
| **周末**      | 只做补缺或演示，不无边界扩功能                     | 演示 / 笔记 |

每个功能按这个顺序推进：

```
接口/协议 → 最小成功路径 → 错误路径 → 测试/日志 → UI
```

> **网络项目先让命令行或测试程序跑通，最后再接 Qt 画面。**

---

## 6. 代码仓库与产出规范

### 仓库结构

```
D:\CppLearn\
├── cpp-lab/              # W1–W4 实验仓库（独立 Git）
│   ├── 01-git-debug-lab/
│   ├── 02-adaptive-bubble/
│   ├── 03-settings-export/
│   ├── 04-thread-worker/
│   └── 05-lan-chat/
│
├── chathub/              # 🏆 ChatHub V1 主项目（独立 Git）
│   ├── client-qt/        # Qt Widgets · QNetworkAccessManager · QTcpSocket
│   ├── chat-server/      # asio · 协议 · Session · Repository · SQLite/MySQL
│   ├── auth-service/     # Express · JWT · users.sqlite · Redis 限流
│   ├── db/               # MySQL 建表、索引、初始化/迁移脚本
│   ├── docker-compose.yml # MySQL + Redis 开发依赖
│   ├── .github/workflows/ # W13 CI
│   ├── tests/
│   ├── docs/             # 架构图 · 协议文档 · 运行说明 · 项目规划 · 学习笔记
│   └── CMakePresets.json
│
└── QT/                   # 原有学习项目（不是 Git 仓库）
    └── ...（各阶段练习项目；主线规划已迁入 chathub/docs/项目规划/）
```

### 提交规范

| 前缀          | 用途     | 示例                              |
|-------------|--------|---------------------------------|
| `feat:`     | 新功能    | `feat: 添加 JWT 认证中间件`            |
| `fix:`      | 修复 bug | `fix: 修复 Session 析构后回调野指针`      |
| `test:`     | 测试     | `test: 添加消息分帧边界测试`              |
| `docs:`     | 文档     | `docs: 补充协议设计文档`                |
| `refactor:` | 重构     | `refactor: 提取消息路由到 LogicSystem` |
| `chore:`    | 工程配置   | `chore: 添加 CMake Presets`       |

每完成一周，创建一个 tag：`w01-git-debug` · `w02-lan-chat` · ... · `w12-review`

---

## 7. 协议与质量红线

### 聊天 TCP 协议设计约束

| 约束      | 说明                                                                |
|---------|-------------------------------------------------------------------|
| 定长头     | 至少包含 `magic`(2B) + `version`(1B) + `type`(2B) + `body_length`(4B) |
| 最大包长    | 定义上限（如 1MB），超过拒绝                                                  |
| 网络字节序   | 多字节字段统一 `network byte order`（`htonl`/`ntohl`）                     |
| 未知 type | 返回错误包，不静默丢弃                                                       |
| JSON 校验 | 所有字段做存在性 + 类型检查                                                   |
| 写队列     | 禁止并发 `async_write`，所有写操作进入队列                                      |

### 必须能回答的 5 个问题

| # | 问题                           | 涉及阶段  |
|---|------------------------------|-------|
| 1 | 为什么 TCP 会粘包/拆包，如何用长度字段解决？    | W3    |
| 2 | 为什么异步回调中要持有 `shared_ptr`？    | W4    |
| 3 | `strand` 解决什么问题，何时仍需要 mutex？ | W4    |
| 4 | JWT 为什么不能保存密码，过期和撤销如何处理？     | W5    |
| 5 | SQLite 的线程和写入限制会怎样影响服务器设计？   | W6/W9 |

---

## 8. llfcchat 正确对照方式

> **核心原则**：先写自己的最小实现 → 跑测试 → 只在卡住时读对应章节/代码 → 记录"我的方案与参考方案差异"。**不要按参考项目的 day01–42 线性照抄。**

| 你的阶段   | 对照 llfcchat 文档                           | 只看什么                   |
|--------|------------------------------------------|------------------------|
| W2     | day02 · day15                            | Qt HTTP/TCP 管理类职责与信号连接 |
| W3–W4  | day16 · day42                            | asio Session、读写回调、粘包处理 |
| W5–W6  | day04 · day05 · day08 · day14            | HTTP/JSON、认证服务的职责边界    |
| W7–W10 | day18–22 · day29 · day30 · day35 · day37 | UI 组织、聊天联调、难点复盘、心跳与存储  |
| W11–W12 | `MysqlDao` / `RedisMgr` 局部源码 | 事务、预编译、TTL、失败边界；不搬运全局管理器 |
| W14 | day25–29 + `LogicSystem` / 联系人 UI 局部源码 | 搜索、申请、授权、联系人与私聊入口 |
| V2     | day27 · day31–34 · day38–41              | 仅作后置 spike，不并入当前项目          |

---

## 9. V2 Backlog

> V1 交付后按收益排序。**每一项先提交一个独立 spike，验证可行后再并入主项目。**

| 优先级 | 功能                                | 涉及技术                                  |
|-----|-----------------------------------|---------------------------------------|
| 1   | Protobuf + gRPC 替换内部调用 | Protobuf · gRPC C++ · CompletionQueue |
| 2   | 文件传输：HTTP 上传 / 分片 / 校验和 / 断点续传 | QHttpMultiPart · 文件分片 |
| 3   | 多 ChatServer 水平扩展 | 负载均衡 · Redis 在线状态 · 跨服消息路由 |
| 4   | 分布式踢人逻辑 | 分布式锁 · gRPC NotifyKickUser |
| 5   | 🏆 **SerialScope** 串口上位机（第二简历项目） | QSerialPort · 波形渲染 · LTTB 降采样 |

### SerialScope 快速预览

```
SerialScope/                       # 4000~5400 行 · 14~16 类 · 纯 C++ Qt
├── core/
│   ├── SerialPortManager.h/.cpp  # QSerialPort + worker thread + 自动重连
│   ├── ProtocolParser.h/.cpp     # 状态机 · 3种帧格式 · 字节序
│   ├── RingBuffer.h              # 模板 · 无锁 · SPSC
│   ├── DataLogger.h/.cpp         # CSV/二进制 · 分段
│   └── ConfigManager.h/.cpp      # QSettings
├── ui/
│   ├── MainWindow.h/.cpp         # QMainWindow + QSplitter
│   ├── SerialConfigPanel.h/.cpp  # 串口参数 · 引脚状态
│   ├── TerminalWidget.h/.cpp     # 收发终端 · HEX/ASCII
│   ├── WaveformView.h/.cpp       # 实时波形 · LTTB · 双缓冲
│   └── ProtocolEditor.h/.cpp     # 可视化协议编辑
├── models/ + delegate/
└── resources/
```

| 挑战          | 解决方案                          |
|-------------|-------------------------------|
| 10万+数据点流畅渲染 | LTTB 降采样 + 增量绘制 + QPixmap 双缓冲 |
| 高波特率不丢数据    | RingBuffer 无锁队列 + 批量消费        |

**简历描述**：「独立开发串口调试与实时数据监控上位机，支持 3 种帧协议自动解析、8 通道实时波形显示（自研 QPainter 渲染引擎，LTTB 降采样支持 10 万数据点流畅渲染），环形缓冲区无锁设计保证 921600bps 高吞吐下 UI 零卡顿。」

---

## 10. 全技术栈清单

> 标记：`V1 必须` — ChatHub V1 交付前独立使用 · `V2 进阶` — V1 后按需 · `加分` — 不阻塞主线

---

### 10.1 C++ 语言与基础库

| 技能                                                          | 要求                     | 优先级   | 状态 |
|-------------------------------------------------------------|------------------------|-------|----|
| C++17 基础语法、类、继承、多态、异常                                       | 能写出职责清晰的业务类与错误处理       | V1 必须 | ✅  |
| RAII、值语义、Rule of Zero/Five                                  | 不手动管理普通资源；能说明析构时机      | V1 必须 | ✅  |
| 智能指针 `unique_ptr/shared_ptr/weak_ptr`                       | 正确管理 Session 和异步回调生命周期 | V1 必须 | ✅  |
| 移动语义、完美转发基础                                                 | 避免消息和容器的非必要复制          | V1 必须 | ✅  |
| STL：`vector/deque/queue/map/unordered_map/optional/variant` | 实现会话表、写队列、返回值和消息状态     | V1 必须 | ✅  |
| `string_view`、`span`、`chrono`、`filesystem`                  | 高效传参、计时、配置/日志文件处理      | V1 必须 | 🟡 |
| `enum class`、`constexpr`、`noexcept`、`explicit`              | 协议类型与接口边界清晰            | V1 必须 | ✅  |
| lambda、`std::function`、回调捕获规则                               | 阅读和编写 Qt/asio 回调       | V1 必须 | ✅  |
| 线程、mutex、condition_variable、atomic                          | 理解并发边界；避免盲目加锁          | V1 必须 | ❌  |
| C++20 coroutine：`co_await/co_return`                        | 重构一条异步调用链              | V2 进阶 | ❌  |
| concepts、ranges、模块                                          | 了解适用场景即可               | 加分    | ❌  |

### 10.2 工程化、构建与协作

| 技能                                             | 要求                            | 优先级   | 状态 |
|------------------------------------------------|-------------------------------|-------|----|
| Git：commit、branch、merge/rebase、tag、ignore、远程协作 | 每个功能有小提交；每周打 tag              | V1 必须 | 🟡 |
| CMake：target、链接、include、`find_package`、install | 能从空目录构建客户端/服务端                | V1 必须 | 🟡 |
| CMake Presets                                  | Debug/Release 与依赖配置可复现        | V1 必须 | ❌  |
| 依赖与配置管理                                 | 管理数据库客户端、环境变量和 Compose 依赖 | V1 必须 | ❌  |
| 编译器与诊断：MSVC、警告级别、AddressSanitizer              | 能处理警告，并定位内存越界/泄漏              | V1 必须 | ❌  |
| 格式化与静态分析：clang-format、clang-tidy               | 保持统一风格，处理关键告警                 | 加分    | 🟡 |
| CI：GitHub Actions 或 GitLab CI                  | 自动配置、构建、测试                    | V1 必须 | ❌  |
| 包管理/发布：CPack、NSIS 或 Inno Setup                 | 打包 Windows 客户端                | V2 进阶 | ❌  |

### 10.3 Qt 6 客户端

| 技能                                                 | 要求                       | 优先级   | 状态 |
|----------------------------------------------------|--------------------------|-------|----|
| Qt Core：QObject、信号槽、父子对象、事件循环                      | 能解释对象所有权和跨线程信号           | V1 必须 | ✅  |
| Qt Widgets、布局、QSS、资源系统                             | 登录页、主聊天页、错误状态完整可用        | V1 必须 | ✅  |
| Model/View/Delegate：`QListView/QAbstractListModel` | 渲染会话列表和消息列表              | V1 必须 | ✅  |
| QPainter、`paintEvent`、字体/尺寸计算                      | 实现自适应文本气泡                | V1 必须 | ✅  |
| Qt Network：`QNetworkAccessManager`、`QNetworkReply` | 调用注册/登录 REST API，处理超时与错误 | V1 必须 | ✅  |
| `QTcpSocket`、`QDataStream/QByteArray`              | TCP 连接、收发、缓存分帧、断线处理      | V1 必须 | ✅  |
| Qt Concurrent / `QThread` worker-object            | 不阻塞 UI；只在确有后台任务时使用       | V1 必须 | ✅  |
| `QTimer`、`QSettings`、`QStandardPaths`              | 心跳、偏好与本地配置               | V1 必须 | ✅  |
| `QSqlDatabase`                                     | 本地缓存需求明确时再使用             | V2 进阶 | ❌  |
| `QPropertyAnimation`、系统托盘、国际化                      | 完善体验                     | 加分    | ❌  |
| QML/Qt Quick                                       | 仅在决定转向声明式 UI 时学习         | 加分    | ❌  |

### 10.4 网络、协议与 C++ 服务端

| 技能                                                              | 要求                     | 优先级   | 状态 |
|-----------------------------------------------------------------|------------------------|-------|----|
| TCP/IP：端口、连接、可靠字节流、半关闭                                          | 能解释三次握手、粘包/拆包和断开事件     | V1 必须 | 🟡 |
| DNS、HTTP、TLS 的基本概念                                              | 知道域名解析、HTTPS 和证书各自解决什么 | V1 必须 | ❌  |
| 字节序、长度字段、版本化、最大包长                                               | 设计安全的应用层消息帧            | V1 必须 | 🟡 |
| JSON、UTF-8、错误码                                                  | 统一 API 和聊天消息的数据表示      | V1 必须 | 🟡 |
| Boost.Asio / standalone Asio：`io_context`、acceptor、socket、timer | 运行异步 ChatServer        | V1 必须 | 🟡  |
| `async_accept/read/write`、buffer、写队列                            | 正确处理多连接和背压             | V1 必须 | 🟡  |
| `strand`、executor、`post/dispatch`                               | 串行化 Session 状态访问       | V1 必须 | ✅  |
| JWT 签名验证（jwt-cpp/OpenSSL）                             | chat-server 验证客户端身份       | V1 必须 | 🟡  |
| `enable_shared_from_this`                                       | 防止异步回调访问已析构对象          | V1 必须 | 🟡 |
| Boost.Beast：HTTP request/response、路由、JSON                       | 实现最小 C++ Gateway       | V2 进阶 | ❌  |
| WebSocket                                                       | 了解握手及适用场景；聊天 V1 不强制使用  | V2 进阶 | ❌  |
| TLS：OpenSSL、证书、HTTPS/WSS                                        | 本地开发后接入加密传输            | V2 进阶 | ❌  |
| 负载均衡、服务发现、限流、熔断                                                 | 为多服务部署做准备              | 加分    | ❌  |

### 10.5 后端业务、认证与安全

| 技能                          | 要求                                 | 优先级   | 状态 |
|-----------------------------|------------------------------------|-------|----|
| REST：资源、方法、状态码、幂等性          | 设计注册、登录、健康检查接口                     | V1 必须 | 🟡  |
| Node.js：npm、模块、环境变量、异步模型    | 维护最小认证服务                           | V1 必须 | 🟡  |
| Express：路由、中间件、错误处理         | 编写 Auth API                        | V1 必须 | 🟡  |
| 密码安全：bcrypt/argon2、盐、成本因子   | 密码绝不明文存储或记录日志                      | V1 必须 | ✅  |
| JWT：签名、claims、`exp`、验证、密钥管理 | 登录授权与 ChatServer 握手验证              | V1 必须 | 🟡  |
| 输入校验与安全日志                   | 验证邮箱/用户名/长度/JSON，日志不泄漏 token/密码    | V1 必须 | 🟡  |
| CORS、CSRF、Cookie/Session    | 理解 Web 场景差异；桌面客户端以 Bearer token 为主 | V2 进阶 | ❌  |
| OAuth2/OIDC、刷新令牌、密钥轮换、权限模型  | 扩展登录方式和会话安全                        | V2 进阶 | ❌  |
| OWASP Top 10 基础             | 知道常见注入、越权和信息泄露风险                   | 加分    | ❌  |

### 10.6 数据存储与缓存

| 技能                             | 要求                                | 优先级   | 状态 |
|--------------------------------|-----------------------------------|-------|----|
| SQL：DDL、CRUD、索引、事务、约束          | 建模用户、会话、消息表                       | V1 必须 | ❌  |
| SQLite：连接、预编译语句、WAL、备份         | 管理 `users.sqlite` 与 `chat.sqlite` | V1 必须 | ❌  |
| C/C++ SQLite API 或封装库          | ChatServer 持久化消息，不拼接 SQL          | V1 必须 | ❌  |
| 数据建模、迁移脚本、时间戳                  | 表结构可演进，可重建测试数据                    | V1 必须 | ❌  |
| Redis：string/hash、TTL、原子计数、连接失败处理 | 登录限流、受控 JWT 撤销；不把聊天正文放入 Redis | V1 必须 | ❌  |
| MySQL                           | Repository 集成、索引、事务与初始化脚本           | V1 必须 | ❌  |
| PostgreSQL                      | 了解关系库迁移的原因和基本操作                     | 加分    | ❌  |
| 消息队列（RabbitMQ/Kafka）           | 仅在异步任务/削峰需求明确后引入                  | 加分    | ❌  |

### 10.7 服务间通信与序列化

| 技能                                         | 要求                     | 优先级   | 状态 |
|--------------------------------------------|------------------------|-------|----|
| JSON Schema/手工字段校验                         | 保证 HTTP 与 TCP 载荷结构正确   | V1 必须 | 🟡 |
| Protocol Buffers：`.proto`、message、生成代码、兼容性 | 定义可演进的内部消息             | V2 进阶 | ❌  |
| gRPC：unary RPC、stub、deadline、status        | 用一个内部查询服务练习            | V2 进阶 | ❌  |
| gRPC C++ CompletionQueue / async API       | 只有同步 gRPC 熟练后再学        | 加分    | ❌  |
| Node `@grpc/grpc-js`                       | 需要 Node 与 C++ 直接通信时再加入 | 加分    | ❌  |
| WebSocket / SSE                            | 浏览器端或服务端推送需求出现时选择      | 加分    | ❌  |

### 10.8 测试、调试、可观测性与交付

| 技能                                  | 要求                     | 优先级   | 状态 |
|-------------------------------------|------------------------|-------|----|
| 单元测试：GoogleTest 或 Catch2 + CTest    | 测协议编解码、路由、数据库查询        | V1 必须 | 🟡 |
| 集成测试                                | 自动启动依赖并验证注册→登录→聊天闭环    | V1 必须 | ❌  |
| 网络测试：netcat、Postman/Bruno、Wireshark | 发送异常包，检查 HTTP/TCP 实际数据 | V1 必须 | 🟡 |
| 调试：断点、调用栈、线程窗口、日志定位                 | 能定位 UI/网络/崩溃问题         | V1 必须 | 🟡 |
| 结构化日志、日志等级、请求/连接 ID                 | 跨服务关联一次登录或消息流          | V1 必须 | ❌  |
| 性能与压测：并发连接、消息吞吐、内存占用                | 给出一组可复现的压测结果           | V2 进阶 | ❌  |
| Sanitizer、Valgrind（Linux）、Fuzzing   | 提前发现内存和协议解析缺陷          | V2 进阶 | ❌  |
| Docker、docker-compose、Linux systemd | 让服务可部署、可重复启动           | V2 进阶 | ❌  |
| 监控：Prometheus/Grafana               | 连接数、延迟、错误率可观测          | 加分    | ❌  |

### 10.9 设计与职业表达

| 技能                      | 要求                  | 优先级   | 状态 |
|-------------------------|---------------------|-------|----|
| 分层与模块边界                 | UI、网络、协议、业务、存储不相互混杂 | V1 必须 | ✅  |
| SOLID、依赖倒置、接口隔离         | 只在重构点实际应用，不为模式而模式   | V1 必须 | 🟡 |
| 常用模式：观察者、工厂、策略、PIMPL    | 能解释项目内的实际使用点        | V2 进阶 | 🟡 |
| 架构图、时序图、README、API/协议文档 | 陌生人能运行并理解项目         | V1 必须 | ❌  |
| 项目复盘与面试表达               | 清楚讲架构、难点、取舍、指标与改进   | V1 必须 | ❌  |

---

## 11. 基于已有笔记的进度校准

### 11.1 已有能力（不再重复学习）

以下来自学习总目录、07-15 阶段复盘以及密码本项目评价；后续项目直接使用，不再安排"看教程式复习"。

| 能力层        | 已有证据                                              | 在 ChatHub 中的直接用途                  |
|------------|---------------------------------------------------|-----------------------------------|
| Qt 交互与对象模型 | 自定义信号槽、lambda、QDialog、事件过滤器、父子对象                  | 登录页、状态提示、断线事件与页面切换                |
| UI 架构与布局   | 三栏 `QSplitter`、Grid/Form/V/H 嵌套、QMainWindow 中央控件  | 主聊天界面、会话侧栏与聊天区                    |
| 状态管理       | 计算器连续运算、番茄钟工作/休息切换                                | 登录中/在线/重连中/离线等客户端状态               |
| Model/View | 密码本使用 `QSortFilterProxyModel`、角色数据、`mapToSource`  | 会话列表过滤、消息模型和气泡委托                  |
| 文件与本地数据    | CSV 导出、QFile/QTextStream/QDataStream、QSettings 笔记 | 配置、日志、导出聊天记录和本地缓存                 |
| 代码组织       | 构造函数只调度 `setupXxx()`、UI/逻辑分模块                     | ChatHub 的 client/server/auth 模块边界 |

### 11.2 "有笔记"不等于"已掌握"——5 个验证实验

详细笔记已覆盖 QThread、QNetworkAccessManager、QTcpSocket、QSslSocket、文件 I/O 与资源系统，但没有对应的完成项目记录。用下面 5 个小实验验收后才可改为 ✅。

| # | 实验                   | 验收标准                                               | 状态      |
|---|----------------------|----------------------------------------------------|---------|
| 1 | `01-git-debug-lab`   | 6+ 语义化提交；独立定位一个编译错误 + 一个运行时错误                      | ✅ 07-18 |
| 2 | `02-adaptive-bubble` | QPainter + QFontMetrics 绘制文本气泡；中英文/emoji/多行不裁切     | ✅ 07-20 |
| 3 | `03-settings-export` | QSettings 保存窗口尺寸/服务器地址 → 重启恢复；UTF-8 CSV 不乱码        | ✅ 07-20 |
| 4 | `04-thread-worker`   | worker-object + moveToThread；进度条 + 取消；UI 不卡；线程安全回收 | ✅ 07-20 |
| 5 | `05-lan-chat`        | QTcpServer/QTcpSocket TCP 文件传输；协议头+分块+流控+半包        | ✅ 07-21 |

### 11.3 三项补强原则

| # | 原则             | 做法                                              |
|---|----------------|-------------------------------------------------|
| 1 | **先独立架构，再写实现** | 每个新项目先提交 `docs/architecture.md`：职责表、类图、消息流、失败路径 |
| 2 | **把调试变成固定交付**  | 每周复盘记录一个真实 bug："现象 → 最小复现 → 根因 → 修复 → 防回归测试"    |
| 3 | **按证据更新掌握度**   | 使用"能改、能讲、能迁移"标准，不以阅读量或代码行数判断进度                  |

---

## 12. 如果卡住了

```
W1–W2 — Qt/C++ 编译或链接错误
  → 检查 CMakeLists.txt 的 find_package + target_link_libraries
  → MinGW 和 MSVC 的库不能混用；Qt 必须用 MinGW 版
  → 读 cmake --build build 的完整输出，不要只看最后一行

W3–W4 — async_read 一直不返回 / 回调不触发
  → 检查 io_context 是否在 run()（没 run() 什么事都不会发生）
  → 检查 socket 是否还 alive（async_read 中途断开 = 永久挂起）
  → 检查 shared_from_this 是否正确继承

W3–W4 — 客户端断开时服务器崩溃
  → 99% 是回调中使用了已被 delete 的 Session
  → 用 shared_from_this() + error_code 检查（on_error 里清理）

W5–W6 — Node.js 服务启动失败 / 端口占用
  → 检查端口是否被占用：netstat -ano | findstr :3000
  → npm install 是否完整执行

W5–W6 — JWT verify 失败
  → 检查 secret 密钥在 Auth Service 和 ChatServer 是否一致
  → 检查 token 是否过期（exp claim）

W6 — 启动脚本 chat-server 起不来："不是内部或外部命令"
  → 批处理里 "cd /d X && exe" 在嵌套 cmd /k 下不可靠，exe 找不到
  → 改用完整路径启动：start "chat-server" cmd /k "X\chat-server.exe"
  → .bat 内中文会乱码（GBK 代码页），文件保持纯 ASCII 最稳
  → jwt.io 粘贴 token 看 payload 是否正确

W7–W10 — TCP 连上了但消息收不到
  → 先用 netcat/telnet 发测试包，确认服务器单独没问题
  → 检查消息分帧：发送端和接收端的长度字段字节序是否一致
  → 检查 JSON 解析是否静默失败

W7 — Qt 客户端连上后服务端报"协议错误"（magic 截断）
  → 服务端日志出现"协议错误，关闭当前连接"，但帧格式看着对
  → 原因：quint16 的 magic (0x4348) 用 static_cast<char> 写帧，被截断成 1 字节 0x48
  → 8 字节协议头只写出了 7 字节，服务端读 magic 对不上 0x4348
  → 修复：分高/低字节写 frame.append(static_cast<char>(kMagic>>8)); frame.append(static_cast<char>(kMagic & 0xFF));

W7–W10 — Qt 界面卡死
  → 网络操作是否在主线程？用 QThread 或 moveToThread
  → 大量数据是否在主线程处理？考虑批量更新 + QTimer::singleShot

W11–W13 — MySQL / Redis / Compose 启动失败
  → 先确认数据库和 Redis 健康检查，再检查应用连接串、环境变量和端口映射
  → 使用独立测试库；不要让 CTest 连接真实开发数据，更不要把密码提交进仓库

通用原则:
  → 卡住 30min → 缩小范围（注释掉一半，二分定位）
  → 卡住 2h   → 看 llfcchat 对应章节 + 问我（带错误信息 + 已尝试方案）
  → 不要跳过难题 —— W3 的问题会在 W8 放大 10 倍
  → 每周末复盘记录一个 bug 的完整生命周期
```

---

## 13. 学习记录

> 每次学习后追加一行：日期 · 内容 · 耗时 · 产出/收获

| 日期    | 周次 | 内容                                                               | 耗时   | 产出                                             |
|-------|----|------------------------------------------------------------------|------|------------------------------------------------|
| 07-18 | W1 | 路线整合：三文件 → 完善版 + 两个索引桩                                           | 1h   | 完善版 v1                                         |
| 07-18 | W1 | `01-git-debug-lab`：Git 基础 + 破坏→修复×3                              | 1.5h | 11 commits · feature branch                    |
| 07-18 | W1 | `02-modern-cpp`：enum class/constexpr/string_view/optional + 三个变式 | 1h   | 8 commits · experiment/modern-cpp              |
| 07-20 | W1 | Qt 容器 vs STL 对比                                                  | 0.5h | 笔记                                             |
| 07-20 | W2 | `02-adaptive-bubble`：QPainter + QFontMetrics 自适应聊天气泡             | 1.5h | QT/adaptive-bubble/ · 笔记                       |
| 07-20 | W2 | `03-settings-export`：QSettings + QFileDialog + UTF-8 CSV 导出      | 1h   | QT/week2/settings-export/ · 笔记                 |
| 07-20 | W2 | `04-thread-worker`：moveToThread + QAtomicInt + DirectConnection  | 2h   | cpp-learn/week2/04-thread-worker · 笔记 · 5 bugs |
| 07-21 | W2 | `05-lan-chat`：TCP 文件传输（QTcpServer/QTcpSocket/协议/分块/流控）  | 2h   | cpp-learn · 笔记 · 12知识点+12待学                |
| 07-27 | W2 | `06-http-client`：QNetworkAccessManager HTTP GET/POST 封装 + 超时 + JSON 校验 | 1.5h | cpp-learn · 合并笔记：TCP+HTTP 统一笔记 |
| 07-28 | W3 | `01-tcp-concepts`：TCP 分帧缓存 + 独立 Asio `io_context` / `steady_timer` | 1h | 半包/粘包实验 · 两定时器约 5.01 秒验证 · Asio 笔记 |
| 07-30 | W3 | `01-tcp-concepts`：Session `steady_timer` 心跳与 pong 超时 | 1h | 两次 pong 保活 · 静默客户端超时关闭 · `tcp_server` 构建通过 |
| 07-30 | W3 | `01-tcp-concepts`：Session 写队列背压 | 1h | 1 KiB 接收缓冲慢客户端 · `3 / 3` 上限关闭 · 服务端可继续接收连接 |
| 07-30 | W3 | `01-tcp-concepts`：`operation_aborted` 与优雅关闭 | 0.5h | 队列关闭无读取失败日志 · EOF 正常断开 · 自动化双场景验证 |
| 07-31 | W3/W4 | `01-tcp-concepts`：FrameDecoder CTest 与项目进度校准 | 0.5h | 7 个分帧/边界场景通过 · 修复测试退出码 · 全量构建通过；W3 85%、W4 65%（均为学习中） |
| 08-02 | W5 | `auth-service`：HTTP 原理 + Express 路由/中间件 | 1h | GET /health、/ 接口；express.json()、res.status().json() |
| 08-02 | W5 | `auth-service`：SQLite 用户表 + better-sqlite3 | 1h | 建表/查询/插入；唯一约束 409；? 占位符防注入；密码占位符练习 |
| 08-02 | W5 | `auth-service`：bcrypt 密码哈希 + 登录接口 | 1h | 注册→登录闭环；哈希存储；统一 401 防枚举；5 种情形验收通过 |
| 08-02 | W5 | `auth-service`：JWT 签发/验证 + exp 过期 | 1.5h | /me 受保护接口；Bearer token；过期/无效/未提供区分；7 项测试全过；W5 80% |
| 08-03 | W5 | `auth-service`：JWT 密钥改环境变量（dotenv） | 0.5h | .env + gitignore；有/无 .env 两情形验证通过；密钥不进代码库 |
| 08-03 | W5 | `auth-service`：输入校验加强 | 1h | validateCredentials 抽函数、register/login 共用；长度/字符集正则；6 情形验证通过；W5 100% 收官 |
| 08-04 | W6 | `chathub`：三仓从零搭建 + auth-service + client-qt | 4h | chat-server 广播、auth-service 注册登录 JWT、client-qt 登录页真实 HTTP；GitHub ChatHub |
| 08-04 | W7 | `chathub`：chat-server JWT 认证（jwt-cpp） | 2h | auth=5 类型、认证状态机、verifyJwt；好token通过/坏token断开/未认证断开；接入第三方库排障 |
| 08-06 | W8 | `chathub`：消息状态机端到端验收 | 1h | A→B 显示发送者和时间；B 离线出现失败红点；重登后复用 local_id 重试成功；关闭 chat-server 后发送按钮禁用 |
| 08-07 | W8 | `chathub`：本地会话列表点击切换 | 0.5h | `itemClicked → m_currentPeer → renderCurrentConversation()`；Bob/Carol 会话可反复切换且仅显示各自消息，构建与人工验收通过 |
| 08-07 | W8 | `chathub`：发送目标回退与前端输入校验 | 0.3h | 手填接收者优先，空时回退 `m_currentPeer`；无目标或空白正文在 UI 层提示并返回，每次有效点击仅发送一次，构建与审查通过 |
| 08-07 | W8 | `chathub`：V1 稳定性优化 | 1.5h | 发送消息先预建模；服务端校验失败保留 local_id；认证超时/密钥启动检查；HTTP 输入边界与 CTest 回归通过；仍为学习中，待独立迁移到新协议场景 |
| 08-10 | W8 | `chathub`：会话未读计数 | 0.5h | `m_unreadCounts` 与 `UserRole` 稳定身份分离；Bob `(2)`、点击清零、当前会话直显人工验收；构建和 CTest 2/2 通过 |
| 08-10 | W8 | `chathub`：会话最后消息预览 | 1h | 摘要由 `m_conversations` 推导；换行/截断/代理对边界处理；收发与补建路径统一刷新，人工验收及 CTest 2/2 通过 |
| 08-10 | W8 | `chathub`：会话时间摘要 | 0.5h | 最后消息 `send_at` 统一转本地时间后按今天/昨天/同年/跨年格式化；无效时间不显示分隔符；未读清零保持时间与预览，人工验收及 CTest 2/2 通过 |
| 08-11 | W8 | `chathub`：最终送达回执 | 2h | `delivery_receipt=7`、服务端待送达索引与认证归属校验、断线清理、B 完成本地处理后回执、A 更新既有气泡为“已送达”；双客户端人工验收，构建与 CTest 2/2 通过 |
| 08-12 | W8 | `chathub`：在线用户快照 | 1.5h | `online_users=8`、Server strand 上的认证映射快照与同名登录接管、客户端严格 JSON 校验和缓存回填、Qt 在线列表点击回填；双客户端验收、构建与 CTest 2/2 通过 |

---

## 14. 成功标准

10 月 11 日前交付的不是"学过一堆技术名词"，而是：

1. 一个陌生人可按 README 运行 ChatHub V1
2. 两台客户端可注册登录和聊天
3. 重启后仍可查看历史
4. 面对坏数据不崩溃
5. **你能在 5 分钟内清楚解释它的架构、协议、线程模型和三个踩坑**

---

> **下一步：W1 任务 2 — `enum class` / `constexpr` / `string_view` / `optional`**
