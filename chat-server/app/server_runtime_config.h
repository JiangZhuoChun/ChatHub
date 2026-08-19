#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
namespace app
{
    // 功能：枚举配置解析失败的稳定分类，供解析器和外部错误文本映射共同使用。
    enum class ServerRuntimeConfigError{
        none,
        missingOptionValue,
        duplicateOption,
        unknownOption,
        invalidPort,
        invalidDatabasePath,
        invalidAuthTimeout,
    };

    // 功能：保存全部合法的 ChatServer 启动参数及其默认值。
    struct ServerRuntimeConfig {
        std::uint16_t port{9000};
        std::filesystem::path database_path{"chathub.db"};
        std::chrono::milliseconds authentication_timeout{5000};
    };

    // 功能：区分有效配置与稳定失败分类；成功时 config 有值且 error 为 none。
    struct ParseRuntimeConfigResult {
        std::optional<ServerRuntimeConfig> config{std::nullopt};
        ServerRuntimeConfigError error{ServerRuntimeConfigError::none};
    };

    // 功能：仅解析和校验命令行参数，不创建监听器、不访问数据库。
    ParseRuntimeConfigResult parseServerRuntimeConfig(int argc,char* argv[]);

    // 功能：将内部错误枚举转换为 main() 可输出和测试可断言的稳定错误码文本。
    std::string_view serverRuntimeConfigErrorCode(ServerRuntimeConfigError error) noexcept;
}
