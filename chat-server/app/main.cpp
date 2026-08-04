#include <iostream>
#include <net/server.h>
#include <asio.hpp>
#include <thread>
//功能::程序入口：创建事件循环、Server，用两个 worker 跑起来
int main() {
    try {
        asio::io_context io_context;   //功能::事件循环引擎
        net::Server server(io_context, 9000);   //功能::创建 Server，监听 9000 端口

        server.start();   //功能::开始接受连接

        //功能::两个 worker 并行运行事件循环；Server 和每个 Session 分别用 strand 保护共享状态。
        std::thread worker1([&]{io_context.run();});
        std::thread worker2([&]{io_context.run();});
        worker1.join();
        worker2.join();
    }
    catch (const std::exception& error) {
        //功能::捕获端口绑定等同步初始化错误，避免程序无提示退出。
        std::cerr << "服务器启动失败：" << error.what() << std::endl;
        return 1;
    }
    return 0;
}
