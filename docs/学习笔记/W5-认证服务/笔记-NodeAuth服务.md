# 笔记：Node.js Auth 服务 — Express + SQLite + bcrypt

> 2026-08-02 | W5 | `chathub/auth-service/`
> 状态：🟡 学习中。注册→登录→/me 全链路 + JWT 已跑通；待输入校验加强。

---

## 一、HTTP 原理（回顾）

- 请求 = 方法 + 路径 + 头 + 空行 + 体；响应 = 状态行 + 头 + 体
- 幂等：GET/PUT/DELETE 重复执行无副作用；POST 不幂等（重复注册会建多个账号）
- REST：用方法表达动作，URL 表达资源

| 方法 | 语义 | 幂等 |
|------|------|------|
| GET | 读 | ✅ |
| POST | 创建 | ❌ |
| PUT/PATCH | 改 | ✅ |
| DELETE | 删 | ✅ |

状态码：200 OK · 201 Created · 400 Bad Request · 401 Unauthorized · 403 Forbidden · 404 Not Found · 409 Conflict · 500 Internal Server Error

---

## 二、Express 路由与中间件

### 中间件是什么

中间件 = 在请求到达路由前先处理一步的函数。`app.use(express.json())` 解析 JSON body 到 `req.body`。

```js
app.use((req, res, next) => {
    // 处理...
    next();   // 必须调用，否则请求卡住
});
```

### 关键写法

```js
const app = express();
app.use(express.json());   // 放最上面，路由之前

app.get('/health', (req, res) => res.send('OK'));
app.post('/register', (req, res) => {
    const { username, password } = req.body;
    if (!username || !password) {
        return res.status(400).json({ error: '用户名和密码不能为空' });
    }
    return res.status(201).json({ message: '注册成功' });
});
```

### 三个必会要点

1. **`express.json()` 不加则 `req.body` 是 `{}`/undefined**（不同版本行为不同，实测本版本是 `{}`）
2. **校验失败必须 `return res.status(...).json(...)`** —— 不加 `return` 会继续往下执行，响应发送两次 → `ERR_HTTP_HEADERS_SENT`
3. **统一错误码**：400 缺参数 · 401 认证失败 · 409 冲突 · 500 服务器错误

---

## 三、Node.js 模块系统

| 写法 | 含义 | 查找位置 |
|------|------|---------|
| `require('bcryptjs')` | 第三方包 | `node_modules/` |
| `require('./db')` | 本地文件 | 当前目录 |
| `require('../db')` | 上级文件 | 相对路径 |

**踩坑**：npm 包名**不能带 `./`**。`require('./bcryptjs')` 会找本地文件，报"路径不存在"。

- `module.exports = db` 导出，`require('./db')` 导入
- `package.json` 的 `"main"` 指定入口，`"scripts.start"` = `node src/server.js`

---

## 四、better-sqlite3（同步 API）

### 与 express 的区别

| | express | better-sqlite3 |
|---|---|---|
| API | 异步 | **同步**（直接返回结果） |
| 出错 | 回调 | **直接 throw**，必须 try/catch |

### 核心方法

```js
const Database = require('better-sqlite3');
const db = new Database('auto.db');   // 打开/创建单文件库

db.exec('CREATE TABLE IF NOT EXISTS ...');   // 建表（可重复执行）
db.prepare('SELECT * FROM users WHERE username = ?').get(username);   // 查一行，无则 undefined
db.prepare('INSERT INTO users (username, password_hash) VALUES (?, ?)').run(username, hash);  // 插入
```

### SQL 注入：为什么用 `?`

字符串拼接：`"WHERE username = '" + username + "'"` → 攻击者输入 `'; DROP TABLE users; --` 能删表。
`?` 占位符把值当数据不当代码，安全。**数据库逻辑永远用预编译语句**。

### 唯一约束与异常

- 表里 `username TEXT NOT NULL UNIQUE`，重复插入会 **throw** `SQLITE_CONSTRAINT_UNIQUE`
- 必须 try/catch：

