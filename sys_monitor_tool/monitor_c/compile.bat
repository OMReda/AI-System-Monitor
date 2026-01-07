@echo off
gcc main.c monitor.c -o monitor.exe -lws2_32 -liphlpapi
if %errorlevel% neq 0 (
    echo Compilation failed!
    exit /b %errorlevel%
)
echo Compilation successful. Run monitor.exe to start the server.
