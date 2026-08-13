# 笔记：W7 握手 — JWT 认证 + C++ 接入第三方库

> 2026-08-04 | ChatHub | 目标：让 chat-server 验证客户端身份

---

## 一、为什么需要"握手"（认证）

### 问题

一开始三个模块是孤立的：
- auth-service 发 token（谁都能登）
- chat-server 接受任何 TCP 连接（不管你是谁）

**任何人不登录就能连 chat-server 发消息** —— 不安全。

### 解决

```
client-qt 登录 → 拿 token → 连 chat-server 发 {auth, token}
                                    ↓
                      chat-server 验签
                        ✓ 有效 → 认证通过 → 能聊天
                        ✗ 无效 → 断开
```

**握手** = 客户端用 token 证明"我是谁"，服务器验证后才让进。

---

## 二、JWT 回顾（验证要懂的基础）

### 结构：三段式，点号分隔

```
eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VybmFtZSI6ImFsaWNlIiwiaWF0Ijox... .XrYvZ...
──────────────Header───────────── ──────────Payload────────── ────Signature──
```

| 段 | 内容 | 能改吗 |
|----|------|--------|
| Header | `{"alg":"HS256","typ":"JWT"}` | 能读，改了签名失效 |
| Payload | `{"username":"alice","iat":...}` | 能读，改了签名失效 |
| **Signature** | `HMAC(密钥, Header+"."+Payload)` | **没有密钥改不了** |

### 验证的三件事

```
verify(token, 密钥):
  ① 签名对不对（防伪造）
  ② 过期没有（exp < now 拒绝）
  ③ 被篡改没（改了 → 签名对不上）
```

**任何一项失败 → 抛异常 → 拒绝**。

---

## 三、jwt-cpp 库（C++ 验证 JWT）

### 是什么

> 一个 **header-only** 的 C++ JWT 库——只有头文件，不用编译库本身（但依赖 OpenSSL）。

### 验证流程（三步）

```cpp
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/kazuho-picojson/traits.h>

// ① 解码（拆开 token）
auto decoded = jwt::decode(token);

// ② 创建验证器（指定算法 + 密钥）
auto verifier = jwt::verify().allow_algorithm(jwt::algorithm::hs256{SECRET_KEY});

// ③ 验证（签名/过期都查，失败抛异常）
verifier.verify(decoded);

// 通过后取数据
auto username = decoded.get_payload_claim("username").as_string();
```

### 失败处理

```cpp
try {
    auto decoded = jwt::decode(token);
    jwt::verify().allow_algorithm(jwt::algorithm::hs256{SECRET_KEY}).verify(decoded);
    // 通过...
} catch (const std::exception& e) {
    // 失败：e.what() 有原因（"token expired"等）
}
```

---

## 四、认证状态机（Session 里怎么用）

### 状态流转

```
连接建立 → [未认证]
   ├─ 收到 auth 消息 → verifyJwt(token)
   │     ├─ 通过 → [已认证]，存 username
   │     └─ 失败 → close()
   └─ 收到非 auth 消息 → close()（没认证就发东西=非法）
[已认证] → 正常处理 chat/ping/pong/error
```

### 代码实现

```cpp
// session.h 加
bool m_authenticated{false};
std::string m_username;
bool verifyJwt(const std::string& token, std::string& out_username);

// session.cpp
void Session::processMessage(const protocol::Message& message) {
    if (!m_authenticated) {
        if (message.type == protocol::MessageType::auth) {
            if (std::string username; verifyJwt(message.body, username)) {
                m_authenticated = true;
                m_username = username;
                log("认证成功: " + username);
            } else {
                log("认证失败，关闭连接");
                close();
            }
        } else {
            log("未认证先发消息，关闭连接");
            close();
        }
        return;
    }
    // 已认证：正常 switch...
}

bool Session::verifyJwt(const std::string& token, std::string& out_username) {
    try {
        auto decoded = jwt::decode(token);
        jwt::verify().allow_algorithm(jwt::algorithm::hs256{SECRET_KEY}).verify(decoded);
        out_username = decoded.get_payload_claim("username").as_string();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
```

---

## 五、今天踩的坑（重点！）

### 坑 1：验证器创建了但没调用

```cpp
// ❌ 只创建了 verifier，没调 verify —— 任何 token 都"通过"
auto verifier = jwt::verify().allow_algorithm(...);
auto username = decoded.get_payload_claim("username").as_string();  // 没验证就取数据

// ✅ 必须调 verify，失败会抛异常
auto verifier = jwt::verify().allow_algorithm(...);
verifier.verify(decoded);   // ← 关键！
```

**教训**：`jwt::verify()` 返回一个对象，但**验证动作在 `.verify(decoded)` 里**。忘了调 = 根本没验证。

### 坑 2：找不到 jwt 头文件

