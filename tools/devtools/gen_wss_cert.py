# -*- coding: utf-8 -*-
"""重新生成 WSS 测试自签名证书（RSA 2048，SAN 含 localhost）。

用途：src/app/certs/ 下的 deviceforge-wss.pem / deviceforge-wss-key.pem
随 QRC 打包供 WSS 模式使用（Qt 6.11 无运行时生成 API，见
src/tools/WebSocketTool/WebSocketBackend.cpp 注释）。证书过期（10 年）
或需要更换密钥时运行本脚本重新生成。

依赖：openssl 可执行文件（Git for Windows 自带：C:/Program Files/Git/mingw64/bin/openssl.exe）
"""
import argparse
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
CERT_DIR = REPO_ROOT / "src" / "app" / "certs"
DEFAULT_OPENSSL = r"C:\Program Files\Git\mingw64\bin\openssl.exe"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="重新生成 WSS 测试自签名证书")
    ap.add_argument("--openssl", default=DEFAULT_OPENSSL, help="openssl 可执行文件路径")
    ap.add_argument("--days", type=int, default=3650, help="证书有效期天数（默认 3650）")
    args = ap.parse_args(argv)

    if not Path(args.openssl).is_file():
        print(f"[FAIL] openssl 不存在: {args.openssl}（可用 --openssl 指定）")
        return 1

    CERT_DIR.mkdir(parents=True, exist_ok=True)
    cert_path = CERT_DIR / "deviceforge-wss.pem"
    key_path = CERT_DIR / "deviceforge-wss-key.pem"

    cmd = [
        args.openssl, "req", "-x509", "-newkey", "rsa:2048",
        "-keyout", str(key_path), "-out", str(cert_path),
        "-days", str(args.days), "-nodes",
        "-subj", "/CN=DeviceForge WSS Test",
        "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
    ]
    print("生成中: " + " ".join(cmd))
    proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        print(f"[FAIL] openssl 失败:\n{proc.stderr}")
        return 1

    # 简单校验：证书与私钥均生成且证书可解析
    if not (cert_path.is_file() and key_path.is_file()):
        print("[FAIL] 证书/私钥文件未生成")
        return 1
    verify = subprocess.run(
        [args.openssl, "x509", "-in", str(cert_path), "-noout", "-subject"],
        capture_output=True, text=True, encoding="utf-8", errors="replace")
    print(f"[OK] 已生成:\n  {cert_path}\n  {key_path}")
    if verify.returncode == 0:
        print(f"  证书: {verify.stdout.strip()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
