# ChatHub chat-server 搭建踩坑复习

> 2026-08-03 | 从零搭建 chat-server（协议层 → 网络层 → 广播）
> 对应代码：`D:\CppLearn\chathub\chat-server\`

---

## 一、C++ 语法 / 语义坑

### 1. 逗号 vs 点（编译错误）

```cpp
// ❌ 错误：逗号表达式（m_cache 和 size 是两个表达式），编译不过
while (m_cache,size >= kHeaderSize)

// ✅ 正确
while (m_cache.size() >= kHeaderSize)
```

**教训**：`.` 和 `,` 长得像，敲错是典型笔误。报错说 `size` 未定义时，先检查是不是把 `.` 敲成了 `,`。

### 2. 重复 static 关键字

```cpp
// ❌ 编译错误：duplicate 'static' specifier
static static void log(...);

// ✅ 只写一个
static void log(...);
```

### 3. 构造函数参数没写名字

```cpp
// ❌ 参数没名字，函数体里用不了
Session::Session(asio::ip::tcp::socket) : m_socket(std::move(socket)) {}

// ✅ 必须写参数名
Session::Session(asio::ip::tcp::socket socket) : m_socket(std::move(socket)) {}
```

**教训**：报 `socket 未定义` 时，检查构造函数签名是否漏了参数名。

---

## 二、类型 / 逻辑坑（最严重）

### 4. std::function 塞进 bool（严重 bug）

```cpp
// ❌ 把 on_message（std::function）塞进 m_disconnected（bool）
m_disconnected(std::move(on_message))

// ✅ 应该是
m_on_disconnect(std::move(on_disconnect))
```

**后果**：`std::function` 有 bool 转换，能编译。但 `m_disconnected` 被设成 true，导致 `close()` 里 `if (m_disconnected) return;` **永远直接返回，不通知 Server 移除在线表** → 广播发给死连接。

**教训**：构造函数的初始化列表要**一个一个对着成员写**，不能想当然。这种 bug 编译不报错，运行才暴露，最难查。

### 5. 写队列 push 忘了 makeFrame

```cpp
// ❌ 把裸 body 塞进 frame 字段
m_write_queue.push_back({type, body});

// ✅ 先编码成完整帧
m_write_queue.push_back({type, protocol::makeFrame(type, body)});
```

**后果**：`async_write` 写的是没有协议头的裸 body，客户端 FrameDecoder 解析失败。

**教训**：`WriteItem` 的 `frame` 字段语义是"编码好的完整帧"，push 时一定要 makeFrame。

### 6. doSend 启动写逻辑反了

```cpp
// ❌ 永远非空，每次 send 都启动一次 doWrite（可能并发写）
m_write_queue.push_back(...);
if (!m_write_queue.empty()) { doWrite(); }

// ✅ 记录"空→非空"时刻，只有这一次启动写
const bool was_empty = m_write_queue.empty();
m_write_queue.push_back(...);
if (was_empty) { doWrite(); }
```

**为什么**：同一 socket 同时只能有一个 `async_write`。写完成回调会自己继续下一条，只有"空→非空"这一次需要主动启动。

---

## 三、CMake 坑

### 7. target_link_directories vs target_include_directories

```cpp
// ❌ 头文件搜索要用 include，不是 link
target_link_directories(chat-server PRIVATE ${ASIO_INCLUDE_DIR})

// ✅ 找 #include 头文件用这个
target_include_directories(chat-server PRIVATE ${ASIO_INCLUDE_DIR})
```

**记忆**：
- `include_directories` → 找头文件（`#include <asio.hpp>`）
- `link_directories` → 找库文件（`.lib`/`.a`）
- `link_libraries` → 链接库

### 8. add_executable 引用了不存在的文件

```cpp
// ❌ 文件还不存在，构建报 "No such file or directory"
add_executable(chat-server app/main.cpp src/net/server.cpp ...)
```