```
# ❌ 编译报 jwt-cpp/jwt.h 找不到
# 因为没告诉编译器 include 路径
```

**解决**：CMake 加
```cmake
set(JWT_INCLUDE_DIR "D:/CppLearn/jwt-cpp/include")
target_include_directories(... ${JWT_INCLUDE_DIR})
```

### 坑 3：找不到 OpenSSL 头文件

```
# ❌ 报 openssl/ec.h: No such file or directory
# jwt.h 内部 include 了 OpenSSL
```

**解决**：加 OpenSSL 头文件路径（MinGW 自带在 `opt/include`）：
```cmake
target_include_directories(... ${SQLITE3_INCLUDE_DIR})  # opt/include 下
```

### 坑 4：链接找不到 crypto 库

```
# ❌ ld cannot find -lcrypto
```

**解决**：加库搜索路径 + 链接：
```cmake
target_link_directories(... ${SQLITE3_LIB_DIR})   # opt/lib
target_link_libraries(... crypto)
```

### 坑 5：运行报 0xC0000135（找不到 DLL）

```
# ❌ 运行时报 0xC0000135 (STATUS_DLL_NOT_FOUND)
# 缺 libcrypto-1_1-x64.dll
```

**原因**：jwt-cpp 调用 OpenSSL 时，需要 `libcrypto-1_1-x64.dll`（动态库）。
**解决**：把 dll 复制到 exe 同目录。
- 命令行构建：`build/chat-server/`
- **CLion 构建：`cmake-build-debug/chat-server/`**（注意！CLion 用不同的构建目录）

### 坑 6：CLion 和命令行构建目录不同

| 运行方式 | exe 位置 |
|---------|---------|
| 命令行 `cmake -B build` | `build/chat-server/` |
| **CLion 运行** | **`cmake-build-debug/chat-server/`** |

dll 必须复制到**你实际运行的那个目录**。CLion 里报错，就复制到 cmake-build-debug。

### 坑 7：Python 测试脚本中文输出乱码

Windows 的 GBK 控制台不支持 `✓` 等特殊字符 → `UnicodeEncodeError`。
**解决**：测试脚本用纯 ASCII 输出，或看服务端日志（权威证据）。

---

## 六、CMake 配置（JWT 完整版）

```cmake
# 根 CMakeLists.txt
set(JWT_INCLUDE_DIR "D:/CppLearn/jwt-cpp/include")

# chat-server/CMakeLists.txt
target_include_directories(chat-server PRIVATE
    ${CHAT_SERVER_INCLUDES}
    ${JWT_INCLUDE_DIR}          # jwt-cpp 头文件
    ${SQLITE3_INCLUDE_DIR}      # OpenSSL 头文件
)
target_link_directories(chat-server PRIVATE ${SQLITE3_LIB_DIR})  # 库路径
target_link_libraries(chat-server PRIVATE ws2_32 mswsock crypto)  # crypto = OpenSSL
```

**注意**：`add_test(NAME xxx COMMAND 可执行文件)` —— COMMAND 是可执行文件名，不是 .cpp 源码。

---

## 七、验证结果

| 场景 | 结果 |
|------|------|
| 好 token 认证 | ✅ 通过，能广播 "Hello from alice" |
| 坏 token | ✅ "认证失败，关闭连接" |
| 未认证发消息 | ✅ "未认证先发消息，关闭连接" |

---

## 八、客户端握手（QTcpSocket + ChatClient 状态机）

> 上文的认证是**服务端**怎么验 token。这一节是**客户端**怎么连 TCP、发 token、收结果——W7-2 的内容。

### 1. 客户端握手的职责

```
client-qt                              chat-server
  │ POST /login → 拿 token (HTTP)         │
  │ TCP connect (localhost:9000)          │
  │═══════════════════════════════════════╪═ 建立连接
  │ 发 auth 帧 [8字节头][type=5][token]    │
  │──────────────────────────────────────→│ 验签
  │←──────────────────────────────────────│ 通过 → 回 {"ok":true}
  │                                       │ 失败 → 直接 close
```

客户端要做的三件事：
1. **连接**：`connectToHost`（异步，靠 `connected` 信号知道连上）
2. **组帧**：把 token 装进 8 字节协议头，type=5
3. **解帧**：接收服务端回帧，解析出"认证成功/失败"

### 2. ChatClient 状态机

客户端把握手过程抽象成 4 个状态，用 `enum class AuthState` 管理：

```
idle ──connectToHost──→ connecting ──连接成功──→ 发auth帧
                                              ↓
                                       waitingAuthResult
                                              ↓
                         收到 type=5 帧 → authenticated（成功）
                         收到断开/error → idle（失败）
```

**为什么需要状态机**：TCP 是异步的，信号随时可能来。不用状态机就不知道"当前在哪个阶段"，收到断开时无法判断该报"连接失败"还是"认证失败"。

