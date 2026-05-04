-- 秒杀 Lua 原子脚本
-- KEYS[1]: stock_key (库存 key)
-- KEYS[2]: order_set_key (已购买用户集合)
-- ARGV[1]: user_id (用户ID)
-- ARGV[2]: quantity (购买数量)
-- ARGV[3]: max_per_user (每人最大购买数)

local stock_key = KEYS[1]
local order_set_key = KEYS[2]
local user_id = ARGV[1]
local quantity = tonumber(ARGV[2])
local max_per_user = tonumber(ARGV[3])

-- 检查用户是否已购买
local bought = redis.call('SISMEMBER', order_set_key, user_id)
if bought == 1 then
    return -2  -- 用户已购买
end

-- 检查库存
local stock = tonumber(redis.call('GET', stock_key) or 0)
if stock < quantity then
    return -1  -- 库存不足
end

-- 原子扣减库存
redis.call('DECRBY', stock_key, quantity)

-- 记录用户已购买
redis.call('SADD', order_set_key, user_id)

-- 返回成功（返回扣减后的库存，方便后续追踪）
return stock - quantity