**教训**：**骨架阶段先只加 main.cpp**，写完一个模块再加回一个，别一步到位。

### 9. add_test 找不到测试

```cpp
// 根 CMakeLists 要加 enable_testing()，add_test 才生效
enable_testing()
```

### 10. include 路径抽变量（DRY）

```cmake
set(CHAT_SERVER_INCLUDES
    ${ASIO_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(chat-server PRIVATE ${CHAT_SERVER_INCLUDES})
target_include_directories(frame_decoder_test PRIVATE ${CHAT_SERVER_INCLUDES})
```

两个目标共用同一路径，改一处即可。

---

## 四、并发 / asio 概念

### 11. strand 是"独木桥"

> 同一 strand 里的回调串行执行，不会同时跑。保护 Session 的 m_decoder/m_write_queue 不用加锁。

### 12. bind_executor 是"入口检查"

> async_read/write 的完成回调默认可能在任意 worker 线程执行。`bind_executor(m_strand, cb)` 让回调触发时先进 strand 排队。

### 13. buffer 不拥有数据

> `asio::buffer(frame)` 只是指针+长度的视图，不拷贝。**frame 必须活到 async_write 完成**——这就是写队列存在的原因，`pop_front` 放完成回调里。

---

## 五、Git / 环境坑

### 14. 代理连不上 GitHub

```
fatal: unable to access ... Could not connect to server
```

**处理**：MyClash 进程在但核心端口没监听。启动代理软件后重试 `git push`。

### 15. 残留 .env 文件差点入库

```
AM week5/auth-service/process.env   ← 含密钥的残留文件
```

**处理**：`git rm --cached -f` 移除 + 确认 `.gitignore` 覆盖 `*.env`。

---

## 六、命名规范（直译）

| 旧命名 | 新命名 | 含义 |
|--------|--------|------|
| `PendingWrite` | `WriteItem` | 待写项 |
| `ServerMessageHandler` | `MessageCallback` | 消息回调 |
| `DisconnectHandler` | `DisconnectCallback` | 断开回调 |
| `doSend` | `enqueueAndWrite` | 入队并写 |
| `doWrite` | `writeFrame` | 写帧 |
| `handleMessage` | `processMessage` | 处理消息 |

**原则**：动词短语命名比抽象 `doXxx` 更直译；长度概念统一用 `Length`（`kHeaderLength`/`kMaxBodyLength`/`body_length`）。

---

## 七、广播实现要点（今天核心成果）

```
客户端A 发 "Hello"
  → SessionA::doRead → FrameDecoder 解码
    → processMessage → m_on_message(m_id, message)   [SessionA strand]
      → Server on_message lambda
        → asio::post(Server strand)                  [跨 strand]
          → onSessionMessage → broadcast
            → 遍历在线表，跳过 A，发给 B/C
```

| 设计 | 为什么 |
|------|--------|
| 在线表 `unordered_map<SessionId, shared_ptr>` | ID 好哈希 + shared_ptr 管生命周期 |
| 回调带 sender_id | 广播才能跳过发送者 |
| 回调里 `post(m_strand)` | Session strand → Server strand，安全访问在线表 |
| `id != sender_id` | A 的消息不给 A 自己 |

---

## 八、今天验收证据

| 项 | 结果 |
|----|------|
| FrameDecoder CTest | ✅ 1/1（半包/粘包/非法magic/超长） |
| 多连接 echo | ✅ 两客户端独立 echo |
| 广播 | ✅ A发→B/C收，A跳过 |
| 断开处理 | ✅ 移除在线表，互不影响 |
| 命名重构 + 注释 | ✅ 构建过，CTest 仍过 |

---

# W5 Auth 服务踩坑复习（Express + SQLite + bcrypt + JWT）

> 对应代码：`D:\CppLearn\chathub\auth-service\`

---

## 一、Node.js 模块坑

### 1. require 路径多写 `./`

```js
// ❌ bcryptjs 是 npm 包，带 ./ 会去找本地文件 → 报"路径不存在"
const bcrypt = require('./bcryptjs');

