# 笔记：SQLite 与数据库入门（W9）

> 2026-08-12 | 项目：ChatHub W9 持久化 | 目标：看懂并写出 SQLite 存储代码

---

## 一、数据库是什么（先建立直觉）

### 生活类比

```
Excel 表格：你能看到、能改，但一次只能一个人编辑，存文件里
数据库：一个"看不见的 Excel"，专门给程序用，能并发、能查询、能保证不出错
```

**数据库解决三个问题**：
1. **持久**——程序关了数据还在（存硬盘）
2. **并发**——多个连接同时读写不打架
3. **可靠**——要么成功要么回滚，不会写一半

### SQLite 的特殊地位

| 对比项 | SQLite | MySQL/PostgreSQL |
|--------|--------|------------------|
| 形态 | **一个文件**（`chat.db`） | 一个服务器程序 |
| 部署 | 程序里直接调用库 | 单独装、单独连 |
| 适用 | **单机、嵌入式、开发测试** | 多机、高并发、生产 |
| 学习成本 | **低**，API 直接 | 高，要管连接/权限 |

> **W9 用 SQLite**：单机 ChatHub 开发/测试够用。**W11 才升级 MySQL**（集成交付）。这是"先简单后复杂"的正确路径。

---

## 二、SQL 语言入门（只会 4 个动词）

### 关系型数据库 = 一堆"表"（Table）

```
messages 表（一条消息 = 一行）：
┌────────────┬────────┬───────────┬─────────┐
│ message_id │ sender │ recipient │ content │  ← 列（字段）
├────────────┼────────┼───────────┼─────────┤
│ abc123     │ alice  │ bob       │ 你好    │  ← 行（记录）
│ def456     │ bob    │ alice     │ 在吗    │  ← 行
└────────────┴────────┴───────────┴─────────┘
```

### 4 个核心命令

```sql
-- ① 建表（定义有什么列）
CREATE TABLE messages (
    message_id TEXT PRIMARY KEY,   -- 主键：唯一标识一行
    sender     TEXT NOT NULL       -- NOT NULL：不能为空
);

-- ② 插入（加一行）
INSERT INTO messages (message_id, sender) VALUES ('abc', 'alice');

-- ③ 查询（按条件取行）
SELECT message_id FROM messages WHERE sender = 'alice';

-- ④ 删除/更新（不太常用，知道即可）
DELETE FROM messages WHERE message_id = 'abc';
UPDATE messages SET sender = 'bob' WHERE message_id = 'abc';
```

### 关键概念：**主键（PRIMARY KEY）**

> **主键 = 每一行的"身份证号"，全局唯一，用来快速定位。**

```
没有主键：要找到"alice 那条"得翻遍全表
有主键：  直接按主键找到，O(1) 快
```

W9 的 `message_id` 就是主键——一条消息一个 ID，永远不会重复。

### 关键概念：**索引（INDEX）**

> **索引 = 书的目录。不建索引，查数据要一页页翻（全表扫描）；建了索引，直接翻到那页。**

```sql
-- 建索引：以后按 (sender, send_at) 查就快
CREATE INDEX idx_messages ON messages(sender, send_at);
```

> **代价**：索引占空间、写入变慢（每次插入要更新索引）。**查询多的列建索引，不是所有列都建。**

---

## 三、数据库操作前的关键概念

### 事务（Transaction）

> **事务 = 一组操作"要么全成，要么全不成"。** 就像转账：扣钱和加钱必须一起成功，不能只扣不加。

```sql
BEGIN TRANSACTION;              -- 开始
INSERT ...;                     -- 操作1
INSERT ...;                     -- 操作2
COMMIT;                         -- 全提交（都生效）
-- 或
ROLLBACK;                       -- 全撤销（都当没发生）
```

> W9 要求"插入用显式事务"——保证一条消息的写入要么完整入库，要么啥都不写。

### 唯一约束（UNIQUE）

> **UNIQUE = 这列的"值"不能重复。** 和主键类似，但主键只能一个，UNIQUE 可以有多个。

```sql
UNIQUE(sender, client_local_id)   -- 同一个 sender 不能用同一个 local_id 发两次
```

