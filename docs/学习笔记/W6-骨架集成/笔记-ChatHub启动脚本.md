# 笔记：ChatHub 启动脚本（Windows 批处理）

> 2026-08-04 | W6 骨架集成 | 目标：一条命令同时启动 auth-service + chat-server

---

## 一、它解决什么问题

没有脚本时，每次测试要手动开两个窗口、敲两条命令：

```
手动：                       有脚本：
窗口1: cd auth-service       双击 start_all.bat
       npm start             → 两个服务自动起来，各自一个窗口
窗口2: cd build\chat-server
       chat-server.exe
```

**启动脚本 = 把"测试前的手动准备"变成"一条命令"。** W7 联调（Qt 客户端连 TCP）时，跑一次脚本服务就绪。

---

## 二、核心命令：start

```
start "窗口标题" cmd /k "要执行的命令"
```

| 部分 | 作用 |
|------|------|
| `start` | 开新窗口，**立即返回**（不阻塞脚本） |
| `"窗口标题"` | 新窗口标题栏显示的名字 |
| `cmd /k` | 开一个新的命令提示符窗口；`/k` = 命令跑完后窗口**不关**（保留日志） |
| `"命令"` | 在新窗口里执行的命令 |

**关键点**：`start` 会立即返回，所以脚本能连续启动两个服务，不会卡住等第一个。

---

## 三、完整脚本

```bat
@echo off
set PROJECT_ROOT=D:\CppLearn\chathub

echo Starting auth-service ...
start "auth-service" cmd /k "cd /d %PROJECT_ROOT%\auth-service && npm start"

echo Starting chat-server ...
start "chat-server" cmd /k "%PROJECT_ROOT%\build\chat-server\chat-server.exe"

echo Both services started. Close each window to stop.
```

**两个坑（重点，都踩过）：**

### 坑 1：`.bat` 里写中文会乱码

```
.bat 文件按 UTF-8 保存
但 Windows cmd 默认用 GBK(936) 代码页解析
→ 中文被读成乱码 → 命令被拆坏 → 各种"不是内部或外部命令"
```

**解决**：`.bat` 文件保持**纯 ASCII（全英文）**。中文提示放笔记里，不放脚本里。

### 坑 2：`cd /d X && exe` 找不到程序

```bat
REM ❌ 这样写，chat-server.exe 报"不是内部或外部命令"
start "chat-server" cmd /k "cd /d %PROJECT_ROOT%\build\chat-server && chat-server.exe"

REM ✅ 改用完整路径，不依赖 cd
start "chat-server" cmd /k "%PROJECT_ROOT%\build\chat-server\chat-server.exe"
```

**原因**：批处理里 `cd /d X && exe` 在嵌套 `cmd /k` 下不可靠——cmd 不总从"cd 之后的目录"找 exe。**完整路径是零依赖、最稳的写法。**

**注意**：auth-service 那条不能改成完整路径——`npm start` 需要**当前目录**有 `package.json`，所以必须 `cd /d` 进目录。它是命令（npm），不是 exe，`cd && npm start` 没问题。

---

## 四、验收结果

| 检查 | 结果 |
|------|------|
| 双击/运行脚本后出现两个窗口 | ✅ |
| auth-service 监听 3000 | ✅ |
| chat-server 监听 9000 | ✅ |
| 脚本本身不阻塞（跑完即返回） | ✅ |

---

## 五、设计取舍

| 方案 | 优点 | 缺点 |
|------|------|------|
| **批处理 .bat**（当前） | Windows 原生、双击即用 | 纯 ASCII、路径要写完整路径 |
| PowerShell .ps1 | 语法更强 | 双击默认不执行，要改执行策略 |
| Linux .sh | 简单 | 这台机器是 Windows |

**为什么选 .bat**：Windows 用户最省事——双击就完事，不用改系统设置。

---

## 六、常见陷阱 / 面试题

1. **Q：`start` 命令为什么脚本不阻塞？**
   A：`start` 开新窗口后立即返回，脚本继续执行下一行。`cmd /k` 让新窗口在命令结束后保持打开。

2. **Q：`.bat` 里中文为什么乱码？**
   A：cmd 默认用 GBK 代码页解析 `.bat`，UTF-8 的中文字节被读错。保持纯 ASCII 最稳。

3. **Q：为什么 `cd /d X && exe` 会报"不是内部或外部命令"？**
   A：批处理嵌套 `cmd /k` 下，cmd 不总从 cd 后的目录找 exe。用完整路径启动 exe 最可靠。

4. **Q：`npm start` 为什么必须 `cd` 进目录？**
   A：npm 需要当前目录有 `package.json` 才能找到启动脚本。

---

**对应文件**：`D:\CppLearn\chathub\start_all.bat`
