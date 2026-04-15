@echo off
tools\avrdude\avrdude.exe -C tools\avrdude\avrdude.conf -c usbasp -p at90usb1286 -U flash:w:%1:i
if errorlevel 1 exit /b 1