### 3. 关键代码（你写的 ChatClient）

**连接 + 发帧**（连接成功后发 auth 帧）：
```cpp
// connectWithToken：初始化 + 连接
m_token = token;
m_received_buffer.clear();
m_state = AuthState::connecting;
m_connect_timer.start(5000);          // 5秒连接超时
m_socket.connectToHost(QHostAddress::LocalHost, 9000);

// onSocketConnected：连上了，发认证帧
void ChatClient::onSocketConnected() {
    m_connect_timer.stop();
    sendAuthFrame();
}
```

**组帧**（8 字节头，大端序）：
```cpp
QByteArray frame;
frame.append(static_cast<char>(kMagic >> 8));    // [0x43] magic 高字节
frame.append(static_cast<char>(kMagic & 0xFF));  // [0x48] magic 低字节
frame.append(static_cast<char>(kVersion));       // [0x01]
frame.append(static_cast<char>(kAuthType));      // [0x05] type=5
// 长度 4 字节，大端
frame.append(static_cast<char>((length >> 24) & 0xFF));
frame.append(static_cast<char>((length >> 16) & 0xFF));
frame.append(static_cast<char>((length >> 8) & 0xFF));
frame.append(static_cast<char>(length & 0xFF));
frame.append(body);                              // token
```

**解帧**（收到数据后拼缓存、解析完整帧）：
```cpp
void ChatClient::processReceivedFrames() {
    while (m_received_buffer.size() >= kHeaderLength) {
        // 读头、校验 magic/version/type/长度...
        if (type == kAuthType && m_state == AuthState::waitingAuthResult) {
            m_state = AuthState::authenticated;
            emit authSucceeded();          // ← 握手成功！
            continue;
        }
        // 收到 error 帧 / 协议错误 → 断开
    }
}
```

### 4. 服务端成功回帧，客户端才收到

**关键**：服务端认证成功后**会回一帧** `{"ok":true}`（type=5），客户端靠**收到这帧**判断握手成功，而不是"连接保持"就算成功。

```cpp
// session.cpp —— 认证成功
send(protocol::MessageType::auth, R"({"ok":true})");
```

所以客户端 `authSucceeded` 信号必须绑定到"收到 type=5 帧"，而不是"TCP 连接建立"。

---

## 九、今天的坑（重点！）

### 坑 8：magic 截断——协议头只有 7 字节

**症状**：客户端连上后，服务端日志报 `协议错误，关闭当前连接`，但帧格式看着对。

**原因**：`kMagic` 是 `quint16`（0x4348，占 2 字节），但用 `static_cast<char>(kMagic)` 写帧，**被截断成 1 字节**：

```cpp
// ❌ 错误：kMagic 是 2 字节，static_cast<char> 只保留低字节 0x48
frame.append(static_cast<char>(kMagic));  // 实际只写了 0x48

// ✅ 正确：分高/低字节分别写
frame.append(static_cast<char>(kMagic >> 8));   // 高字节 0x43
frame.append(static_cast<char>(kMagic & 0xFF)); // 低字节 0x48
```

**结果**：8 字节协议头只写出了 7 字节（magic 丢了一半），服务端读 magic 对不上 `0x4348` → 报协议错误 → 断开。

**教训**：**多字节字段（quint16/quint32）写进字节流时，必须按字节拆开，不能直接 cast 成 char**。这和 length 字段要 `>> 24 / >> 16 / >> 8` 拆 4 字节是同一个道理。

### 坑 9：服务端认证失败"直接断开"，不回 error 帧

服务端认证失败走 `close()`，**不返回 error 帧**。所以客户端判断认证失败，靠的是 `onSocketDisconnected`（"服务器在认证前断开"），而不是等 error 帧。设计客户端时要知道这一点。

---

## 十、验证结果（客户端）

| 场景 | 结果 |
|------|------|
| Qt 客户端登录 + TCP 握手 | ✅ 收到 {"ok":true}，认证成功 |
| 两个客户端同时在线 | ✅ 服务端日志"当前在线:2" |
| 服务端拒绝 | ✅ 客户端提示"认证失败" |

---

## 十一、一句话总结

> **客户端握手 = 用 QTcpSocket 连 TCP → 发带 token 的 auth 帧（type=5）→ 收到服务端 {"ok":true} 才算成功。** 核心是状态机管理异步流程 + 组帧时多字节字段要按字节拆开。

> **握手 = 客户端用 JWT 证明身份。chat-server 用 jwt-cpp 验证签名（HS256 + 密钥），通过才允许聊天。** 接入第三方库的坑主要在 CMake（include 路径、库链接）和运行时（dll 位置）。

---

**对应代码**：`D:\CppLearn\chathub\chat-server\`（examples/jwt_demo.cpp · src/net/session.cpp）
