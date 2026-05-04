#!/usr/bin/env python3
"""
⚡ 闪电侠验证测试 v5 (修复 leftover 先于 select 处理)
"""
import socket
import urllib.request
import json
import random
import sys
import time
import threading
import select

API_BASE   = "http://localhost:9000"
SSE_HOST   = "127.0.0.1"
SSE_PORT   = 9001
TEST_USER  = f"FLASH_U{random.randint(1000,9999)}"
PRODUCT_ID = "P001"

result = {
    "sse_connected": False,
    "sse_received":  False,
    "db_found":      False,
    "cost_ms":       None,
    "order_id":      None,
    "error":         None,
}
connected_event = threading.Event()
stop_event      = threading.Event()

def http_get(url, timeout=5):
    req = urllib.request.Request(url)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode())

def http_post(url, timeout=5):
    req = urllib.request.Request(url, method="POST", data=b"")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode())

def parse_sse_lines(lines_buf, event_type_ref):
    """解析 SSE 行，返回剩余未完整行；events 通过 side-effect 处理"""
    event_type = event_type_ref[0]
    done = False

    while "\n" in lines_buf and not done:
        line, lines_buf = lines_buf.split("\n", 1)
        line = line.rstrip("\r")

        if line.startswith("event:"):
            event_type = line[6:].strip()
        elif line.startswith("data:"):
            data_str = line[5:].strip()
            try:
                data = json.loads(data_str)
            except Exception:
                data = {}

            if event_type == "connected":
                result["sse_connected"] = True
                print(f"✓ SSE 连接建立成功 (user={TEST_USER})")
                connected_event.set()

            elif event_type == "order_created":
                result["sse_received"] = True
                result["order_id"] = data.get("order_id")
                print(f"\n🎇 [SSE 瞬间到达] 收到 order_created！order_id={result['order_id']}")
                print("🕵️  正在光速验证数据库是否已落盘...")

                t0 = time.perf_counter()
                try:
                    db_data = http_get(
                        f"{API_BASE}/api/order/detail?order_id={result['order_id']}"
                    )
                except Exception as e:
                    db_data = {}
                    result["error"] = str(e)

                cost = (time.perf_counter() - t0) * 1000
                result["cost_ms"] = round(cost, 2)

                if db_data.get("code") == 200 and db_data.get("order"):
                    result["db_found"] = True
                    print(f"✅ [验证成功] 耗时 {result['cost_ms']}ms。订单确已落盘！")
                    print(f"   DB order: {json.dumps(db_data['order'], ensure_ascii=False)}")
                else:
                    result["error"] = f"DB returned: {db_data}"
                    print(f"❌ [致命异常] 耗时 {result['cost_ms']}ms。SSE说成功但DB查不到！")

                stop_event.set()
                done = True

        elif line == "":
            event_type = ""

    event_type_ref[0] = event_type
    return lines_buf, done

def listen_sse():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((SSE_HOST, SSE_PORT))

        # 发 HTTP GET
        request = (
            f"GET /sse?user_id={TEST_USER} HTTP/1.1\r\n"
            f"Host: {SSE_HOST}:{SSE_PORT}\r\n"
            f"Accept: text/event-stream\r\n"
            f"Connection: keep-alive\r\n\r\n"
        )
        s.sendall(request.encode())

        # 读数据直到拿到 HTTP header 结束符
        buf = b""
        while b"\r\n\r\n" not in buf:
            rdy = select.select([s], [], [], 5)
            if not rdy[0]:
                raise TimeoutError("Timeout waiting for HTTP headers")
            chunk = s.recv(4096)
            if not chunk:
                raise ConnectionError("Connection closed")
            buf += chunk

        # 分离 header 和 SSE body
        idx = buf.index(b"\r\n\r\n") + 4
        leftover = buf[idx:].decode("utf-8", errors="replace")

        event_type_ref = [""]

        # ★ 先解析 leftover（可能包含 connected 事件）
        leftover, done = parse_sse_lines(leftover, event_type_ref)
        if done:
            s.close()
            return

        # 再进入 select 循环等更多事件
        while not stop_event.is_set():
            rdy = select.select([s], [], [], 20)
            if not rdy[0]:
                continue
            chunk = s.recv(4096)
            if not chunk:
                break
            leftover += chunk.decode("utf-8", errors="replace")

            leftover, done = parse_sse_lines(leftover, event_type_ref)
            if done:
                break

        s.close()
    except Exception as e:
        result["error"] = str(e)
        stop_event.set()
        connected_event.set()

def main():
    print("=" * 60)
    print(f"⚡ 闪电侠验证测试 v5")
    print(f"   测试用户: {TEST_USER}")
    print(f"   商品ID:   {PRODUCT_ID}")
    print("=" * 60)

    try:
        health = http_get(f"{API_BASE}/api/health", timeout=3)
        print(f"✓ HTTP服务器在线 (mysql={health['components']['mysql']}, redis={health['components']['redis']})")
    except Exception as e:
        print(f"✗ HTTP服务器不可达: {e}")
        sys.exit(1)

    print(f"\n[1/3] 建立 SSE 连接 (localhost:{SSE_PORT})...")
    t = threading.Thread(target=listen_sse, daemon=True)
    t.start()

    if not connected_event.wait(timeout=8):
        print("✗ SSE 连接超时（8s）")
        print(f"   error: {result.get('error', 'N/A')}")
        sys.exit(1)

    if not result["sse_connected"]:
        print(f"✗ SSE 连接失败: {result.get('error')}")
        sys.exit(1)

    print(f"\n[2/3] 发起秒杀请求 user={TEST_USER}...")
    try:
        data = http_post(
            f"{API_BASE}/api/seckill/buy?user_id={TEST_USER}&product_id={PRODUCT_ID}&quantity=1"
        )
        print(f"   code={data.get('code')} msg={data.get('msg')} order_id={data.get('order_id','N/A')}")

        if data.get("code") != 200:
            print(f"\n⚠️  秒杀未成功（{data.get('msg')}），请确认秒杀已开启且库存充足")
            stop_event.set()
            sys.exit(0)
    except Exception as e:
        print(f"✗ 秒杀请求失败: {e}")
        stop_event.set()
        sys.exit(1)

    print(f"\n[3/3] 等待 SSE order_created 推送（最多15秒）...")
    stop_event.wait(timeout=15)

    print("\n" + "=" * 60)
    print("📊 最终结论")
    print("=" * 60)

    if result["sse_received"] and result["db_found"]:
        print(f"✅ PASS — 前端收到SSE推送时，数据库里一定有这条记录！")
        print(f"   order_id : {result['order_id']}")
        print(f"   反查耗时 : {result['cost_ms']}ms")
        sys.exit(0)
    elif result["sse_received"] and not result["db_found"]:
        print(f"❌ FAIL — SSE收到了但数据库没有！严重一致性问题！")
        print(f"   error: {result['error']}")
        sys.exit(2)
    else:
        print(f"❌ FAIL — 15秒内未收到 SSE order_created 事件")
        if result.get("error"):
            print(f"   error: {result['error']}")
        sys.exit(3)

if __name__ == "__main__":
    main()
