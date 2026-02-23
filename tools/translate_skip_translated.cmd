@echo off
cd /d "%~dp0"
python tools\translate_data.py --skip-translated
pause
