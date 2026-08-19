
#include "server_runtime_config.h"

#include <charconv>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace {
// 功能：只在整个无符号整数文本和范围均可解析时写入输出参数，避免失败污染调用方。
bool parseUnsignedInteger(std::string_view text, std::uint64_t &output) {
    if (text.empty()) {
        return false;
    }
    std::uint64_t value = 0;

    const char* const begin = text.data();
    const char* const end = text.data() + text.size();

    const auto [ptr, ec] = std::from_chars(begin, end, value);

    if (ec != std::errc{} || ptr != end) {
        return false;
    }
    output = value;
    return true;
}
// 功能：限制本期允许的命令行选项，在线人数上限不允许通过运行参数绕过协议容量合同。
bool isSupportedOption(std::string_view option) {
    return  option == "--port"
    || option == "--database-path"
    || option == "--auth-timeout-ms";
}

// 功能：识别缺失值场景中的下一个选项标记，避免把选项名误当作前一个选项的值。
bool startsWithDoubleDash(std::string_view text)
{
    return text.size() >= 2 && text[0] == '-' && text[1] == '-';
}

// 功能：统一构造失败结果，确保失败路径不会泄露局部候选配置。
app::ParseRuntimeConfigResult makeError(const app::ServerRuntimeConfigError error) {
    return {std::nullopt,error};
}
}

namespace app {
    // 功能：在局部 candidate 中完成全部参数试错；只有循环成功结束后才提交有效配置。
    ParseRuntimeConfigResult parseServerRuntimeConfig(int argc, char *argv[])
    {

        ServerRuntimeConfig candidate;

        std::unordered_set<std::string> seen_options;

        for (int index = 1; index < argc; ++ index) {
            const std::string option{argv[index]};

            if (!isSupportedOption(option)) {return makeError(ServerRuntimeConfigError::unknownOption);}

            if (seen_options.find(option) != seen_options.end()) {return makeError(ServerRuntimeConfigError::duplicateOption);}

            seen_options.emplace(option);

            if (index + 1 >= argc) {
                return makeError(ServerRuntimeConfigError::missingOptionValue);
            }
            if (const std::string_view next{argv[index + 1]};
                startsWithDoubleDash(next)) {
                return makeError(ServerRuntimeConfigError::missingOptionValue);
            }

            const std::string_view value{argv[++index]};

            if (option == "--port")
            {
                std::uint64_t parsed = 0;

                if (!parseUnsignedInteger(value, parsed) ||parsed < 1 ||parsed > 65535) {
                    return makeError(ServerRuntimeConfigError::invalidPort);
                    }
                candidate.port =
                    static_cast<std::uint16_t>(parsed);
                continue;
            }

            if (option == "--database-path")
            {
                if (value.empty()) {
                    return makeError(ServerRuntimeConfigError::invalidDatabasePath);
                }
                candidate.database_path =
                    std::filesystem::path{value};
                continue;
            }

            if (option == "--auth-timeout-ms")
            {
                std::uint64_t parsed = 0;

                if (!parseUnsignedInteger(value, parsed) ||parsed < 1000 ||parsed > 30000) {
                    return makeError(ServerRuntimeConfigError::invalidAuthTimeout);
                    }
                candidate.authentication_timeout =
                    std::chrono::milliseconds{parsed};
            }
        }

        return {std::move(candidate),ServerRuntimeConfigError::none};
    }

    // 功能：集中维护错误枚举与外部稳定文本的唯一映射，避免解析逻辑散落字符串字面量。
    std::string_view serverRuntimeConfigErrorCode(const ServerRuntimeConfigError error) noexcept {
        switch (error){
            case ServerRuntimeConfigError::none:
                return "";
            case ServerRuntimeConfigError::missingOptionValue:
                return "missing_option_value";
            case ServerRuntimeConfigError::duplicateOption:
                return "duplicate_option";
            case ServerRuntimeConfigError::unknownOption:
                return "unknown_option";
            case ServerRuntimeConfigError::invalidPort:
                return "invalid_port";
            case ServerRuntimeConfigError::invalidDatabasePath:
                return "invalid_database_path";
            case ServerRuntimeConfigError::invalidAuthTimeout:
                return "invalid_auth_timeout";
        }
        return {"invalid_runtime_config_error"};
    }
}
