import redis
import sys

# 连接本地 Redis
try:
    r = redis.Redis(host='127.0.0.1', port=6379, decode_responses=True)
    r.ping()
except Exception as e:
    print(f"Redis 连接失败: {e}")
    sys.exit(1)

stream_key = 'seckill:stream:orders'
order_map = {}
last_id = '-'
total_scanned = 0

print("🔍 探针启动：开始逐条扫描 Redis Stream...")

while True:
    # 每次拉取 10000 条，避免撑爆内存
    # 注意：为了防止死循环，下次拉取的 min ID 是上一次的最后一个 ID 加上 '(' 表示开区间
    messages = r.xrange(stream_key, min=last_id, max='+', count=10000)
    if not messages:
        break
    
    for msg_id, msg_data in messages:
        total_scanned += 1
        # 解析你的 payload: "ORD1777126524418650193|100000001|APPLE_01|1|..."
        raw_data = msg_data.get('order_data', '')
        parts = raw_data.split('|')
        
        if len(parts) >= 2:
            order_id = parts[0]
            user_id = parts[1]
            
            # 核心查重逻辑
            if order_id in order_map:
                order_map[order_id].append(user_id)
            else:
                order_map[order_id] = [user_id]
                
        # 更新 last_id，带上圆括号表示不包含当前 ID
        last_id = f"({msg_id}"
        
    print(f"已扫描 {total_scanned} 条...", end='\r')

print("\n\n🎯 扫描完成！开始核对碰撞记录...")

collision_count = 0
# 遍历寻找被两个以上 user_id 共享的 order_id
for order_id, users in order_map.items():
    if len(users) > 1:
        collision_count += 1
        print(f"🚨 抓获物理碰撞 [{collision_count}]:")
        print(f"   => 案发订单号: {order_id}")
        print(f"   => 受害用户群: {users}")
        print("-" * 40)

print(f"✅ 结案总结：在 {total_scanned} 条数据中，共发现 {collision_count} 次 order_id 碰撞。")

# 验证我们推导的那个 862104 的数字
unique_order_ids = len(order_map)
print(f"✅ 理论 MySQL 入库数 (去重后的 order_id 数量): {unique_order_ids}")
