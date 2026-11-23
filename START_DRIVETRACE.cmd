@echo off
setlocal
cd /d "%~dp0"
echo.
echo  DRIVETRACE - PHASE 1 LOCAL WEB DASHBOARD
echo  All readings are simulated. No hardware is needed.
echo.
where py >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    py main.py
) else (
    where python >nul 2>nul
    if %ERRORLEVEL% EQU 0 (
        python main.py
    ) else (
        echo ERROR: Python 3.10 or newer is required.
        echo Visit https://www.python.org/downloads/
    )
)
echo.
echo DriveTrace is not running. Press any key to close.
pause >nul
