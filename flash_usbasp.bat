@echo off
setlocal
set EEPROM_BACKUP=%TEMP%\printrboard_eeprom_backup.hex

echo [FLASH] Reading EEPROM backup...
tools\avrdude\avrdude.exe -C tools\avrdude\avrdude.conf -c usbasp -B 10 -p at90usb1286 -U eeprom:r:"%EEPROM_BACKUP%":i
if errorlevel 1 (
    echo [FLASH] Warning: could not read EEPROM. Continuing anyway.
)

echo [FLASH] Erasing and flashing firmware (fast ISP)...
tools\avrdude\avrdude.exe -C tools\avrdude\avrdude.conf -c usbasp -B 1 -p at90usb1286 -e -U flash:w:%1:i
if errorlevel 1 exit /b 1

if exist "%EEPROM_BACKUP%" (
    echo [FLASH] Restoring EEPROM...
    tools\avrdude\avrdude.exe -C tools\avrdude\avrdude.conf -c usbasp -B 10 -p at90usb1286 -U eeprom:w:"%EEPROM_BACKUP%":i
    if errorlevel 1 (
        echo [FLASH] Warning: could not restore EEPROM.
    )
    del "%EEPROM_BACKUP%"
)

echo [FLASH] Done.