// ✅ npm 包不带 ./，本地文件才带
const bcrypt = require('bcryptjs');
const db = require('./db');   // db.js 是本地文件，要带 ./
```

**记忆**：`require('包名')` = 去 node_modules 找；`require('./文件')` = 当前目录找。

---

## 二、Express 坑

### 2. res.status().json() 不 return（响应发两次）

```js
// ❌ 校验失败后没有 return，继续执行下面的成功响应
if (!username) {
    res.status(400).json({ error: '...' });
}
res.json({ message: '注册成功' });   // 又发一次 → ERR_HTTP_HEADERS_SENT

// ✅ return 提前退出
if (!username) {
    return res.status(400).json({ error: '...' });
}
res.json({ message: '注册成功' });
```

**教训**：Express 里"校验失败就返回"是高频模式，**必须 `return`**，否则响应发两次崩溃。

### 3. 校验函数不能当中间件用（请求卡死）

```js
// ❌ validateCredentials 返回字符串，不调 next()，express 不知道它完成了
app.post('/register', validateCredentials, handler);

// ✅ 在 handler 内部调用，判断返回值
app.post('/register', (req, res) => {
    const error = validateCredentials(username, password);
    if (error) return res.status(400).json({ error });
    // 继续...
});
```

**中间件必须做两件事之一**：发响应 或 调 `next()`。普通函数两件都不做 → 请求挂起。

---

## 三、better-sqlite3 坑

### 4. 同步 API，冲突直接 throw

```js
// better-sqlite3 是同步的，重复 username 插入时 insert.run() 直接 throw
// 必须 try/catch，不能用 if(err) 判断
try {
    insertStmt.run(username, hash);
    return res.status(201).json({ message: '注册成功' });
} catch (err) {
    if (err.code === 'SQLITE_CONSTRAINT_UNIQUE') {
        return res.status(409).json({ error: '用户名已存在' });
    }
    return res.status(500).json({ error: '服务器内部错误' });
}
```

**记忆**：better-sqlite3 同步 API = 出错 throw（像 C++ 的异常）；express 回调 = 出错返回错误对象。

### 5. SQL 字符串拼接缺空格

```js
// ❌ *FROM 和 usersWHERE 缺空格
db.prepare(`SELECT *FROM usersWHERE username = ?`);

// ✅ 空格不能少
db.prepare(`SELECT * FROM users WHERE username = ?`);
```

**教训**：SQL 是字符串，**每个关键字间都要空格**。`*FROM`、`usersWHERE` 这种笔误会导致 SQL 语法错误或表名错误。

### 6. SQL 注入 vs ? 占位符

```js
// ❌ 字符串拼接，攻击者输入 '; DROP TABLE users; -- 能删表
db.exec("SELECT * FROM users WHERE username = '" + username + "'");

// ✅ ? 占位符，数据库把值当数据不当代码
db.prepare('SELECT * FROM users WHERE username = ?').get(username);
```

---

## 四、bcrypt / JWT 坑

### 7. 密码绝不能存明文

```js
// ❌ 明文，拿库就全泄露
insertStmt.run(username, password);

// ✅ bcrypt 单向哈希，10 = 复杂度（盐+哈希）
const hash = bcrypt.hashSync(password, 10);
insertStmt.run(username, hash);
```

### 8. 登录统一返回 401（防用户枚举）

```js
// ❌ 区分"用户不存在"和"密码错误" → 攻击者可探测用户名
// ✅ 统一返回一样的 401
if (!user || !bcrypt.compareSync(password, user.password_hash)) {
    return res.status(401).json({ error: '用户名或密码错误' });
}
```

### 9. jwt.sign 不抛错，jwt.verify 才抛错

```js
// ✅ sign 登录时签发，不抛错，不用 try/catch
const token = jwt.sign({ username }, SECRET_KEY, { expiresIn: '1h' });

