@echo off
tools\dfu-programmer\dfu-programmer.exe at90usb1286 erase
if errorlevel 1 exit /b 1
tools\dfu-programmer\dfu-programmer.exe at90usb1286 flash %1
if errorlevel 1 exit /b 1
tools\dfu-programmer\dfu-programmer.exe at90usb1286 start
if errorlevel 1 exit /b 1
