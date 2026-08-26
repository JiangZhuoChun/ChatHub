#pragma once

#include <functional>
#include <string>
#include <chrono>
#include <cstddef>

namespace auth
{
struct AuthIntrospectionConfig
{
    std::string host;//Auth Service 主机
    std::string port;//HTTP 端口，先保存为字符串，便于交给 Asio resolver；
    std::string target{"/internal/auth/introspect"};//HTTP 请求路径，不保存完整 URL；
    std::string internal_service_key;//服务凭证，不能记录日志；
    std::chrono::milliseconds timeout{2000};//一次 introspection 最大等待时间；
    std::size_t max_response_body_bytes{4096};//限制异常响应占用内存。
};
enum class IntrospectionStatus
{
    active,//本次查询时 token 有效且未撤销
    authentication_rejected,//用户 token 无效、过期或已撤销
    dependency_unavailable//依赖服务不可用
};

struct IntrospectionResult
{
    IntrospectionStatus status{IntrospectionStatus::dependency_unavailable};
    std::string username;
};

using IntrospectionHandler = std::function<void(IntrospectionResult)>;

class IAuthIntrospectionClient
{
public:
    virtual ~IAuthIntrospectionClient() = default;
    //因为 HTTP 是异步的，调用函数返回后，原来的协议帧对象可能已经销毁；
    //客户端必须拥有自己的 token 副本。
    virtual void introspect(std::string token, IntrospectionHandler handler) = 0;
};

}