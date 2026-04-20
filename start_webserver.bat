@echo off
cd /d "%~dp0webapp"
start "" /min cmd /c "node server.js >> server.log 2>&1"
echo Webserver started in background. Logs written to webapp\server.log
