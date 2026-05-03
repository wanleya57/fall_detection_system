#!/usr/bin/env python3
"""
camera_proxy.py - 将手机摄像头流代理到 localhost
解决 WSL2 无法直接访问局域网的问题

用法: python camera_proxy.py <手机IP:端口> [本地端口]
示例: python camera_proxy.py 192.168.31.252:8080 9090
"""
import sys
import socket
import threading
import time

def handle_client(client_sock, target_addr):
    """转发一个客户端连接到手机"""
    try:
        target_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        target_sock.settimeout(10)
        target_sock.connect(target_addr)
        target_sock.settimeout(None)

        # 转发请求
        data = client_sock.recv(4096)
        if data:
            target_sock.sendall(data)

        # 双向转发数据
        def forward(src, dst, name):
            try:
                while True:
                    data = src.recv(65536)
                    if not data:
                        break
                    dst.sendall(data)
            except (ConnectionError, OSError):
                pass
            finally:
                try: src.shutdown(socket.SHUT_RD)
                except: pass
                try: dst.shutdown(socket.SHUT_WR)
                except: pass

        t1 = threading.Thread(target=forward, args=(target_sock, client_sock, "phone->client"))
        t2 = threading.Thread(target=forward, args=(client_sock, target_sock, "client->phone"))
        t1.daemon = True
        t2.daemon = True
        t1.start()
        t2.start()
        t1.join()
        t2.join()

    except Exception as e:
        print(f"[proxy] Connection error: {e}")
    finally:
        try: client_sock.close()
        except: pass
        try: target_sock.close()
        except: pass

def main():
    if len(sys.argv) < 2:
        print("Usage: python camera_proxy.py <phone_ip:port> [local_port]")
        print("Example: python camera_proxy.py 192.168.31.252:8080 9090")
        sys.exit(1)

    target = sys.argv[1]
    local_port = int(sys.argv[2]) if len(sys.argv) > 2 else 9090

    if ':' in target:
        host, port = target.rsplit(':', 1)
        target_addr = (host, int(port))
    else:
        target_addr = (target, 8080)

    # 测试能否连接手机
    print(f"[proxy] Testing connection to {target_addr[0]}:{target_addr[1]}...")
    try:
        test = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        test.settimeout(3)
        test.connect(target_addr)
        test.close()
        print(f"[proxy] Phone reachable!")
    except Exception as e:
        print(f"[proxy] ERROR: Cannot reach phone at {target_addr}: {e}")
        sys.exit(1)

    # 启动代理服务器
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('0.0.0.0', local_port))
    server.listen(5)

    print(f"[proxy] Camera proxy started!")
    print(f"[proxy] Phone:  {target_addr[0]}:{target_addr[1]}")
    print(f"[proxy] Local:  http://localhost:{local_port}")
    print(f"[proxy] WSL:    http://host.docker.internal:{local_port} or http://172.29.48.1:{local_port}")
    print(f"[proxy] Use:    CAMERA_URL=http://localhost:{local_port}/video ./fall_detection_pc")
    print(f"[proxy] Press Ctrl+C to stop")
    print()

    try:
        while True:
            client, addr = server.accept()
            t = threading.Thread(target=handle_client, args=(client, target_addr))
            t.daemon = True
            t.start()
    except KeyboardInterrupt:
        print("\n[proxy] Stopped")
    finally:
        server.close()

if __name__ == '__main__':
    main()
