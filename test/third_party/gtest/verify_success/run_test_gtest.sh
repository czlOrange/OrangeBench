#!/bin/bash
# 直接写死所有绝对路径（100%避免路径拼接错误）
# 1. 项目根目录（必须替换成你实际的根路径）
PROJECT_ROOT="/CODE/OrangeLi_Http_Server"
# 2. 测试文件绝对路径
TEST_FILE="${PROJECT_ROOT}/test/third_party/gtest/test_gtest.cpp"
# 3. 项目库文件绝对路径
PROJECT_LIB="${PROJECT_ROOT}/build/lib/libhttpserver_net.a"
# 4. gtest库文件绝对路径
GTEST_LIB="${PROJECT_ROOT}/third_party/gtest/lib/libgtest.a"
GTEST_MAIN_LIB="${PROJECT_ROOT}/third_party/gtest/lib/libgtest_main.a"
# 5. 输出目录
OUTPUT_DIR="${PROJECT_ROOT}/build/bin"
mkdir -p "${OUTPUT_DIR}"

# 编译（直接指定所有文件的绝对路径）
echo "正在编译test_gtest..."
g++ -std=c++17 \
"${TEST_FILE}" \
-I"${PROJECT_ROOT}/include" \
-I"${PROJECT_ROOT}/third_party/gtest/include" \
"${PROJECT_LIB}" \
"${GTEST_LIB}" \
"${GTEST_MAIN_LIB}" \
-lpthread \
-o "${OUTPUT_DIR}/test_gtest"

# 运行
if [ $? -eq 0 ]; then
    echo "编译成功，正在运行测试..."
    "${OUTPUT_DIR}/test_gtest"
else
    echo "编译失败！请检查以下路径是否存在："
    echo "1. 测试文件：${TEST_FILE}"
    echo "2. 项目库：${PROJECT_LIB}"
    echo "3. gtest库：${GTEST_LIB}、${GTEST_MAIN_LIB}"
fi