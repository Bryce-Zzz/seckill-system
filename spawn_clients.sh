#!/bin/bash
echo "🌊 正在召唤 1000 个潜伏的 SSE 客户端..."

# 循环 1000 次，使用 curl 发起长连接请求并放入后台执行
for i in {1..1000}; do
    # -N 保持长连接不缓冲，-s 静默模式，丢弃输出
    curl -N -s "http://localhost:9001/sse?user_id=bot_$i" > /dev/null &
done

echo "✅ 1000 个连接已建立！"
echo "👀 请立刻去终端输入 top -p \$(pidof seckill-server) 观察 CPU"
echo "🛑 测试结束后，请在这个窗口按回车键，我会杀掉所有 bot"
read
echo "🧹 正在清理战场..."
killall curl
echo "✅ 清理完毕！"
