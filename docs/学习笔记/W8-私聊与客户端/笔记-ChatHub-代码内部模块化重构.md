# ChatHub：代码内部模块化重构

## 本次目标

在不改变文件结构、协议格式、信号与槽签名、路由规则及业务行为的前提下，按职责重新排列类成员和函数，并补充中文职责注释。

## 统一规则

1. 头文件与源文件采用同一模块顺序：生命周期、对外操作、事件入口、内部辅助、状态成员。
2. 构造函数只负责组装、初始化和连接信号；具体工作交给 `setupXxx()`、处理函数或槽函数。
3. 网络层只处理连接、协议帧和消息模型；界面层只维护会话、渲染气泡和响应用户操作。
4. 协议常量集中在共享协议头；聊天消息模型集中在客户端类型头，避免同义字段分散定义。
5. 注释说明“本模块解决什么问题”，不重复代码已经表达的语法细节。

## 本次实践

- `Session`：明确发送、接收解码、认证、会话生命周期、Server 协作回调与业务分派的边界；将 `verifyJwt()` 与认证分派辅助函数放在同一实现模块。
- 协议层：为帧魔数、版本、头长度、正文上限和帧/聊天正文成员补充职责说明。
- Qt 客户端：补充 `HttpClient` 网络管理器、`ChatClient` TCP Socket 与连接定时器的职责说明，保持其与 UI 的边界。

## 验证证据

- `cmake --build build --parallel 2` 成功。
- `ctest --test-dir build --output-on-failure`：2/2 通过（`frame_decoder_test`、`jwt_demo`）。
- 本轮属于代码组织与边界理解练习，仍标记为“学习中”，不能仅凭构建通过判定完全掌握。

## 常见陷阱

- `MainWindow` 持有的外部 `ChatClient*` 是借用指针，不能由窗口释放。
- `message`、`content`、`body` 分别表示完整消息模型、聊天文本、协议正文，不能混用。
- 客户端写入 Socket 成功不等于对端已收到；确认与失败必须依赖 `local_id` 定位本地消息。

## 明日实现设计：以 `ChatMessage` 直传

目标是消除 UI 信号槽中并列传递 `local_id`、`from`、`to`、`content`、`send_at` 的方式。先只设计，不在本轮修改功能。

1. `ChatClient` 从协议正文解析字段后，调用内部组装函数：

```cpp
static ChatMessage makeReceivedChatMessage(
    const QString& local_id,
    const QString& from,
    const QString& to,
    const QString& content,
    const QDateTime& send_at);
```

2. 该函数统一填写模型：收到的消息使用 `ChatMessageStatus::Received`，`failure_reason` 置空；协议暂未提供的成员使用与业务一致的占位值，不让 UI 猜测。
3. 将接收信号与 UI 槽逐步改为 `const ChatMessage& message`。槽函数内部自行读取 `message.from`、`message.content`、`message.send_at` 等成员，更新 `m_conversations` 和界面。
4. 仅返回 `local_id` 的确认/失败帧不强行虚构完整 `ChatMessage`；它们继续作为“定位已有本地模型”的事件，避免丢失原始发送时间、正文和接收者。

这样，消息创建责任在通信层，消息显示责任在界面层，字段扩展时只修改 `ChatMessage` 与组装函数。

## 08-10 验收：收到消息以 `ChatMessage` 直传

- `ChatClient::handleChatBody()` 先校验 JSON、`local_id`、`from`、`to`、`content` 与有效 ISO `send_at`；非法时间发出 `serverError` 并停止分发。
- 校验通过后，`makeReceivedChatMessage(const QJsonObject&)` 负责生成收到消息模型，设置 `ChatMessageStatus::Received` 并保持失败原因为空。
- `chatMessageReceived` 与 `MainWindow::onChatMessageReceived` 改为传递 `const ChatMessage&`。UI 只读取模型成员、写入 `m_conversations`，再按当前会话决定是否渲染。
- 双客户端消息显示和非法时间拒绝均已人工验收；构建通过。当前为“学习中”，下一次可把发送、确认和失败状态更新按相同模型边界逐步收敛。

## 08-10 验收：发送消息先保存本地模型

- `MainWindow` 在网络调用前生成稳定的 `local_id`、UTC `send_at` 和 `Sending` 状态，并先写入 `m_conversations`；因此网络层立即失败时也能按 `local_id` 标记气泡失败。
- `ChatClient::sendChatMessage(ChatMessage)` 只校验并编码既有模型，不重新生成身份；发送正文只含 `to`、`content`、`local_id`、`send_at`。
- 重试仅恢复 `Sending`、清空失败原因，保留原 `local_id`、正文、双方身份与最初时间。双客户端、断线失败、重试、手填接收者与会话回退均已人工验收。

## 08-10 验收：确认与失败以状态更新模型直传

- 确认帧和携带 `local_id` 的失败帧由 `makeMessageStateUpdate()` 组装为状态更新模型：只有 `local_id`、`status`、`failure_reason` 有业务意义，其余成员保持占位。
- `chatMessageAccepted`、`chatSendFailed` 以及对应 UI 槽统一传递 `const ChatMessage&`；UI 只按 `local_id` 查找既有消息并更新状态，绝不把状态更新对象加入会话。
- 无 `local_id` 的认证错误和通用服务端错误仍分别走 `authFailed`、`serverError`。确认成功、失败红点、未知 ID 和认证错误均已人工验收。

