#!/usr/bin/env python3
"""Проверка Kotlin-клиента (android/app/.../WatchClient.kt) на JVM против ядра часов.
Нужны: собранный tests/sync_server (см. docs/sync-protocol.md), JDK 17+ и kotlinc.
  tests/run_kotlin_selftest.py <sync_server> <kotlinc> [рабочая папка]"""
import os, subprocess, sys, tempfile
server, kotlinc = os.path.abspath(sys.argv[1]), sys.argv[2]
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
work = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else tempfile.mkdtemp()
os.makedirs(work, exist_ok=True)
jar = os.path.join(work, "selftest.jar")
subprocess.run([kotlinc, os.path.join(root, "android/app/src/main/java/com/watchos/sync/WatchClient.kt"),
                os.path.join(root, "android/jvmtest/ProtocolSelfTest.kt"), "-include-runtime", "-d", jar], check=True,
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
srv = subprocess.Popen([server, "18766", "2"], cwd=work, stdout=subprocess.PIPE, text=True)
code = ""
for line in srv.stdout:                       # перед CODE сервер печатает отладочные строки LTRACE
    if line.startswith("CODE "):
        code = line.split()[1]
        break
r = subprocess.run(["java", "-jar", jar, "127.0.0.1", "18766", code], capture_output=True, text=True, timeout=120)
print(r.stdout, r.stderr)
out = srv.communicate(timeout=60)[0]
print("server:", out.strip().replace("\n", " | "))
ok = r.returncode == 0 and "PHOTOS 2" in out and "a1b2c3d4" in out and "TIME 2023-11-15" in out and "restarts=1" in out and os.path.getsize(os.path.join(work, "fw_received.bin")) == 150000
print("RESULT:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
