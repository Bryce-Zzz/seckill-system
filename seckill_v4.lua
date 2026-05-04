-- seckill_v4.lua
wrk.method = "POST"
-- 关键修改 1：改回表单类型
wrk.headers["Content-Type"] = "application/x-www-form-urlencoded"
wrk.headers["Connection"] = "Keep-Alive"

local global_thread_counter = 0

function setup(thread)
    thread:set("thread_id", global_thread_counter)
    global_thread_counter = global_thread_counter + 1
end

function init(args)
    -- 每个线程 1 亿号段，绝对防撞
    user_id_counter = thread_id * 100000000
end

function request()
    user_id_counter = user_id_counter + 1
    
    -- 关键修改 2：改回表单拼接格式
    local body = string.format("user_id=%d&product_id=APPLE_01", user_id_counter)
    
    return wrk.format(nil, nil, nil, body)
end
