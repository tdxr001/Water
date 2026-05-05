@echo off
setlocal
cd /d "%~dp0"

echo Testing MQTT broker at 172.20.10.6:1883...
"%~dp0mosquitto\mosquitto_pub.exe" -h 172.20.10.6 -p 1883 -t codex/connection/check -m ok

if errorlevel 1 (
  echo.
  echo Test failed. Start start-mosquitto-u575.bat first, then try again.
) else (
  echo.
  echo Publish test succeeded.
)

pause
