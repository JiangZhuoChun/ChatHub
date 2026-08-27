#include "auth/asio_auth_introspection_client.h"

#include <boost/json.hpp>

#include <array>
#include <charconv>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
namespace
{
using namespace auth;

std::optional<int> parseHttpStatusCode(std::string_view status_line)
{
    if (status_line.rfind("HTTP/", 0) != 0)
    {
        return std::nullopt;
    }
    const auto first_space = status_line.find(' ');
    const auto second_space = status_line.find(' ', first_space + 1);
    if (first_space == std::string_view::npos || second_space == std::string_view::npos)
    {
        return std::nullopt;
    }

    int status_code = 0;
    const auto [end, error] =
        std::from_chars(status_line.data() + first_space + 1, status_line.data() + second_space, status_code);

    if (error != std::errc{} || end != status_line.data() + second_space || status_code < 100 || status_code > 599)
    {
        return std::nullopt;
    }
    return status_code;
}
std::optional<std::string> parseActiveUsername(std::string_view response_body)
{
    boost::system::error_code parse_error;
    const auto json_value =
        boost::json::parse(response_body, parse_error);

    if (parse_error || !json_value.is_object())
    {
        return std::nullopt;
    }

    const auto &object = json_value.as_object();
    const auto *active_value = object.if_contains("active");
    const auto *username_value = object.if_contains("username");

    if (active_value == nullptr ||
        username_value == nullptr ||
        !active_value->is_bool() ||
        !active_value->as_bool() ||
        !username_value->is_string())
    {
        return std::nullopt;
    }

    const auto &username = username_value->as_string();
    if (username.empty())
    {
        return std::nullopt;
    }

    return std::string(username.data(), username.size());
}

std::optional<std::string> parseResponseCode(std::string_view response_body)
{
    boost::system::error_code parse_error;
    const auto json_value = boost::json::parse(response_body, parse_error);

    if (parse_error || !json_value.is_object())
    {
        return std::nullopt;
    }

    const auto *code_value = json_value.as_object().if_contains("code");
    if (code_value == nullptr || !code_value->is_string())
    {
        return std::nullopt;
    }

    const auto &code = code_value->as_string();
    return std::string(code.data(), code.size());
}

class RequestOperation : public std::enable_shared_from_this<RequestOperation>
{
  public:
    RequestOperation(asio::io_context &io_context, AuthIntrospectionConfig config, std::string token,
                     IntrospectionHandler handler)
        : strand(io_context.get_executor()), resolver(io_context), socket(io_context), timer(io_context),
          config(std::move(config)), token(std::move(token)), handler(std::move(handler)), finished(false), read_chunk()
    {
    }
    // ---- 核心流程函数 ----
    void start();
    void startOnStrand();
    void buildRequest();
    void handleResolve(const std::error_code &error, const asio::ip::tcp::resolver::results_type &endpoints);
    void handleConnect(const std::error_code &error, const asio::ip::tcp::endpoint &endpoint);
    void handleWrite(const std::error_code &error, std::size_t bytes_transferred);
    void handleRead(const std::error_code &error, std::size_t bytes_transferred);
    void handleTimeout(const std::error_code &error);
    void finish(auth::IntrospectionResult result);

  private:
    // ---- 网络与定时资源（每请求独立） ----
    asio::strand<asio::any_io_executor> strand;
    asio::ip::tcp::resolver resolver; // DNS/地址解析
    asio::ip::tcp::socket socket;     // TCP套接字
    asio::steady_timer timer;         // 定时器
    // ---- 请求数据 ----
    AuthIntrospectionConfig config; // 当前请求所用配置
    std::string token;              // 拥有 token 副本
    std::string request_text;       // 拥有完整 HTTP 请求
    IntrospectionHandler handler;   // 最终回调
    // ---- 状态与缓冲 ----
    bool finished;                     // 防止重复回调
    std::array<char, 1024> read_chunk; // 临时读取缓冲区
    std::string response_text;         // 累积响应内容