```js
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

- 推荐"直接插 + 唯一约束捕获"，比"先查再插"更可靠（避免并发竞态）

---

## 五、bcrypt 密码哈希

### 为什么不能存明文

明文密码 = 任何人拿到 db 文件就全泄露。哈希是**单向函数**，无法从哈希反推密码。

```js
const bcrypt = require('bcryptjs');
const hash = bcrypt.hashSync(password, 10);   // 加密，10 = 计算复杂度
const ok = bcrypt.compareSync(password, user.password_hash);  // 登录比对
```

- 同样的密码每次哈希结果不同（bcrypt 自动加盐）
- `10` 越大越慢越安全，但要平衡性能
- 登录用 `compareSync`（重新哈希比对），不是"解密"

### 防用户枚举

登录失败**统一返回 401**，不区分"用户不存在"和"密码错误"，否则攻击者可探测哪些用户名存在。

```js
if (!user || !bcrypt.compareSync(password, user.password_hash)) {
    return res.status(401).json({ error: '用户名或密码错误' });
}
```

---

## 五·五、JWT：登录后签发的通行证

### 为什么需要 JWT

登录成功只返回"登录成功"没用——下次请求服务器怎么知道是 alice？让客户端再发密码？太危险。
**方案**：登录后签发 token，客户端每次带着，服务器验证签名即知身份，不用查数据库。

### JWT 结构（三段用 `.` 分隔）

```
eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VybmFtZSI6ImFsaWNlIn0.9nT1a1...
──────Header────── ──────Payload────── ──Signature──
算法信息 HS256      数据 + iat/exp       用密钥签的防篡改凭证
```

- Header：`{"alg":"HS256","typ":"JWT"}` 说明算法
- Payload：要携带的数据 + `iat`(签发时间) + `exp`(过期时间)
- Signature：`HMAC(密钥, Header+"."+Payload)` —— 攻击者改 payload 后没有密钥，重新算不出合法签名

### jsonwebtoken API

```js
const jwt = require('jsonwebtoken');
const SECRET_KEY = 'chathub-secret';   // 服务器私有，绝不泄露

// 签发（登录时，sign 不会抛错，不用 try/catch）
const token = jwt.sign({ username }, SECRET_KEY, { expiresIn: '1h' });

// 验证（每次请求时，verify 会抛错，必须 try/catch）
try {
    const decoded = jwt.verify(token, SECRET_KEY);  // 合法返回 payload
} catch (err) {
    if (err.name === 'TokenExpiredError') { /* 过期 */ }
    // 其他 JsonWebTokenError → 无效
}
```

### 客户端怎么带 token（Bearer）

```js
// 请求头
Authorization: Bearer <token>
// 服务器取法
const token = req.headers.authorization.split(' ')[1];  // 切掉 "Bearer " 前缀
```

### /me 接口验证流程（顺序很重要）

```js
app.get('/me', (req, res) => {
    const authHeader = req.headers.authorization;
    // ① 先检查头存在（否则 undefined.split 崩溃）
    if (!authHeader || !authHeader.startsWith('Bearer ')) {
        return res.status(401).json({ error: '未提供有效的授权信息' });
    }
    const token = authHeader.split(' ')[1];
    // ② 再验证（try/catch 区分过期/无效）
    try {
        const decoded = jwt.verify(token, SECRET_KEY);
        return res.status(200).json({ username: decoded.username });
    } catch (err) {
        if (err.name === 'TokenExpiredError') {
            return res.status(401).json({ error: '授权已过期' });
        }
        return res.status(401).json({ error: 'token无效' });
    }
});
```

### 关键点

- **sign 和 verify 是两回事**：sign 登录时签发，不抛错；verify 每次验证，会抛错
- `expiresIn: '1h'`（字符串）vs `expiresIn: 2`（数字=秒）
- token 放请求头而不是 URL：URL 会进日志/历史记录，泄露 token

---

## 五·六、环境变量：密钥不进代码库

### 为什么密钥不能写死

- 代码库会被 Git 提交/推上 GitHub，密钥泄露 = 任何人能伪造 token
- 换环境（开发/生产）要换密钥，写死只能改代码

### 用法

```js
// server.js 第一行（必须最顶部，后面代码要用）
require('dotenv').config();