## 08-10 验收：UI 气泡渲染以完整消息模型直传

- `renderCurrentConversation()` 只读当前会话中的完整 `ChatMessage`，逐条调用 `appendMessageBubble(const ChatMessage&)`；不再将同一消息拆成七个并列参数。
- `appendMessageBubble()` 自行读取 `from`、`content`、`send_at`、`status`、`failure_reason`，并保留 `local_id` 作为失败重试时回查既有消息的唯一键。
- 状态更新模型仍不能直接渲染：它缺少正文、双方身份和真实时间，只能先按 `local_id` 更新完整消息后再重绘。
- 构建通过；正常消息展示、失败红点和点击重试均已人工验收。当前能力为“学习中”。

### 常见陷阱 / 面试题

- 回调不应因为方便而持有完整消息副本；若回调只需重试标识，就只保存 `local_id`。这样既明确依赖，也避免无关数据在每个失败气泡中重复保留。

## 08-10 验收：失败重试回调的最小捕获

- 失败按钮的 Lambda 使用初始化捕获，将 `message.local_id` 复制为自己的 `local_id`；点击发生在气泡构造函数返回之后，不能捕获 `message` 的引用。
- 回调不再复制完整 `ChatMessage`，但仍只将 ID 传给 `onRetryClicked()`；该槽函数按 ID 重新查询当前的完整消息，因此重试行为不变。
- 构建、失败红点和重试均已人工验收。当前能力为“学习中”。

## 08-10 验收：查询真实元素与会话稳定身份

- `findMessageByLocalId()` 需要修改会话消息，因此保持非 `const`；遍历时必须使用 `ChatMessage&`，返回的才是容器真实元素地址。按值遍历后返回地址会产生悬空指针。
- `QListWidgetItem::text()` 是可变化展示，`Qt::UserRole` 才保存稳定的 peer。会话项创建采用“找到相同身份立即返回；遍历结束后再创建”的结构，不能改写不匹配旧项。
- Bob/Carol 多会话和重复创建均已人工验收；未读数功能仍在实现中。

## 08-10 验收：会话未读计数

- 未读数属于 `MainWindow` 的界面状态，因此使用 `QHash<QString, int> m_unreadCounts` 单独保存；完整聊天数据仍只保存在 `m_conversations`。
- `Qt::UserRole` 保存稳定 peer，`text()` 只显示 `peer` 或 `peer (N)`。这样展示文本变化不会影响会话查找、切换和发送目标。
- 收到非当前会话的消息时，必须对同一个 `message.from` 同时完成“计数递增”和“列表刷新”；当前会话只重绘消息，不能生成未读数。
- 点击会话后从 `UserRole` 读取 peer，`markConversationRead()` 删除该 peer 的计数并刷新列表项。Bob 连发两条显示 `(2)`，点击后清零；当前 Bob 会话直接显示新消息，均已人工验收。

### 常见陷阱 / 面试题

- 状态模型的键与 UI 刷新的身份必须一致。若递增 `m_unreadCounts[message.from]` 却刷新 `m_currentPeer`，数据虽然正确，界面却会刷新错误的会话项，甚至在未选会话时被空值保护直接忽略。

## 08-10 验收：会话最后消息预览

- `m_conversations` 仍是消息数据唯一真相。`makeConversationPreview()` 只读最后一条完整消息并转为 UI 摘要，不增加 `m_lastPreview` 一类重复缓存。
- `refreshConversationItem()` 是列表展示的唯一出口：用 `UserRole` 找到稳定 peer，再组合“名称/未读数”和摘要为两行文本；`text()` 始终只承担显示职责。
- 任何新增消息必须在模型写入后刷新对应 peer：本地发送、接收消息、防御性补建三条路径均已接入。非当前接收路径先累加未读、再刷新一次，避免用旧未读数产生重复 UI 重绘。
- 摘要换行会变为空格；超长摘要保留固定上限并追加 `…`。若截断末尾是高代理项，会先删去该项，防止形成无效 UTF-16。
- 本地发送、当前会话接收、非当前会话接收、未读清零和气泡/重试回归均已人工验收；构建和 CTest 2/2 通过。当前仍为“学习中”。

### 常见陷阱 / 面试题

- `QString::toUShort()` 是把文本解析成数字，不能用来读取字符编码；检查代理项要从末尾 `QChar` 的 `unicode()` 值取得 UTF-16 单元。
- `QString::arg()` 的 `int` 参数可能表示字段宽度。要展示 peer、未读数、预览三项时，应让格式串的每个占位符对应一次清晰的 `.arg()` 调用。

## 08-10 验收：会话时间摘要

- 时间摘要不是新的会话状态：它只从 `m_conversations` 中最后一条完整 `ChatMessage::send_at` 推导，避免出现与消息模型不同步的缓存。
- 时间比较前必须统一时区。消息按 UTC 保存，格式化函数把消息时间和当前时间都转为本地时间，再判断今天、昨天、同年或跨年。
- `makeConversationTimeText()` 负责读取和转换，`refreshConversationItem()` 只负责把名称、未读数、时间和预览拼成两行文本；无效时间返回空文本，界面层据此不追加分隔符。
- 分隔符使用 `QStringLiteral(" \\u00B7 %1")`，避免编辑器编码导致中点字符显示成其他文本。
- 今天、昨天、同年、跨年、无效时间及未读清零均已人工验收；构建成功，CTest 2/2 通过。当前能力仍为“学习中”。