> **这就是 W9 幂等的核心**——靠它挡住"同一条消息发两次"。

### 预编译语句（Prepared Statement）

> **预编译 = 先写好 SQL 模板（带 `?` 占位符），再每次填不同值。** 防止 SQL 注入 + 快。

```sql
-- 模板（? 是占位符）
INSERT INTO messages VALUES (?, ?, ?)

-- 第一次填：abc, alice, bob
-- 第二次填：def, carol, dave
```

**为什么安全**：用户输入当"值"填进去，不会变成"SQL 代码"执行。字符串里有 `'` 也不会破坏语句。

---

## 四、sqlite3 C 语言 API（W9 要用的）

### 完整的 6 步流程

```
① sqlite3_open(路径, &db)     打开数据库（创建文件）
② sqlite3_prepare_v2(db, SQL, -1, &stmt, NULL)   预编译
③ sqlite3_bind_text/int64(...)  绑定参数（填占位符）
④ sqlite3_step(stmt)            执行
⑤ sqlite3_column_*(stmt, 列号)  取结果（SELECT 时）
⑥ sqlite3_finalize(stmt)        释放语句
```

### ① 打开与关闭

```cpp
sqlite3* db = nullptr;
int rc = sqlite3_open("chat.db", &db);   // 不存在会自动创建
if (rc != SQLITE_OK) { /* 失败 */ }
sqlite3_close(db);                        // 用完关
```

### ② 预编译

```cpp
sqlite3_stmt* stmt = nullptr;
//              db     SQL文本  长度(-1=自动)  语句指针  剩余(忽略)
sqlite3_prepare_v2(db, "SELECT * FROM t WHERE id=?", -1, &stmt, nullptr);
```

> **重要：`prepare_v2` 的返回值必须检查**，不等于 `SQLITE_OK` 说明 SQL 写错了。

### ③ 绑定参数（**索引从 1 开始**）

```cpp
sqlite3_bind_text(stmt, 1, "alice", -1, SQLITE_TRANSIENT);   // 第1个 ?
sqlite3_bind_int64(stmt, 2, 123456);                          // 第2个 ?（整数）
```

| 参数 | 含义 |
|------|------|
| `1` | **第 1 个 `?`（从 1 开始！）** |
| `-1` | 字符串长度自动（按 `\0`） |
| `SQLITE_TRANSIENT` | **让 SQLite 拷贝一份**，不依赖原指针存活（**最安全，固定用它**） |

> **绝对不要用 `SQLITE_STATIC`**：它假设字符串一直活着，函数参数销毁后就悬空，是隐藏的崩溃源。

### ④ 执行（**看返回值**）

```cpp
int rc = sqlite3_step(stmt);

rc == SQLITE_ROW        // 查到 1 行（SELECT）
rc == SQLITE_DONE       // 执行完毕（INSERT/无结果 SELECT）
rc == SQLITE_CONSTRAINT // 违反约束（如 UNIQUE 冲突）
```

| 语句 | 成功时 step 返回 | 判断 |
|------|----------------|------|
| `SELECT` 有结果 | `SQLITE_ROW` | 有行 |
| `SELECT` 无结果 | `SQLITE_DONE` | 没查到 |
| `INSERT` 成功 | `SQLITE_DONE` | 插入了 |
| 违反 UNIQUE | `SQLITE_CONSTRAINT` | **冲突** |

> **最容易混**：`SQLITE_ROW`（查到行）和 `SQLITE_DONE`（执行完没行）。SELECT 用 ROW 判断有没有结果。

### ⑤ 取列值（**索引从 0 开始！**）

```cpp
// 只在 step() 返回 SQLITE_ROW 后调用
const char* id  = sqlite3_column_text(stmt, 0);    // 第 0 列
int64_t    num  = sqlite3_column_int64(stmt, 1);   // 第 1 列
```

> **⚠️ 三个索引别混**：
> - `bind` 参数索引：**从 1 开始**
> - `column` 列索引：**从 0 开始**
> - `step` 控制行，column 取列，**行/列是两个维度**

### ⑥ 释放

