#include "repository/message_repository.h"

#include <sqlite3.h>
#include <iostream>
#include <string>
namespace repository{
    MessageRepository::~MessageRepository() {
        close();
    }

    bool MessageRepository::open(const std::string &db_path) {
        int rc = sqlite3_open(db_path.c_str(), &m_db);

        if (rc != SQLITE_OK) {
            // 功能：打开失败时用 rc 直接取错误文本，避免在 m_db 可能无效时调用 sqlite3_errmsg(m_db)
            std::cerr << "failed to open database: " << sqlite3_errstr(rc) << std::endl;
            close();
            return false;
        }
        // 第二步：设置 busy_timeout
        rc = sqlite3_busy_timeout(m_db,3000);
        if (rc != SQLITE_OK) {
            log("failed to set busy timeout");
            close();
            return false;
        }
        //第三步：建表
        const auto* creat_table_sql = R"(
            CREATE TABLE IF NOT EXISTS messages (
                message_id             TEXT PRIMARY KEY,
                sender                 TEXT NOT NULL,
                recipient              TEXT NOT NULL,
                participant_low        TEXT NOT NULL,
                participant_high       TEXT NOT NULL,
                client_local_id        TEXT NOT NULL,
                content                TEXT NOT NULL,
                client_send_at         TEXT NOT NULL,
                server_received_at_ms  INTEGER NOT NULL,
                UNIQUE(sender, client_local_id)
            );
        )";
        if (!exec(creat_table_sql)) {
            close();
            return false;
        }
        //第四步：创建索引
        const auto* create_indexes_sql = R"(
            CREATE INDEX IF NOT EXISTS idx_messages_conversation_order
                 ON messages(
                    participant_low,
                    participant_high,
                    server_received_at_ms DESC,
                    message_id DESC
                );
            CREATE INDEX IF NOT EXISTS idx_messages_user_order
                ON messages(
                    sender,
                    server_received_at_ms DESC,
                    message_id DESC
                );
            CREATE INDEX IF NOT EXISTS idx_messages_recipient_order
                ON messages(
                     recipient,
                     server_received_at_ms DESC,
                     message_id DESC
                );
        )";
        if (!exec(create_indexes_sql)) {
            close();
            return false;
        }
        // 第五步：设置数据库版本
        if (!exec("PRAGMA user_version = 1")) {
            close();
            return false;
        }
        return true;
    }

    // 存储或获取现有消息
    // 功能：按 UNIQUE(sender, client_local_id) 幂等规则写入或复用一条持久消息。
    // 返回：Stored（新插入）/ DuplicateSame（完全重复，复用既有记录）/
    //       IdempotencyConflict（同一 local_id 但内容不同）/ DatabaseError。
    StoreOutcome MessageRepository::storeOrGetExisting(const NewMessage &message)
    {
        // ===== 第 1 步：查幂等记录（sender + client_local_id）=====
        sqlite3_stmt* stmt = nullptr;
        const auto* sql = R"(
            SELECT message_id, recipient, content, client_send_at, server_received_at_ms
            FROM messages WHERE sender=? AND client_local_id=?
        )";
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            log("failed to prepare select statement");
            return StoreOutcome{StoreResult::DatabaseError};
        }
        // 绑定查询参数（索引从 1 开始）
        sqlite3_bind_text(stmt, 1, message.sender.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, message.client_local_id.c_str(), -1, SQLITE_TRANSIENT);

        // 执行查询：SQLITE_ROW=查到旧记录，SQLITE_DONE=无记录
        const int rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
            log("failed to step select statement");
            sqlite3_finalize(stmt);
            return StoreOutcome{StoreResult::DatabaseError};
        }

        // ===== 第 2 步：查到了旧记录 → 判断是完全重复还是冲突 =====
        if (rc == SQLITE_ROW) {
            // 取出旧记录各列（column 索引从 0 开始）
            const auto* old_recipient = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const auto* old_content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            const auto* old_client_send_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            const auto old_server_received_at_ms = sqlite3_column_int64(stmt, 4);

            const std::string old_recipient_str = old_recipient ? old_recipient : "";
            const std::string old_content_str = old_content ? old_content : "";
            const std::string old_client_send_at_str = old_client_send_at ? old_client_send_at : "";

            // 完全重复判定：recipient/content/client_send_at 三个字段都一致。
            // 注意：server_received_at_ms 是服务端生成的可信时间，不参与比较。
            const bool same = (old_recipient_str == message.recipient &&
                               old_content_str == message.content &&
                               old_client_send_at_str == message.client_send_at);

            // 情况 A：完全重复 → 复用既有记录（返回 DuplicateSame + 旧 message_id）
            if (same) {
                const auto* old_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                StoreOutcome out;
                out.result = StoreResult::DuplicateSame;
                out.message_id = old_id ? old_id : "";
                out.sender = message.sender;
                out.recipient = message.recipient;
                out.content = old_content_str;
                out.client_send_at = old_client_send_at_str;
                out.server_received_at_ms = old_server_received_at_ms;
                sqlite3_finalize(stmt);
                return out;
            }
            // 情况 B：同一 local_id 但内容不同 → 幂等冲突，拒绝写入
            sqlite3_finalize(stmt);
            return StoreOutcome{StoreResult::IdempotencyConflict};
        }

        // ===== 第 3 步：无旧记录 → 生成 message_id 并插入新消息 =====
        // 生成持久业务 ID：SQLite 随机 16 字节转 32 位 hex 字符串
        sqlite3_stmt* id_smst = nullptr;
        if (sqlite3_prepare_v2(m_db, "SELECT hex(randomblob(16))", -1, &id_smst, nullptr) != SQLITE_OK) {
            sqlite3_finalize(stmt);
            log("failed to prepare message_id statement");
            return StoreOutcome{StoreResult::DatabaseError};
        }
        if (sqlite3_step(id_smst) != SQLITE_ROW) {
            sqlite3_finalize(id_smst);
            sqlite3_finalize(stmt);
            log("failed to generate message_id");
            return StoreOutcome{StoreResult::DatabaseError};
        }
        const auto* new_id = reinterpret_cast<const char*>(sqlite3_column_text(id_smst, 0));
        const std::string message_id = new_id ? new_id : "";
        sqlite3_finalize(id_smst);

        // 会话归属：两个用户名按字典序，low 在前 high 在后（服务端算，不信客户端）
        const std::string low = (message.sender < message.recipient) ? message.sender : message.recipient;
        const std::string high = (message.sender < message.recipient) ? message.recipient : message.sender;

        // 预编译插入语句（9 个字段，? 占位符）
        sqlite3_stmt* ins_stmt = nullptr;
        const auto* insert_sql = R"(
            INSERT INTO messages
            (message_id, sender, recipient, participant_low, participant_high,
             client_local_id, content, client_send_at, server_received_at_ms)
            VALUES (?,?,?,?,?,?,?,?,?)
        )";
        if (sqlite3_prepare_v2(m_db, insert_sql, -1, &ins_stmt, nullptr) != SQLITE_OK) {
            sqlite3_finalize(stmt);
            log("failed to prepare insert statement");
            return StoreOutcome{StoreResult::DatabaseError};
        }
        // 绑定 9 个插入值（索引从 1 开始）
        sqlite3_bind_text(ins_stmt, 1, message_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins_stmt, 2, message.sender.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins_stmt, 3, message.recipient.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins_stmt, 4, low.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins_stmt, 5, high.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins_stmt, 6, message.client_local_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins_stmt, 7, message.content.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins_stmt, 8, message.client_send_at.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(ins_stmt, 9, message.server_received_at_ms);

        // 执行插入：SQLITE_DONE=成功，其他（如 UNIQUE 冲突）为失败
        const int irc = sqlite3_step(ins_stmt);
        sqlite3_finalize(ins_stmt);
        sqlite3_finalize(stmt);

        if (irc == SQLITE_DONE) {
            StoreOutcome out;
            out.result = StoreResult::Stored;
            out.message_id = message_id;
            out.sender = message.sender;
            out.recipient = message.recipient;
            out.content = message.content;
            out.client_send_at = message.client_send_at;
            out.server_received_at_ms = message.server_received_at_ms;
            return out;
        }

        log("failed to insert message");
        return StoreOutcome{StoreResult::DatabaseError};
    }

    // 加载用户最近消息
    bool MessageRepository::loadRecentForUser(const std::string &username, std::vector<StoredMessage> &out_messages,int limit)
    {

    }

    bool MessageRepository::exec(const char *sql){
        char* error_message = nullptr;

        if (const int rc = sqlite3_exec(m_db,sql,nullptr,nullptr,&error_message);
            rc != SQLITE_OK)
        {
            std::cerr <<"SQLite error:" << (error_message ? error_message : "unknown") << std::endl;
            //字符串是 SQLite 分配的，所以用完必须
            sqlite3_free(error_message);
            return false;
        }
        return true;
    }

    void MessageRepository::close() {
        if (m_db != nullptr) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
    }

    void MessageRepository::log(const std::string &operation) const {
        std::cerr << operation << ":" << sqlite3_errmsg(m_db) << std::endl;
    }
}
