#pragma once

#include <QDateTime>
#include <QString>

// ==================== 模块：聊天领域类型 ====================
// 功能：集中定义界面层使用的消息状态和完整聊天记录数据。
enum class ChatMessageStatus {
    Sending,
    Accepted,
    Failed,
    Received,
};

// 功能：表示一条完整聊天记录，不包含任何 Qt 控件。
struct ChatMessage {
    QString local_id;
    QString from;
    QString to;
    QString content;
    QDateTime send_at;
    ChatMessageStatus status{ChatMessageStatus::Received};
    QString failure_reason;
};