// ✅ verify 每次验证会抛错（过期/篡改），必须 try/catch
try {
    const decoded = jwt.verify(token, SECRET_KEY);
} catch (err) {
    if (err.name === 'TokenExpiredError') { /* 过期 */ }
    // 其他 JsonWebTokenError → 无效
}
```

**记忆**：`sign` 处理自己生成的数据（可信），`verify` 处理客户端带来的数据（不可信）→ verify 必须 try/catch。

### 10. token 是字符串，不是对象

```js
// ❌ token.expiration 是 undefined（token 是字符串 "eyJ..."）
if (token.expiration < Date.now()) { ... }

// ✅ 过期看 verify 抛的异常类型
catch (err) {
    if (err.name === 'TokenExpiredError') { return 401; }
}
```

### 11. /me 先检查头存在，再取 token

```js
// ❌ authHeader 是 undefined 时，undefined.split 崩溃
const token = req.headers.authorization.split(' ')[1];

// ✅ 先判断头存在 + 格式
const authHeader = req.headers.authorization;
if (!authHeader || !authHeader.startsWith('Bearer ')) {
    return res.status(401).json({ error: '未提供有效的授权信息' });
}
const token = authHeader.split(' ')[1];
```

**记忆**：任何 `.split`/`.length` 之前，先确认对象不是 undefined/null。

---

## 五、环境变量坑

### 12. dotenv 必须放最顶部

```js
// ✅ 第一行，后面代码才能读到 process.env.JWT_SECRET
require('dotenv').config();
const SECRET_KEY = process.env.JWT_SECRET || 'dev-secret';
```

### 13. .env 必须 gitignore

```text
# .gitignore
*.env     # 覆盖 .env 和 process.env 这类文件
```

**教训**：密钥进 git = 泄露。`git rm --cached -f` 移除已误加的。

---

## 六、输入校验坑

### 14. 先判空，再取 length

```js
// ❌ username 是 undefined 时，undefined.length 崩溃
if (username.length < 3) { ... }

// ✅ 先 !username 判空，再取 length
if (!username || username.length < 3) { ... }
```

### 15. 校验结果必须判断

```js
// ❌ 调了但不用返回值，校验形同虚设
validateCredentials(username, password);

// ✅ 接住返回值，非 null 就 400
const error = validateCredentials(username, password);
if (error) return res.status(400).json({ error });
```

### 16. 正则校验用户名

```js
// ^ 开头 $ 结尾 [ ] 字符集 + 一个或多个
// 只能含字母数字下划线
if (!/^[a-zA-Z0-9_]+$/.test(username)) { return 400; }
```

---

## 七、W5 验收证据

| 项 | 结果 |
|----|------|
| 注册 alice | ✅ 201 |
| 重复注册 | ✅ 409 |
| 登录正确 | ✅ 200 + token |
| /me 带 token | ✅ 200 |
| /me 无/篡改/过期 token | ✅ 401 三种区分 |
| 密码存储 | ✅ bcrypt 哈希非明文 |
| 输入校验 | ✅ 长度/字符集 6 情形 |
| .env 有无两情形 | ✅ 都正常跑 |

---

## 八、W5 核心概念速记

| 概念 | 一句话 |
|------|--------|
| 中间件 | 请求到达路由前先处理，必须 `next()` 或发响应 |
| 幂等 | 重复执行无副作用；POST 不幂等 |
| 同步 vs 异步 | better-sqlite3 同步（throw），express 异步（回调） |
| 占位符 | `?` 防 SQL 注入，数据库把值当数据 |
| bcrypt | 单向哈希，无法解密，登录用 compareSync |
| JWT | sign 签发不抛错，verify 验证会抛错（try/catch） |
| Bearer | token 放请求头 `Authorization: Bearer xxx`，不放 URL |
| 统一 401 | 防用户枚举 |
| dotenv | 环境变量文件，gitignore 排除 |
