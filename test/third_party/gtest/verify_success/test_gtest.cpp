#include <gtest/gtest.h>

//HelloTest测试组下,BasicAssertion测试用例{测试断语1;  测试断语2; ...}
TEST(HelloTest, BasicAssertion) {
    EXPECT_TRUE(true);
    EXPECT_EQ(1+1, 2);
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}验证成功性Verify 正确性Correctness 成功吗