```cpp
sqlite3_finalize(stmt);   // 每个 prepare 的 stmt 都要 finalize，否则内存泄漏
```

---

## 五、W9 的幂等逻辑（为什么先查再插）

### 场景：客户端重试发送

```
客户端发消息（local_id = "msg-1"）
  → 网络抖了一下，客户端以为失败 → 重试（还是 local_id = "msg-1"）
  → 服务端收到两次同样请求！
```

**如果不处理**：数据库里存了两条一模一样的消息 → 用户看到两条重复气泡。

### 幂等：靠 UNIQUE(sender, client_local_id) 挡重复

```
第一次：sender=alice, local_id=msg-1 → 插入成功
第二次：sender=alice, local_id=msg-1 → UNIQUE 冲突！
```

### 但"冲突"要区分两种情况

```
① 完全重复：同一 local_id + 正文一模一样
   → 就是"重试"，返回旧记录（DuplicateSame），不要新增

② 冲突重复：同一 local_id 但正文改了（如改成"你好吗"）
   → 这是客户端 bug/异常，返回 IdempotencyConflict，报错

UNIQUE 约束对两种都是 SQLITE_CONSTRAINT，分不清
→ 所以必须先 SELECT 查出旧记录，比较正文后才能区分
```

> **核心结论**：`SELECT 查旧记录 → 比较正文 → 决定 DuplicateSame / IdempotencyConflict / Stored`。

---

## 六、完整流程图（storeOrGetExisting）

```
storeOrGetExisting(消息)
    │
    ├─ ① SELECT 查 (sender, client_local_id)
    │      │
    │      ├─ 查到（SQLITE_ROW）？
    │      │    ├─ 取出旧记录
    │      │    ├─ recipient/content/client_send_at 全相同
    │      │    │    → 返回 DuplicateSame（带旧 message_id）
    │      │    └─ 有不同
    │      │         → 返回 IdempotencyConflict
    │      │
    │      └─ 没查到（SQLITE_DONE）
    │           ├─ ② 生成 message_id = hex(randomblob(16))
    │           ├─ ③ INSERT 全部字段
    │           │      ├─ 成功（SQLITE_DONE）→ 返回 Stored
    │           │      └─ 意外错误 → DatabaseError
    │           └─ 返回 Stored（带新 message_id）
    │
    └─ 任一 prepare/step 出错 → DatabaseError
```

---

## 七、常见陷阱 / 面试题

1. **`sqlite3_bind_text` 的第 2 个参数从几开始？**
   → **从 1**，对应 SQL 里第 1 个 `?`。

2. **`sqlite3_column_text` 的第 2 个参数从几开始？**
   → **从 0**，第 0 列 = SELECT 的第一个字段。

3. **`SQLITE_TRANSIENT` 和 `SQLITE_STATIC` 区别？**
   → TRANSIENT 让 SQLite 拷贝（安全）；STATIC 假设指针长存（危险，函数参数销毁就悬空）。**固定用 TRANSIENT**。

4. **`SQLITE_ROW` 和 `SQLITE_DONE` 区别？**
   → ROW = 查到一行（SELECT 有结果）；DONE = 执行完毕（INSERT 成功 / SELECT 没查到）。

5. **为什么幂等要"先 SELECT 再 INSERT"？**
   → 直接 INSERT 靠 UNIQUE 约束只能知道"冲突"，但分不清"完全重复（应返回旧记录）"还是"冲突重复（应报错）"。必须先查出旧记录比较。

6. **主键和 UNIQUE 的区别？**
   → 主键唯一标识一行（一个表一个）；UNIQUE 约束"值不重复"（可多个）。都建立唯一索引。

7. **为什么要用预编译语句（prepared statement）？**
   → 防 SQL 注入（用户输入当值不当代码）+ 快（SQL 只解析一次）。**永远不要拼接 SQL 字符串。**

8. **事务的作用？**
   → 一组操作要么全成要么全不。防"写一半崩溃留下半截数据"。W9 插入用显式事务。

---

**对应代码**：`D:\CppLearn\chathub\chat-server\src\repository\message_repository.cpp`
