@echo off
setlocal

set MSBUILD=
for %%P in (
  "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
  "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
  "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"
) do (
  if exist %%P set MSBUILD=%%P
)

if "%MSBUILD%"=="" (
  echo MSBuild bulunamadi. Visual Studio C++ yukleyin.
  exit /b 1
)

echo MSBuild: %MSBUILD%
"%MSBUILD%" "%~dp0LVGL.Simulator\LVGL.Simulator.vcxproj" /p:Configuration=Release /p:Platform=x64 /m
if errorlevel 1 exit /b 1

echo.
echo Derleme tamam. Calistirmak icin:
echo   Output\Binaries\Release\x64\LVGL.Simulator.exe
