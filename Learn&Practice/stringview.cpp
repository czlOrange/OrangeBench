#include <iostream>
#include <string>
#include <string_view>

void print_name(std::string_view name) {
    std::cout << "Hello, " << name << "!" << std::endl;
}

int main() {
    // 场景1：从C风格字符串创建
    const char* c_name = "Alice";
    print_name(c_name);  // 输出: Hello, Alice!
    
    // 场景2：从std::string创建（零拷贝）
    std::string cpp_name = "Bob";
    print_name(cpp_name);  // 输出: Hello, Bob!
    
    // 场景3：只取部分字符串
    std::string full = "Charlie Brown";
    std::string_view first_name(full.data(), 7);  // 只取"Charlie"
    print_name(first_name);                       // 输出: Hello, Charlie!
    
    return 0;
}