    bool response_headers_parsed{false};
    int response_status_code{0};
};

// ----------------------------------------------------------------------------
// start(): 设置超时 → 构造请求 → 发起 DNS 解析
// ----------------------------------------------------------------------------

void RequestOperation::start()
{
    auto self = shared_from_this();

    asio::post(strand, [self] { self->startOnStrand(); });
}
void RequestOperation::startOnStrand()
{
    auto self = shared_from_this();
    // 1. 设置总超时计时器（从 start 开始计算，覆盖 DNS + 连接 + 读写）
    timer.expires_after(config.timeout);
    timer.async_wait(asio::bind_executor(strand, [self](const std::error_code &ec) { self->handleTimeout(ec); }));

    // 2. 构造完整 HTTP 请求文本（必须在 async_resolve 之前完成）
    buildRequest();

    // 3. 发起异步 DNS 解析
    resolver.async_resolve(config.host, config.port,
                           asio::bind_executor(strand, [self](const std::error_code &ec,
                                                              const asio::ip::tcp::resolver::results_type &endpoints) {
                               self->handleResolve(ec, endpoints);
                           }));
}
void RequestOperation::buildRequest()
{
    boost::json::object request_object;
    request_object["token"] = token;
    const std::string body = boost::json::serialize(request_object);

    std::ostringstream oss;
    oss << "POST " << config.target << " HTTP/1.1\r\n"
        << "Host: " << config.host << ":" << config.port << "\r\n"
        << "X-Internal-Service-Key: " << config.internal_service_key << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Accept: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;

    request_text = oss.str();
}
void RequestOperation::handleResolve(const std::error_code &error,
                                     const asio::ip::tcp::resolver::results_type &endpoints)
{
    if (finished)
    {
        return;
    }
    if (error)
    {
        finish(IntrospectionResult{IntrospectionStatus::dependency_unavailable, ""});
        return;
    }

    auto self = shared_from_this();
    asio::async_connect(
        socket, endpoints,
        asio::bind_executor(strand, [self](const std::error_code &ec, const asio::ip::tcp::endpoint &ep) {
            self->handleConnect(ec, ep);
        }));
}
void RequestOperation::handleConnect(const std::error_code &error, const asio::ip::tcp::endpoint &endpoint)
{
    if (finished)
    {
        return;
    }
    if (error)
    {
        finish(IntrospectionResult{IntrospectionStatus::dependency_unavailable, ""});
        return;
    }
    auto self = shared_from_this();
    asio::async_write(
        socket, asio::buffer(request_text),
        asio::bind_executor(strand, [self](const std::error_code &ec, const std::size_t bytes_transferred) {
            self->handleWrite(ec, bytes_transferred);
        }));
}
void RequestOperation::handleWrite(const std::error_code &error, std::size_t bytes_transferred)
{
    if (finished)
    {
        return;
    }
    if (error)
    {
        finish(IntrospectionResult{IntrospectionStatus::dependency_unavailable, ""});
        return;
    }
    auto self = shared_from_this();
    socket.async_read_some(asio::buffer(read_chunk),
                           asio::bind_executor(strand, [self](const std::error_code &ec, const std::size_t n) {
                               self->handleRead(ec, n);
                           }));
}
void RequestOperation::handleRead(const std::error_code &error, const std::size_t bytes_transferred)
{
    if (finished)
    {
        return;
    }
    if (error && error != asio::error::eof)
    {
        finish(IntrospectionResult{IntrospectionStatus::dependency_unavailable, {}});
        return;
    }
    if (bytes_transferred > 0)
    {
        // 累积已读数据，继续读取剩余部分
        response_text.append(read_chunk.data(), bytes_transferred);
    }
    if (response_text.size() > config.max_response_body_bytes)
    {
        finish(IntrospectionResult{IntrospectionStatus::dependency_unavailable, ""});
        return;
    }
    if (!response_headers_parsed)
    {
        if (const auto separator = response_text.find("\r\n\r\n"); separator != std::string::npos)
        {
            const auto first_line_end = response_text.find("\r\n");

            if (first_line_end == std::string::npos)
            {
                finish(IntrospectionResult{IntrospectionStatus::dependency_unavailable, ""});
                return;
            }

            const auto status_line = std::string_view(response_text).substr(0, first_line_end);
            const auto status_code = parseHttpStatusCode(status_line);

            if (!status_code)
            {
                finish(IntrospectionResult{IntrospectionStatus::dependency_unavailable, ""});
                return;
            }
            response_status_code = *status_code;
            response_headers_parsed = true;
            response_text.erase(0, separator + 4);

            if (response_status_code == 503)
            {
                finish({IntrospectionStatus::dependency_unavailable, ""});
                return;
            }

            if (response_status_code != 200 && response_status_code != 401)
            {
                finish({IntrospectionStatus::dependency_unavailable, ""});
                return;
            }
        }
    }

    if (error == asio::error::eof)
    {
        if (!response_headers_parsed)
        {
            finish({IntrospectionStatus::dependency_unavailable,{}});
            return;
        }

        if (response_status_code == 401)
        {
            const auto response_code = parseResponseCode(response_text);
            if (response_code && *response_code == "authentication_rejected")
            {
                finish({IntrospectionStatus::authentication_rejected, {}});
            }
            else
            {
                // internal_service_rejected、未知 code 或格式错误都属于依赖/配置故障。
                finish({IntrospectionStatus::dependency_unavailable, {}});
            }
            return;
        }

        if (response_status_code != 200)
        {
            finish({IntrospectionStatus::dependency_unavailable, {}});
            return;
        }

        const auto username = parseActiveUsername(response_text);
        if (!username)
        {
            finish({IntrospectionStatus::dependency_unavailable, {}});
            return;
        }
        finish({IntrospectionStatus::active,*username});
        return;
    }
    auto self = shared_from_this();
    socket.async_read_some(asio::buffer(read_chunk),
                           asio::bind_executor(strand, [self](const std::error_code &ec, const std::size_t n) {
                               self->handleRead(ec, n);
                           }));
}
void RequestOperation::handleTimeout(const std::error_code &error)
{

    // 如果已被正常 finish，timer 被 cancel 后会收到 operation_aborted
    if (finished || error == asio::error::operation_aborted)
    {
        return;
    }
    finish(IntrospectionResult{IntrospectionStatus::dependency_unavailable, ""});
}
// ----------------------------------------------------------------------------
// finish(): 统一终结路径，防止重复回调
// ----------------------------------------------------------------------------
void RequestOperation::finish(IntrospectionResult result)
{
    if (finished)
    {
        return;
    }
    finished = true;
    // 取消所有未完成的异步操作
    std::error_code ignore_ec;
    timer.cancel(ignore_ec);
    resolver.cancel();
    socket.close(ignore_ec);

    // 移出 handler 并调用，避免在回调中持有自身引用
    if (const auto cb = std::move(handler))
    {
        cb(std::move(result));
    }
}
} // namespace

namespace auth
{
// ============================================================================
// AsioAuthIntrospectionClient 公开接口实现
// ============================================================================

AsioAuthIntrospectionClient::AsioAuthIntrospectionClient(
    asio::io_context &io_context, AuthIntrospectionConfig config)
        :m_io_context(io_context),
        m_config(std::move(config))
{
}
void AsioAuthIntrospectionClient::introspect(std::string token, IntrospectionHandler handler)
{
    // 每次请求创建独立的 RequestOperation，资源不共享
    const auto operation =
        std::make_shared<RequestOperation>(m_io_context, m_config, std::move(token), std::move(handler));
    // start() 内部会启动异步链并通过 shared_from_this 维持生命周期
    operation->start();

    // 此处 operation 局部变量释放，但异步回调通过 self 捕获保持存活
}
} // namespace auth
