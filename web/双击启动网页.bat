@echo off
chcp 65001 >nul
title ESP32-S3 环境监测
cd /d "%~dp0"

echo 正在启动本地服务...
start "adc-web" /min "D:\program\python\python3.13.12\python.exe" server.py
timeout /t 2 >nul

echo 正在打开网页...
start "" http://127.0.0.1:8080/

echo.
echo  服务已启动 ✓ 关掉这个黑窗口 = 关掉服务 ✗
echo  网页地址: http://127.0.0.1:8080/
timeout /t 3 >nul
exit