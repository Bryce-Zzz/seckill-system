-- seckill_post.lua (彻底纠错版)
wrk.method = "POST"
wrk.headers["Content-Type"] = "application/x-www-form-urlencoded"

-- 种子加上微秒级偏移，彻底打散 16 个线程的随机空间
math.randomseed(os.time() + os.clock() * 1000)

request = function()
   -- 生成一个 1 到 1 亿之间的随机数，碰撞概率降至忽略不计
   local user_id = math.random(1, 100000000)
   local body = "user_id=" .. user_id .. "&product_id=APPLE_01"
   return wrk.format(nil, nil, nil, body)
end
