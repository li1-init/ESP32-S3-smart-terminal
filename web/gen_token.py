#!/usr/bin/env python3
# ============================================================
#  OneNET API Token 生成器（本地运行 ✓ 密钥不外传 ✓）
#
#  用法：
#     python gen_token.py
#     然后按提示粘贴【access_key】和【产品ID】
#
#  ★ 注意：这里要用【账号/产品级】的 access_key ✗ 不是设备密钥 ✗
#     在 OneNET 控制台找：
#       · 右上角头像 → 访问密钥 / 密钥管理        （账号级 ✓ 推荐）
#       · 或 产品 → 概览 → 访问密钥               （产品级 ✓）
#     ⚠️ 设备详情页里那个密钥只能给 MQTT 用 ✗ 查询 API 用不了 ✗
#
#  生成后把这一整串粘到网页的「鉴权 Token」里 ✓
# ============================================================
import hmac, hashlib, base64, urllib.parse

def make_token(access_key: str, res: str, et: int = 1924833600) -> str:
    """OneNET token 算法：sign = base64( HMAC-SHA256( base64解码的key, 待签串 ) )"""
    string_for_sig = f"{et}\nsha256\n{res}\n2018-10-31"
    sign = base64.b64encode(
        hmac.new(base64.b64decode(access_key),
                 string_for_sig.encode('utf-8'),
                 hashlib.sha256).digest()).decode()
    return (f"version=2018-10-31&res={urllib.parse.quote(res, safe='')}"
            f"&et={et}&method=sha256&sign={urllib.parse.quote(sign, safe='')}")

if __name__ == '__main__':
    print("=" * 62)
    print(" OneNET API Token 生成器")
    print("=" * 62)
    key = input("① 粘贴 access_key（账号/产品级，不是设备密钥）: ").strip()
    pid = input("② 产品 ID（例如 xxxxxxxxxx）: ").strip()

    if not key or not pid:
        print("✗ 两项都不能为空")
        raise SystemExit(1)

    print()
    print("======= 把下面【整行】复制到网页的「鉴权 Token」里 =======")
    print(make_token(key, "products/" + pid))
    print("=" * 62)
    print("有效期：2030-12-30 ✓")