// 从环境变量读，没有就回退默认值（开发用）
const SECRET_KEY = process.env.JWT_SECRET || 'dev-secret';
```

### .env 文件（不入库）

```
# auth-service/.env（gitignore 排除 *.env）
JWT_SECRET=chathub-dev-secret
```

- dotenv 把 `.env` 里的 `键=值` 写进 `process.env`
- `.env` 必须加进 .gitignore，防止密钥泄露
- 别人克隆仓库没有 `.env` → 走默认值能跑；生产环境必须显式设环境变量

### 关键点

- `require('dotenv').config()` **必须放最顶部**，否则后面的代码读到 undefined
- `|| 'dev-secret'` 是双刃剑：开发方便，生产忘设会共用公开密钥
- 真实项目里密钥由负责人口头/密钥管理工具分发给新人，不写进仓库

---

## 五·七、输入校验：信任前的检查

### 为什么校验

所有网络输入都不可信。不校验的风险：

| 输入 | 问题 |
|------|------|
| 超长字符串 | 撑爆数据库、bcrypt 哈希变慢（DoS） |
| 非法字符 | XSS 注入、显示错乱 |
| 首尾空格 | `alice` 和 ` alice ` 变两个用户 |

### 校验函数（DRY：register 和 login 共用）

```js
// 返回错误消息或 null（合法）
function validateCredentials(username, password) {
    if (!username || !password) return '用户名和密码不能为空';
    if (username.length > 20 || username.length < 3) return '用户名长度必须在 3 到 20 个字符之间';
    if (!/^[a-zA-Z0-9_]+$/.test(username)) return '用户名只能包含字母、数字和下划线';
    if (password.length > 64 || password.length < 6) return '密码长度必须在 6 到 64 个字符之间';
    return null;
}
```

### 正确用法：接返回值再判断

```js
app.post('/register', (req, res) => {
    const { username, password } = req.body;
    const error = validateCredentials(username, password);
    if (error) return res.status(400).json({ error });
    // 校验通过，继续...
});
```

### 关键点

- **校验顺序**：先判空（`!username`）再取 length（`username.length`），否则 undefined.length 崩溃
- **不能把校验函数当中间件用**：中间件签名是 `(req,res,next)`，普通校验函数是 `(username,password)`，混用会卡死请求（不发响应也不 next）
- **正则** `/^[a-zA-Z0-9_]+$/`：`^`开头 `$`结尾 `[]`字符集 `+`一个或多个，整串只能字母数字下划线
- **DRY**：两接口共用同一校验函数，避免重复

---

## 六、常见陷阱 / 面试题

| 陷阱 | 正确做法 |
|------|---------|
| `res.status().json()` 不 return | `return res.status(...).json(...)`，否则响应发两次 |
| npm 包 require 带 `./` | `require('bcryptjs')`，不带 `./` |
| 把 better-sqlite3 当异步 | 它是同步的，出错直接 throw，用 try/catch |
| 字符串拼接 SQL | 用 `?` 占位符预编译 |
| 密码存明文/占位符 | bcrypt.hashSync 存哈希 |
| 登录区分用户不存在/密码错 | 统一 401 防枚举 |
| `jwt.sign` 包在 try/catch | sign 不抛错，verify 才抛错；把 verify 错误处理放 /me |
| `token.expiration` 取过期 | token 是字符串，过期看 `err.name === 'TokenExpiredError'` |
| /me 先取 token 再查头 | 先检查 `authHeader` 存在，否则 `undefined.split` 崩溃 |
| SQL 字符串拼接缺空格 | `SELECT * FROM users WHERE`（空格不能少） |
| 校验函数当中间件用 | 校验是普通函数 `(username,password)`，在 handler 内调用并判断返回值；当中间件会卡死请求 |
| 校验结果不判断 | `const error = validate(...); if (error) return 400;` 否则校验形同虚设 |
| 先 length 后判空 | 先 `!username` 再 `username.length`，否则 undefined.length 崩溃 |

**面试题：为什么登录错误统一返回 401？**
→ 防止用户枚举。区分错误类型等于告诉攻击者"这个用户名存在"，便于针对性爆破。

**面试题：JWT 为什么防篡改？**
→ Signature 用服务器私有密钥对 Header+Payload 做 HMAC。攻击者改 payload 后，没有密钥算不出匹配的新签名，验签失败。

---

## 七、待办

- [x] JWT 签发/验证 + exp 过期（W5 任务 5）✅ 07 项测试全过
- [x] JWT 密钥改环境变量（dotenv + .env + gitignore 排除）✅
- [x] 输入校验加强：长度 3-20/6-64、字符集正则、register/login 共用 ✅
- [ ] 数据库迁移脚本（建表脚本可复用，可选）
- [ ] 密码非明文已验证：db 里是 `$2b$10$...` bcrypt 哈希
- [ ] ChatHub 中：Qt 客户端用 QNetworkAccessManager 调这些接口（W6）

---

**对应代码**：`D:\CppLearn\chathub\auth-service\`
