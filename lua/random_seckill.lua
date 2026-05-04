wrk.method = "POST"
wrk.headers["Content-Type"] = "application/x-www-form-urlencoded"

-- 利用 CPU 运行时钟的微秒级差异，为这 8 个线程生成绝对不一样的独一无二的前缀
local thread_prefix = tostring(os.clock() * 1000000)
local counter = 0

request = function()
    counter = counter + 1
    -- 组装成：user_微秒前缀_自增序号。保证全局唯一，绝对不撞车！
    local user_id = "user_" .. thread_prefix .. "_" .. counter
    local body = "product_id=T001" .. "&user_id=" .. user_id .. "&quantity=1"
    return wrk.format(nil, nil, nil, body)
end
