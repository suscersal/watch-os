#!/usr/bin/env python3
"""Проверка прошивальщика ESP32-S3 по USB (android/app/.../EspFlasher.kt) на JVM против поддельного ROM-загрузчика.
Нужны JDK 17+ и kotlinc:  tests/run_flash_selftest.py <kotlinc> [рабочая папка]"""
import os, subprocess, sys, tempfile
kotlinc = sys.argv[1]
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
work = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else tempfile.mkdtemp()
os.makedirs(work, exist_ok=True)
jar = os.path.join(work, "flash_selftest.jar")
subprocess.run([kotlinc, os.path.join(root, "android/app/src/main/java/com/watchos/sync/EspFlasher.kt"),
                os.path.join(root, "android/jvmtest/FlashSelfTest.kt"), "-include-runtime", "-d", jar], check=True,
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
r = subprocess.run(["java", "-jar", jar], capture_output=True, text=True, timeout=300)
print(r.stdout, r.stderr)
sys.exit(0 if r.returncode == 0 and "RESULT: PASS" in r.stdout else 1)
