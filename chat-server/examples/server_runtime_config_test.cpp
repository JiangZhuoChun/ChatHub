#include "server_runtime_config.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

// 功能：使用拥有参数文本的 vector 构造 argc/argv，避免测试传入悬空 char*。
app::ParseRuntimeConfigResult parseArguments(std::vector<std::string> arguments) {
    std::vector<char*> argv;
    argv.reserve(arguments.size());
    for (auto& argument : arguments) {
        argv.push_back(argument.data());
    }
    return app::parseServerRuntimeConfig(static_cast<int>(argv.size()), argv.data());
}

// 功能：集中确认失败结果不携带半成品配置，并保持预期的内部错误枚举。
bool hasError(const app::ParseRuntimeConfigResult& result,
              const app::ServerRuntimeConfigError expected_error) {
    return !result.config.has_value() && result.error == expected_error;
}

bool testDefaults() {
    const auto result = parseArguments({"chat-server"});
    return result.config.has_value() && result.error == app::ServerRuntimeConfigError::none &&
           result.config->port == 9000 && result.config->database_path == "chathub.db" &&
           result.config->authentication_timeout.count() == 5000;
}

bool testValidOverrides() {
    const auto result = parseArguments({"chat-server", "--port", "9001", "--database-path",
                                        "test-data.db", "--auth-timeout-ms", "1200"});
    return result.config.has_value() && result.error == app::ServerRuntimeConfigError::none &&
           result.config->port == 9001 && result.config->database_path == "test-data.db" &&
           result.config->authentication_timeout.count() == 1200;
}

bool testInvalidArguments() {
    return hasError(parseArguments({"chat-server", "--unknown", "value"}),
                    app::ServerRuntimeConfigError::unknownOption) &&
           hasError(parseArguments({"chat-server", "--max-online-users", "88"}),
                    app::ServerRuntimeConfigError::unknownOption) &&
           hasError(parseArguments({"chat-server", "--port", "9000", "--port", "9001"}),
                    app::ServerRuntimeConfigError::duplicateOption) &&
           hasError(parseArguments({"chat-server", "--port"}),
                    app::ServerRuntimeConfigError::missingOptionValue) &&
           hasError(parseArguments({"chat-server", "--database-path"}),
                    app::ServerRuntimeConfigError::missingOptionValue) &&
           hasError(parseArguments({"chat-server", "--auth-timeout-ms"}),
                    app::ServerRuntimeConfigError::missingOptionValue) &&
           hasError(parseArguments({"chat-server", "--port", "--auth-timeout-ms", "5000"}),
                    app::ServerRuntimeConfigError::missingOptionValue) &&
           hasError(parseArguments({"chat-server", "--database-path", "--port", "9000"}),
                    app::ServerRuntimeConfigError::missingOptionValue) &&
           hasError(parseArguments({"chat-server", "--port", "0"}),
                    app::ServerRuntimeConfigError::invalidPort) &&
           hasError(parseArguments({"chat-server", "--port", "65536"}),
                    app::ServerRuntimeConfigError::invalidPort) &&
           hasError(parseArguments({"chat-server", "--port", "9000abc"}),
                    app::ServerRuntimeConfigError::invalidPort) &&
           hasError(parseArguments({"chat-server", "--database-path", ""}),
                    app::ServerRuntimeConfigError::invalidDatabasePath) &&
           hasError(parseArguments({"chat-server", "--auth-timeout-ms", "999"}),
                    app::ServerRuntimeConfigError::invalidAuthTimeout) &&
           hasError(parseArguments({"chat-server", "--auth-timeout-ms", "30001"}),
                    app::ServerRuntimeConfigError::invalidAuthTimeout) &&
           hasError(parseArguments({"chat-server", "--auth-timeout-ms", "1200ms"}),
                    app::ServerRuntimeConfigError::invalidAuthTimeout);
}

bool testStableErrorCodeMapping() {
    return app::serverRuntimeConfigErrorCode(app::ServerRuntimeConfigError::none).empty() &&
           app::serverRuntimeConfigErrorCode(app::ServerRuntimeConfigError::missingOptionValue) ==
               "missing_option_value" &&
           app::serverRuntimeConfigErrorCode(app::ServerRuntimeConfigError::duplicateOption) ==
               "duplicate_option" &&
           app::serverRuntimeConfigErrorCode(app::ServerRuntimeConfigError::unknownOption) ==
               "unknown_option" &&
           app::serverRuntimeConfigErrorCode(app::ServerRuntimeConfigError::invalidPort) ==
               "invalid_port" &&
           app::serverRuntimeConfigErrorCode(app::ServerRuntimeConfigError::invalidDatabasePath) ==
               "invalid_database_path" &&
           app::serverRuntimeConfigErrorCode(app::ServerRuntimeConfigError::invalidAuthTimeout) ==
               "invalid_auth_timeout" &&
           app::serverRuntimeConfigErrorCode(
               static_cast<app::ServerRuntimeConfigError>(999)) == "invalid_runtime_config_error";
}

bool runTest(const char* name, const bool passed) {
    if (passed) {
        std::cout << "PASS: " << name << '\n';
        return true;
    }
    std::cerr << "FAIL: " << name << '\n';
    return false;
}

} // namespace

int main() {
    const bool defaults_passed = runTest("runtime config defaults", testDefaults());
    const bool overrides_passed = runTest("runtime config valid overrides", testValidOverrides());
    const bool invalid_arguments_passed =
        runTest("runtime config invalid arguments", testInvalidArguments());
    const bool error_code_mapping_passed =
        runTest("runtime config stable error code mapping", testStableErrorCodeMapping());
    return defaults_passed && overrides_passed && invalid_arguments_passed &&
                   error_code_mapping_passed
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
