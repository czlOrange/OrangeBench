#!/bin/bash
# 保存为 test_post.sh

echo "========== 测试 GET 路由 =========="
curl -s http://localhost:8080/
echo ""

curl -s http://localhost:8080/hello
echo ""

curl -s http://localhost:8080/json
echo ""

echo ""
echo "========== 测试 POST 表单 =========="
curl -s -X POST -d "name=张三&age=25&email=test@example.com" http://localhost:8080/api/users
echo ""

echo ""
echo "========== 测试 POST 回声 =========="
curl -s -X POST -d "Hello World" http://localhost:8080/echo
echo ""

echo ""
echo "========== 测试 POST JSON =========="
curl -s -X POST -d '{"key":"value"}' http://localhost:8080/api/data
echo ""

echo ""
echo "========== 测试 404 =========="
curl -s -X POST http://localhost:8080/notfound
echo ""

echo ""
echo "✅ 测试完成！"