#include "defer.h"  // 您的 defer.h 头文件
#include <iostream>

// 测试1: 基本功能验证 - defer 在函数退出时执行
void test_basic() {
    std::cout << "test_basic 开始" << std::endl;
    LOG_DEFER {
        std::cout << "defer: 资源清理操作" << std::endl;
    };
    std::cout << "test_basic 中间逻辑" << std::endl;
    // 函数结束时，defer 代码块会自动执行
}

// 测试2: 验证提前返回时 defer 仍有效
void test_early_return() {
    std::cout << "test_early_return 开始" << std::endl;
    LOG_DEFER {
        std::cout << "defer: 即使提前返回也会执行" << std::endl;
    };
    if (true) {
        std::cout << "触发提前返回" << std::endl;
        return;  // defer 应在此处执行
    }
    std::cout << "这行不会执行" << std::endl; // 不会到达
}

// 测试3: 验证多个 defer 的执行顺序（LIFO）
void test_multiple_defer() {
    std::cout << "test_multiple_defer 开始" << std::endl;
    LOG_DEFER {
        std::cout << "defer: 第一个注册" << std::endl;
    };
    LOG_DEFER {
        std::cout << "defer: 第二个注册" << std::endl;
    };
    std::cout << "test_multiple_defer 结束" << std::endl;
    // 退出时，第二个 defer 先执行，第一个后执行
}

// 测试4: 实际场景模拟 - 文件操作（伪代码示意）
void test_file_operation() {
    // 模拟打开文件
    std::cout << "打开文件" << std::endl;
    LOG_DEFER {
        std::cout << "defer: 关闭文件" << std::endl; // 确保文件被关闭
    };
    std::cout << "处理文件数据..." << std::endl;
    // 即使此处发生异常或返回，defer 也会确保资源释放
}

int main() {
    std::cout << "=== 基本功能测试 ===" << std::endl;
    test_basic();

    std::cout << "\n=== 提前返回测试 ===" << std::endl;
    test_early_return();

    std::cout << "\n=== 多 defer 顺序测试 ===" << std::endl;
    test_multiple_defer();

    std::cout << "\n=== 实际场景测试 ===" << std::endl;
    test_file_operation();

    return 0;
}