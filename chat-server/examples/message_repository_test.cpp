#include "repository/message_repository.h"

#include <filesystem>
#include <iostream>
#include <system_error>
#include <type_traits>
#include <optional>
namespace fs = std::filesystem;
namespace {

// 功能：验证 SQLite 不能将目录作为数据库文件打开，Repository 会将该失败转换为 false。
bool testOpenFailsForDirectory() {
    namespace fs = std::filesystem;

    std::error_code error;
    const fs::path database_directory =
        fs::temp_directory_path(error) / "chathub_repository_open_failure";
    if (error) {
        std::cerr << "无法获取临时目录：" << error.message() << '\n';
        return false;
    }

    fs::remove_all(database_directory, error);
    if (error) {
        std::cerr << "无法清理旧测试目录：" << error.message() << '\n';
        return false;
    }

    if (!fs::create_directory(database_directory, error) || error) {
        std::cerr << "无法创建测试目录：" << error.message() << '\n';
        return false;
    }

    repository::MessageRepository repository;
    const bool opened = repository.open(database_directory.string());

    std::error_code cleanup_error;
    fs::remove_all(database_directory, cleanup_error);
    if (cleanup_error) {
        std::cerr << "无法清理测试目录：" << cleanup_error.message() << '\n';
        return false;
    }

    return !opened;
}

// 功能：验证 MessageRepository 的存储和加载功能。
bool textLoadMessage() {
    const fs::path db_path= fs::temp_directory_path() / "message_repository_test.db";

    // 避免上一次异常退出留下旧测试库。
    fs::remove(db_path);
    {
        repository::MessageRepository repository;

        if (!repository.open(db_path.string())) {
            std::cerr << "无法打开数据库文件" << '\n';
            return false;
        }
        // Alice -> Bob, timestamp = 1000
        {
            repository::NewMessage message;
            message.sender = "Alice";
            message.recipient = "Bob";
            message.client_local_id = "alice-local-001";
            message.content = "message at 1000";
            message.client_send_at = "client-time-1000-a";
            message.server_received_at_ms = 1000;
            repository.storeOrGetExisting(message);
        }
        // Alice -> Bob, timestamp = 2000
        {
            repository::NewMessage message;
            message.sender = "Alice";
            message.recipient = "Bob";
            message.client_local_id = "alice-local-002";
            message.content = "first message at 2000";
            message.client_send_at = "client-time-2000-b";
            message.server_received_at_ms = 2000;

            repository.storeOrGetExisting(message);
        }
        // Alice -> Bob, timestamp = 2000
        {
            repository::NewMessage message;
            message.sender = "Alice";
            message.recipient = "Bob";
            message.client_local_id = "alice-local-003";
            message.content = "first message at 2000";
            message.client_send_at = "client-time-2000-c";
            message.server_received_at_ms = 2000;

            repository.storeOrGetExisting(message);
        }
        // Bob -> Alice, timestamp = 2000
        {
            repository::NewMessage message;
            message.sender = "Bob";
            message.recipient = "Alice";
            message.client_local_id = "bob-local-001";
            message.content = "second message at 2000";
            message.client_send_at = "client-time-2000";
            message.server_received_at_ms = 2000;

            repository.storeOrGetExisting(message);
        }
        // Carol -> Dave, timestamp = 3000
        {
            repository::NewMessage message;
            message.sender = "Carol";
            message.recipient = "Dave";
            message.client_local_id = "carol-local-001";
            message.content = "unrelated message";
            message.client_send_at = "client-time-3000";
            message.server_received_at_ms = 3000;
            repository.storeOrGetExisting(message);
        }

        //验证“首页查询”的业务合同
        //  用户隔离
        // + 首页 limit
        // + has_more
        // + 复合排序/next_cursor
        repository::HistoryQueryResult result;
        const bool ok =repository.loadRecentForUser("Alice",std::nullopt,2,result);
        if (!ok) {
            std::cerr << "无法加载消息" << '\n';
            return false;
        }
        if (result.messages.size() != 2) {
            std::cerr << "expected 2 messages, got " << result.messages.size() << '\n';
            return false;
        }
        //验证两条消息都属于 Alice
        for (const auto& message : result.messages) {
            const bool related_to_alice = message.sender == "Alice" || message.recipient == "Alice";
            if (!related_to_alice) {
                std::cerr << "message not related to Alice: "  << '\n';
                return false;
            }
            if (message.sender == "Carol" && message.recipient == "Dave") {
                std::cerr << "Carol -> Dave message leaked into Alice result\n";
                return false;
            }
        }
        //验证两条都是时间戳 2000
        if (result.messages[0].server_received_at_ms != 2000 || result.messages[1].server_received_at_ms != 2000) {
            std::cerr << "expected both first-page messages ""to have timestamp 2000\n";
            return false;
        }
        if (!result.has_more) {
            std::cerr << "expected has_more == true\n";
            return false;
        }
        //next_cursor 必须存在，且应对应结果中第 0 条消息（页面按时间升序展示时它是本页最旧的一条）
        if (!result.next_cursor.has_value()) {
            std::cerr << "expected next_cursor to exist\n";
            return false;
        }
        if (result.next_cursor->server_received_at_ms != result.messages[0].server_received_at_ms ||
            result.next_cursor->message_id != result.messages[0].message_id ) {
            std::cerr << "next_cursor does not match ""the oldest message on the page\n";
            return false;
        }
        //验证同毫秒下的稳定排序
        if (result.messages[0].message_id >= result.messages[1].message_id) {
            std::cerr << "expected message_id ascending order ""for equal timestamps\n";
            return false;
        }

        //验证复合游标在“同一个毫秒有多条消息”时不会重复、不会漏消息。
        repository::HistoryQueryResult older_page;
        const bool order_ok = repository.loadRecentForUser("Alice",result.next_cursor,2,older_page);
        if (!order_ok) {
            std::cerr << "loadRecentForUser older page failed\n";
            return false;
        }
        if (older_page.messages.size() != 2) {
            std::cerr << "expected 2 messages in older page, got "<< older_page.messages.size() << '\n';
            return false;
        }
        //重点验证第一页和第二页没有重复 message_id
        for (const auto& first : result.messages) {
            for (const auto& older : older_page.messages) {
                if (first.message_id == older.message_id) {
                    std::cerr << "duplicate message across pages: "<< first.message_id << '\n';
                    return false;
                }
            }
        }
        // 验证同毫秒下，第二页继续取得 cursor 之前的那条消息。
        if (older_page.messages[0].server_received_at_ms != 1000 ||
            older_page.messages[1].server_received_at_ms != 2000 ||
            older_page.messages[1].message_id >= result.next_cursor->message_id) {
            std::cerr << "same-timestamp cursor did not return the expected older message\n";
            return false;
        }

        bool found_1000 = false;
        for (const auto& message : older_page.messages){
            if (message.server_received_at_ms == 1000)
            {found_1000 = true;}
        }
        if (!found_1000) {
            std::cerr << "timestamp 1000 message was skipped\n";
            return false;
        }

    }
    std::error_code ec;
    fs::remove(db_path,ec);
    if (ec) {
        std::cerr << "无法删除测试数据库文件：" << ec.message() << '\n';
        return false;
    }
    return true;
}

// 功能：验证同一发送者重复使用 local_id 时，完全重复会复用原记录，冲突重复不会改写原记录。
bool testStoreIdempotency()
{
    const fs::path db_path = fs::temp_directory_path() / "message_repository_idempotency_test.db";
    std::error_code error;
    fs::remove(db_path, error);
    if (error) {
        std::cerr << "无法清理旧幂等测试数据库：" << error.message() << '\n';
        return false;
    }

    {
        repository::MessageRepository repository;
        if (!repository.open(db_path.string())) {
            std::cerr << "无法打开幂等测试数据库" << '\n';
            return false;
        }

        repository::NewMessage first_message;
        first_message.sender = "Alice";
        first_message.recipient = "Bob";
        first_message.client_local_id = "alice-duplicate-001";
        first_message.content = "只应保存一次";
        first_message.client_send_at = "2026-08-17T12:00:00.000Z";
        first_message.server_received_at_ms = 1000;

        const repository::StoreOutcome first_outcome = repository.storeOrGetExisting(first_message);
        if (first_outcome.result != repository::StoreResult::Stored || first_outcome.message_id.empty()) {
            std::cerr << "首次写入没有返回 Stored 和持久 message_id" << '\n';
            return false;
        }

        repository::NewMessage replayed_message = first_message;
        replayed_message.server_received_at_ms = 2000;
        const repository::StoreOutcome replayed_outcome = repository.storeOrGetExisting(replayed_message);
        if (replayed_outcome.result != repository::StoreResult::DuplicateSame ||
            replayed_outcome.message_id != first_outcome.message_id ||
            replayed_outcome.server_received_at_ms != first_outcome.server_received_at_ms) {
            std::cerr << "完全重复请求没有复用原持久记录" << '\n';
            return false;
        }

        repository::NewMessage conflicting_message = first_message;
        conflicting_message.content = "同一 local_id 但正文不同";
        const repository::StoreOutcome conflict_outcome = repository.storeOrGetExisting(conflicting_message);
        if (conflict_outcome.result != repository::StoreResult::IdempotencyConflict) {
            std::cerr << "冲突重复请求没有返回 IdempotencyConflict" << '\n';
            return false;
        }

        repository::HistoryQueryResult history;
        if (!repository.loadRecentForUser("Alice", std::nullopt, 50, history)) {
            std::cerr << "无法查询幂等测试历史记录" << '\n';
            return false;
        }
        if (history.messages.size() != 1 ||
            history.messages.front().message_id != first_outcome.message_id ||
            history.messages.front().content != first_message.content) {
            std::cerr << "重复请求改变了数据库中的原始记录" << '\n';
            return false;
        }
    }

    fs::remove(db_path, error);
    if (error) {
        std::cerr << "无法删除幂等测试数据库：" << error.message() << '\n';
        return false;
    }
    return true;
}


    //分别验证：MessageRepository 不可复制构造；
    // MessageRepository 不可复制赋值。
    static_assert(!std::is_copy_constructible_v<repository::MessageRepository>,
              "MessageRepository must not be copy constructible");

    static_assert(!std::is_copy_assignable_v<repository::MessageRepository>,
                  "MessageRepository must not be copy assignable");

// 功能：输出单项测试结果，并将布尔结果返回给测试入口。
bool runTest(const char* name, const bool passed) {
    if (passed) {
        std::cout << "PASS: " << name << '\n';
        return true;
    }

    std::cerr << "FAIL: " << name << '\n';
    return false;
}

} // namespace

// 功能：执行 Repository 的失败路径测试，并以进程退出码交给 CTest 判断结果。
int main() {
    const bool open_failure_passed = runTest("database open failure", testOpenFailsForDirectory());
    const bool load_store_passed = runTest("load and store messages", textLoadMessage());
    const bool idempotency_passed = runTest("store idempotency", testStoreIdempotency());
    return open_failure_passed && load_store_passed && idempotency_passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
