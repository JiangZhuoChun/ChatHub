#include <asio.hpp>
#include <net/server.h>
#include "server_runtime_config.h"

#include <iostream>
#include <thread>
#include <cstdlib>


// ==================== 模块：聊天服务器启动入口 ====================
// 功能：创建 Asio 事件循环和聊天服务器，并由两个工作线程持续处理网络事件。
// 失败：端口绑定等同步初始化异常时输出原因并以失败状态退出。
int main(int argc, char* argv[]) {
    const auto result = app::parseServerRuntimeConfig(argc, argv);
    if (!result.config) {
        std::cerr  << "configuration_error: "
                   << app::serverRuntimeConfigErrorCode(result.error)
                   << std::endl;
        return EXIT_FAILURE;
    }
    const auto& config = *result.config;

    try {

        asio::io_context io_context;
        net::Server server(io_context,
                            config.port,
                            config.database_path.string(),
                            config.authentication_timeout);
        server.start();

        std::thread worker1(
            // 功能：在第一个工作线程中运行 Asio 事件循环。
            [&] {
                io_context.run();
            });
        std::thread worker2(
            // 功能：在第二个工作线程中运行 Asio 事件循环。
            [&] {
                io_context.run();
            });
        worker1.join();
        worker2.join();
    } catch (const std::exception& error) {
        std::cerr << "服务器启动失败：" << error.what() << std::endl;
        return 1;
    }

    return 0;
}
