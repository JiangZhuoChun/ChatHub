#include <iostream>
#include <net/server.h>
#include <asio.hpp>
#include <thread>
int main() {
    try {
        asio::io_context io_context;
        net::Server server(io_context, 9000);

        server.start();

        // 两个 worker 并行运行事件循环；Server 和每个 Session 分别用 strand 保护共享状态。
        std::thread worker1([&]{io_context.run();});
        std::thread worker2([&]{io_context.run();});
        worker1.join();
        worker2.join();
    }
    catch (const std::exception& error) {
        // 捕获端口绑定等同步初始化错误，避免程序无提示退出。
        std::cerr << "服务器启动失败：" << error.what() << std::endl;
        return 1;
    }
    return 0;
}
