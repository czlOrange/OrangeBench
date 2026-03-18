#include <gtest/gtest.h>
#include "calculator.h"

// 没使用夹具-基本测试用例
TEST(CalculatorTest, AddPositiveNumbers) {
    Calculator calc;
    EXPECT_EQ(calc.Add(2, 3), 5);
    EXPECT_EQ(calc.Add(0, 0), 0);
    EXPECT_EQ(calc.Add(-1, 1), 0);
}

TEST(CalculatorTest, SubtractNumbers) {
    Calculator calc;
    EXPECT_EQ(calc.Subtract(5, 3), 2);
    EXPECT_EQ(calc.Subtract(0, 0), 0);
}

// 测试夹具示例(共享逻辑而不是共享实例)
// class 自定义类名 : 全局命名空间 :: 子命名空间 :: 类名 {}
class CalculatorTestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        // 每个测试前的设置
        calc = new Calculator();
    }    
    
    void TearDown() override {
        // 每个测试后的清理
        delete calc;
    }    
    Calculator* calc;
};

// 使用夹具的测试
TEST_F(CalculatorTestFixture, MultiplyTest) {
    EXPECT_EQ(calc->Multiply(3, 4), 12);
    EXPECT_EQ(calc->Multiply(0, 100), 0);
}

// 异常测试
TEST(CalculatorTest, DivisionByZero) {
    Calculator calc;
    EXPECT_THROW(calc.Divide(5, 0), std::invalid_argument);
}

// 浮点数近似比较
TEST(CalculatorTest, DivisionPrecision) {
    Calculator calc;
    EXPECT_NEAR(calc.Divide(1, 3), 0.33333, 0.00001);
}

int main(int argc, char **argv) {           
    ::testing::InitGoogleTest(&argc, argv); //初始
    return RUN_ALL_TESTS();                 //执行
}