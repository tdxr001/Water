@echo off
setlocal
cd /d "%~dp0"

echo Starting Mosquitto broker for U575 MQTT test...
echo Broker address: 172.20.10.6:1883
echo.
echo Keep this window open while testing.
echo Press Ctrl+C or close this window to stop the broker.
echo.

"%~dp0mosquitto\mosquitto.exe" -c "%~dp0mosquitto-local-ip1883.conf" -v

echo.
echo Mosquitto has stopped.
pause
