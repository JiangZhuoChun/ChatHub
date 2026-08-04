#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/kazuho-picojson/traits.h>
#include <iostream>

const std::string SECRET_KEY = "chathub-dev-secret";

int main() {
    //1.从命令行获取token
    const std::string token =
"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VybmFtZSI6ImFsaWNlIiwiaWF0IjoxNzg1ODIzMzQ4LCJleHAiOjE3ODU4MjY5NDh9.ak-U4eagxXzt1BesIMOxwcg25CfLwF7NVTg2XdlxATA";
    try {
        //2.解析token
        auto decoded = jwt::decode(token);
        //3.创建验证器 + 验证
        auto verifier = jwt::verify().allow_algorithm(jwt::algorithm::hs256{SECRET_KEY});
        verifier.verify(decoded);
        //4.验证成功,取出username
        auto username = decoded.get_payload_claim("username").as_string();
        std::cout << "JWT验证通过！ username =" << username << std::endl;
    }
    catch (const std::exception &error) {
        std::cout << "JWT验证失败！" << error.what() << std::endl;
    }
    return 0;
}