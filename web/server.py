#!/usr/bin/env python3
# ============================================================
#  本地小服务器：① 提供 dashboard.html  ② 代理 /api/* 到 OneNET
#
#  ★ 为什么要代理？
#      OneNET 的 API 不返回跨域(CORS)响应头 ✗
#      → 浏览器【不允许】网页直接读它的数据 ✗✗
#      → 所以让【服务器】去请求 ✓（服务器不受同源策略限制 ✓）
#      → 网页只请求自己的 /api/... ✓ 同源 ✓ 浏览器就放行了 ✓✓
#
#  用法：python server.py    然后浏览器打开 http://localhost:8080/dashboard.html
#  不依赖任何第三方库 ✓ 只用 Python 自带的 ✓
# ============================================================
import http.server, socketserver, urllib.request, urllib.error, os, sys

UPSTREAM = 'https://iot-api.heclouds.com'   # OneNET 接口域名
PORT     = 8080

class Handler(http.server.SimpleHTTPRequestHandler):

    # ---- 代理：把 /api/xxx 转发到 OneNET 的 /xxx ----
    def proxy(self):
        target = UPSTREAM + self.path[len('/api'):]
        n = int(self.headers.get('Content-Length') or 0)
        body = self.rfile.read(n) if n else None

        req = urllib.request.Request(target, data=body, method=self.command)
        req.add_header('Content-Type', self.headers.get('Content-Type', 'application/json'))
        auth = self.headers.get('Authorization')
        if auth:
            req.add_header('Authorization', auth)

        try:
            with urllib.request.urlopen(req, timeout=12) as r:
                data = r.read()
                code = r.status
        except urllib.error.HTTPError as e:
            data, code = e.read(), e.code
        except Exception as e:
            data = ('{"code":-1,"msg":"proxy error: %s"}' % e).encode()
            code = 502

        self.send_response(code)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):                         # ★★★ 之前漏了这个 ✗✗
        if self.path.startswith('/api/'):     #    OneNET 查数据是【GET】✗ 不是 POST ✗
            return self.proxy()               #    → 没有它，GET 会被当成"找文件" → 404 ✗
        return super().do_GET()

    def do_POST(self):
        if self.path.startswith('/api/'):
            return self.proxy()
        self.send_error(404)

    def do_OPTIONS(self):                     # 万一浏览器还要预检 ✓ 直接放行 ✓
        self.send_response(204)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Headers', '*')
        self.send_header('Access-Control-Allow-Methods', 'POST,GET,OPTIONS')
        self.end_headers()

    def end_headers(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        # ★★★ 禁止浏览器缓存页面 ✗✗
        #   起因：浏览器缓存了旧版 dashboard.html → 里面的 API 还是直连 OneNET ✗
        #        → 请求发不出去 → 页面永远"连接中"✗
        #   → 让服务端明确说"别存" ✓ 以后改网页【永远立刻生效】✓✓
        self.send_header('Cache-Control', 'no-store, no-cache, must-revalidate, max-age=0')
        self.send_header('Pragma', 'no-cache')
        self.send_header('Expires', '0')
        super().end_headers()

    def log_message(self, fmt, *args):
        pass                                   # 安静点 ✓

if __name__ == '__main__':
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    socketserver.ThreadingTCPServer.allow_reuse_address = True
    with socketserver.ThreadingTCPServer(('0.0.0.0', PORT), Handler) as httpd:
        print('serving  http://localhost:%d/dashboard.html' % PORT)
        httpd.serve_forever()