#include "protocol/chat_protocol.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <asio.hpp>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

using asio::ip::tcp;
using namespace std::chrono_literals;

constexpr auto kProcessStartupTimeout = 3s;
constexpr auto kAuthenticationReadTimeout = 3s;

class ScopedTestDirectory {
public:
    ScopedTestDirectory()
        : m_path(std::filesystem::temp_directory_path() /
                 ("chathub-process-config-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directories(m_path);
    }

    ~ScopedTestDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    const std::filesystem::path& path() const {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

class ServerProcess {
public:
    ~ServerProcess() {
        stop();
    }

    bool start(const std::filesystem::path& executable,
               const std::vector<std::wstring>& arguments,
               const std::filesystem::path& working_directory) {
        std::wstring command_line = quote(executable.wstring());
        for (const std::wstring& argument : arguments) {
            command_line += L" ";
            command_line += quote(argument);
        }

        SECURITY_ATTRIBUTES pipe_attributes{};
        pipe_attributes.nLength = sizeof(pipe_attributes);
        pipe_attributes.bInheritHandle = TRUE;

        HANDLE standard_output_read = nullptr;
        HANDLE standard_output_write = nullptr;
        if (!::CreatePipe(&standard_output_read,
                          &standard_output_write,
                          &pipe_attributes,
                          0) ||
            !::SetHandleInformation(standard_output_read, HANDLE_FLAG_INHERIT, 0)) {
            if (standard_output_read != nullptr) {
                ::CloseHandle(standard_output_read);
            }
            if (standard_output_write != nullptr) {
                ::CloseHandle(standard_output_write);
            }
            return false;
        }

        STARTUPINFOW startup_info{};
        startup_info.cb = sizeof(startup_info);
        startup_info.dwFlags = STARTF_USESTDHANDLES;
        startup_info.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
        startup_info.hStdOutput = standard_output_write;
        startup_info.hStdError = standard_output_write;
        PROCESS_INFORMATION process_info{};

        const bool process_started = ::CreateProcessW(executable.c_str(),
                                                       command_line.data(),
                                                       nullptr,
                                                       nullptr,
                                                       TRUE,
                                                       CREATE_NO_WINDOW,
                                                       nullptr,
                                                       working_directory.c_str(),
                                                       &startup_info,
                                                       &process_info) != FALSE;
        ::CloseHandle(standard_output_write);
        if (!process_started) {
            ::CloseHandle(standard_output_read);
            return false;
        }

        m_process = process_info.hProcess;
        m_standard_output_read = standard_output_read;
        ::CloseHandle(process_info.hThread);
        return true;
    }

    bool waitsForStandardOutput(const std::vector<std::string_view>& required_fragments,
                                const std::chrono::milliseconds timeout) const {
        std::string output;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            output += readAvailableStandardOutput();

            bool contains_all_fragments = true;
            for (const std::string_view fragment : required_fragments) {
                if (output.find(fragment) == std::string::npos) {
                    contains_all_fragments = false;
                    break;
                }
            }
            if (contains_all_fragments) {
                return true;
            }
            std::this_thread::sleep_for(20ms);
        }
        return false;
    }

    bool waitForExit(const std::chrono::milliseconds timeout, DWORD& exit_code) const {
        if (m_process == nullptr ||
            ::WaitForSingleObject(m_process, static_cast<DWORD>(timeout.count())) != WAIT_OBJECT_0) {
            return false;
        }
        return ::GetExitCodeProcess(m_process, &exit_code) != FALSE;
    }

    void stop() {
        if (m_process == nullptr) {
            return;
        }

        if (::WaitForSingleObject(m_process, 0) == WAIT_TIMEOUT) {
            ::TerminateProcess(m_process, EXIT_SUCCESS);
            ::WaitForSingleObject(m_process, static_cast<DWORD>(kProcessStartupTimeout.count()));
        }
        ::CloseHandle(m_process);
        m_process = nullptr;
        if (m_standard_output_read != nullptr) {
            ::CloseHandle(m_standard_output_read);
            m_standard_output_read = nullptr;
        }
    }

private:
    std::string readAvailableStandardOutput() const {
        if (m_standard_output_read == nullptr) {
            return {};
        }

        std::string output;
        DWORD available = 0;
        while (::PeekNamedPipe(m_standard_output_read, nullptr, 0, nullptr, &available, nullptr) &&
               available > 0) {
            std::array<char, 512> buffer{};
            DWORD bytes_read = 0;
            const DWORD bytes_to_read = std::min<DWORD>(available, buffer.size());
            if (!::ReadFile(m_standard_output_read,
                            buffer.data(),
                            bytes_to_read,
                            &bytes_read,
                            nullptr) ||
                bytes_read == 0) {
                break;
            }
            output.append(buffer.data(), bytes_read);
        }
        return output;
    }

    static std::wstring quote(const std::wstring& value) {
        return L"\"" + value + L"\"";
    }

    HANDLE m_process{nullptr};
    HANDLE m_standard_output_read{nullptr};
};

std::uint16_t findAvailablePort() {
    asio::io_context io_context;
    tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 0));
    return acceptor.local_endpoint().port();
}

