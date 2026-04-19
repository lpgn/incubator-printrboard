@echo off
setlocal

echo [FLASH] FAST MODE — no EEPROM backup, just flash and go.
echo [FLASH] Erasing and flashing firmware...
tools\avrdude\avrdude.exe -C tools\avrdude\avrdude.conf -c usbasp -B 1 -p at90usb1286 -e -U flash:w:%1:i
if errorlevel 1 exit /b 1

echo [FLASH] Done.
