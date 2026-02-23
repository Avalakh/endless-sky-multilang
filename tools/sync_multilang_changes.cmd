@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "PYTHONIOENCODING=utf-8"

where py >nul 2>&1
if %ERRORLEVEL%==0 (
    py "%SCRIPT_DIR%sync_multilang_changes.py" %*
) else (
    python "%SCRIPT_DIR%sync_multilang_changes.py" %*
)
set "EXIT_CODE=%ERRORLEVEL%"

if %EXIT_CODE%==0 (
    echo.
    echo Multilang sync completed successfully.
) else (
    echo.
    echo Multilang sync finished with exit code %EXIT_CODE%.
)

exit /b %EXIT_CODE%