bool connectBeforeDeadline(tcp::socket& socket,
                           const std::uint16_t port,
                           const std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const tcp::endpoint endpoint(asio::ip::address_v4::loopback(), port);

    while (std::chrono::steady_clock::now() < deadline) {
        std::error_code error;
        socket.connect(endpoint, error);
        if (!error) {
            return true;
        }
        socket.close(error);
        socket.open(tcp::v4(), error);
        if (error) {
            return false;
        }
        std::this_thread::sleep_for(20ms);
    }
    return false;
}

bool readExactlyBeforeDeadline(tcp::socket& socket,
                               char* data,
                               const std::size_t size,
                               const std::chrono::steady_clock::time_point deadline) {
    std::size_t received = 0;
    while (received < size) {
        std::error_code error;
        const auto bytes_read = socket.read_some(asio::buffer(data + received, size - received), error);
        received += bytes_read;

        if (!error) {
            continue;
        }
        if (error != asio::error::would_block && error != asio::error::try_again) {
            return false;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(5ms);
    }
    return true;
}

bool receivesAuthenticationTimeout(tcp::socket& socket,
                                   std::chrono::milliseconds& elapsed) {
    std::error_code error;
    socket.non_blocking(true, error);
    if (error) {
        return false;
    }

    const auto started_at = std::chrono::steady_clock::now();
    const auto deadline = started_at + kAuthenticationReadTimeout;
    std::array<char, protocol::kFrameHeaderLength> header{};
    if (!readExactlyBeforeDeadline(socket, header.data(), header.size(), deadline)) {
        return false;
    }

    const std::uint32_t body_size =
        (static_cast<std::uint32_t>(static_cast<unsigned char>(header[4])) << 24U) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(header[5])) << 16U) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(header[6])) << 8U) |
        static_cast<std::uint32_t>(static_cast<unsigned char>(header[7]));
    if (header[3] != static_cast<char>(protocol::MessageType::error) ||
        body_size > protocol::kMaxFrameBodyLength) {
        return false;
    }

    std::string body(body_size, '\0');
    if (!readExactlyBeforeDeadline(socket, body.data(), body.size(), deadline)) {
        return false;
    }

    elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at);
    return body.find("\"scope\":\"auth\"") != std::string::npos &&
           body.find("\"code\":\"authentication_timeout\"") != std::string::npos;
}

bool canBindPort(const std::uint16_t port) {
    try {
        asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), port));
        return true;
    } catch (const std::system_error&) {
        return false;
    }
}

bool testInvalidConfigurationHasNoSideEffects(const std::filesystem::path& server_executable) {
    ScopedTestDirectory test_directory;
    const auto port = findAvailablePort();
    const auto database_path = test_directory.path() / "must-not-exist.db";
    ServerProcess process;

    if (!process.start(server_executable,
                       {L"--port", std::to_wstring(port),
                        L"--database-path", database_path.filename().wstring(),
                        L"--auth-timeout-ms", L"1"},
                       test_directory.path())) {
        return false;
    }

    DWORD exit_code = EXIT_SUCCESS;
    return process.waitForExit(kProcessStartupTimeout, exit_code) && exit_code != EXIT_SUCCESS &&
           !std::filesystem::exists(database_path) && canBindPort(port);
}

bool testCustomConfigurationReachesRealServer(const std::filesystem::path& server_executable) {
    ScopedTestDirectory test_directory;
    const auto port = findAvailablePort();
    const auto database_path = test_directory.path() / "custom-runtime.db";
    ServerProcess process;

    if (!process.start(server_executable,
                       {L"--port", std::to_wstring(port),
                        L"--database-path", database_path.filename().wstring(),
                        L"--auth-timeout-ms", L"1000"},
                       test_directory.path())) {
        return false;
    }

    if (!process.waitsForStandardOutput({"server_started",
                                         "database_path=\"custom-runtime.db\"",
                                         "auth_timeout_ms=1000"},
                                        kProcessStartupTimeout)) {
        return false;
    }

    asio::io_context client_io;
    tcp::socket client_socket(client_io);
    if (!connectBeforeDeadline(client_socket, port, kProcessStartupTimeout)) {
        return false;
    }

    std::chrono::milliseconds timeout_elapsed{0};
    return std::filesystem::exists(database_path) &&
           receivesAuthenticationTimeout(client_socket, timeout_elapsed) &&
           timeout_elapsed >= 800ms && timeout_elapsed <= 2500ms;
}

bool runTest(const char* name, const bool passed) {
    std::cout << (passed ? "PASS: " : "FAIL: ") << name << '\n';
    return passed;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "server_process_config_test requires the chat-server executable path\n";
        return EXIT_FAILURE;
    }

    const std::filesystem::path server_executable = std::filesystem::absolute(argv[1]);
    const bool invalid_config_passed =
        runTest("invalid configuration exits without listener or database",
                testInvalidConfigurationHasNoSideEffects(server_executable));
    const bool custom_config_passed =
        runTest("custom port, database path, and authentication timeout reach the process",
                testCustomConfigurationReachesRealServer(server_executable));
    return invalid_config_passed && custom_config_passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
