@echo off
set IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.3.5
set IDF_PYTHON_ENV_PATH=C:\Espressif\python_env\idf5.3_py3.11_env
set PATH=C:\Espressif\python_env\idf5.3_py3.11_env\Scripts;C:\Espressif\tools\xtensa-esp-elf\esp-13.2.0_20250707\xtensa-esp-elf\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\tools\cmake\3.30.2\bin;C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20240318\openocd-esp32\bin;%PATH%
cd /d C:\Users\erdem\ESP32\New-Project
echo === FULLCLEAN ===
python %IDF_PATH%\tools\idf.py fullclean
echo === RECONFIGURE ===
python %IDF_PATH%\tools\idf.py reconfigure
echo === BUILD ===
python %IDF_PATH%\tools\idf.py build
echo BUILD_EXIT=%ERRORLEVEL%
echo === FLASH ===
python %IDF_PATH%\tools\idf.py -p COM4 flash
echo FLASH_EXIT=%ERRORLEVEL%
