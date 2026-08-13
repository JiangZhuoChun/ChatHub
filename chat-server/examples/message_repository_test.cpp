#include "repository/message_repository.h"

#include <filesystem>
#include <iostream>
#include <system_error>

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
    return runTest("database open failure", testOpenFailsForDirectory()) ? EXIT_SUCCESS
                                                                          : EXIT_FAILURE;
}
