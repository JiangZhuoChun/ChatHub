#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <sqlite3.h>

namespace repository{
    //存储结果枚举（storeOrGetExisting 的返回值）
    enum class StoreResult{
        Stored,//新插入成功
        DuplicateSame,//完全重复: 同一 sender+local_id，正文也相同
        IdempotencyConflict,//冲突: 同一local_id，但正文、接收者、时间不同
        DatabaseError//数据库失败
    };
    //持久记录结构（StoreOutcome——带状态 + 完整持久数据）
    struct StoreOutcome {
        StoreResult result;
        //持久记录字段(result != DatabaseError时有效)
        std::string message_id;// 服务端 UUID（Stored 时新生成，DuplicateSame 时既有）
        std::string sender;
        std::string recipient;
        std::string content;
        std::string client_send_at;
        std::int64_t server_received_at_ms;
    };
    //历史记录结构（loadRecentForUser 返回的单条
    struct StoredMessage {
        std::string message_id;
        std::string sender;
        std::string recipient;
        std::string content;
        std::string client_send_at;
        std::string client_local_id;
        std::int64_t server_received_at_ms;
    };
    //历史记录游标结构
    struct HistoryCursor {
        std::int64_t server_received_at_ms;
        std::string message_id;
    };
    //历史记录查询结果结构（loadRecentForUser 返回的完整结构）
    struct HistoryQueryResult {
        std::vector<StoredMessage> messages;//本页消息
        bool has_more {false};//是否存在更早一页
        std::optional<HistoryCursor> next_cursor;//下一页游标（如果 has_more == true）
    };
    //封装一次发送请求的完整参数
    struct NewMessage {
        std::string sender;
        std::string recipient;
        std::string content;
        std::string client_send_at;
        std::string client_local_id;
        std::int64_t server_received_at_ms;
    };

    class MessageRepository {
    public:
        MessageRepository() = default;
        ~MessageRepository();

        MessageRepository(const MessageRepository&) = delete;
        MessageRepository& operator=(const MessageRepository&) = delete;

        //打开/迁移数据库；返回 true 表示可用
        bool open(const std::string& db_path);

        //存储消息；重复时按幂等规则返回既有记录或冲突
        StoreOutcome storeOrGetExisting(const NewMessage& message);

        // 加载某用户参与的最近消息（按 server_received_at_ms 排序）
        // 返回完整查询结果
        bool loadRecentForUser(const std::string& username,const std::optional<HistoryCursor>& before,int limit,HistoryQueryResult& out_result);

    private:
        bool exec(const char* sql);

        void close();

        void log(const std::string& operation) const;

        sqlite3* m_db = nullptr;
    };
}
