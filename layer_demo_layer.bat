@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%.") do set "ROOT_DIR=%%~fI\"

set "MANIFEST=%ROOT_DIR%openxr_api_layer_demo.json"
set "LAYER_DLL=%ROOT_DIR%obj\LayerDemo\openxr_api_layer_demo.dll"
set "REG_KEY=HKCU\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Explicit"
set "LAYER_NAME=XR_APILAYER_OSSO_layer_demo"

if "%~1"=="" goto :usage
if /I "%~1"=="install" goto :install
if /I "%~1"=="remove" goto :remove
if /I "%~1"=="status" goto :status
goto :usage

:install
if not exist "%MANIFEST%" (
  echo [ERROR] Manifest not found:
  echo         %MANIFEST%
  exit /b 1
)

if not exist "%LAYER_DLL%" (
  echo [WARN ] Layer DLL not found:
  echo         %LAYER_DLL%
  echo [WARN ] Build it first with: make layer_demo
)

reg add "%REG_KEY%" /v "%MANIFEST%" /t REG_DWORD /d 0 /f >nul
if errorlevel 1 (
  echo [ERROR] Failed to register layer manifest.
  exit /b 1
)

echo [ OK  ] Layer manifest registered.
echo [INFO ] Registry key: %REG_KEY%
echo [INFO ] Value name  : %MANIFEST%
echo.
echo [INFO ] To force-enable this explicit layer for current shell:
echo         set XR_ENABLE_API_LAYERS=%LAYER_NAME%
exit /b 0

:remove
reg delete "%REG_KEY%" /v "%MANIFEST%" /f >nul 2>nul
if errorlevel 1 (
  echo [INFO ] Layer manifest was not registered.
  exit /b 0
)

echo [ OK  ] Layer manifest removed.
exit /b 0

:status
reg query "%REG_KEY%" /v "%MANIFEST%" >nul 2>nul
if errorlevel 1 (
  echo [INFO ] Layer manifest is NOT registered.
  exit /b 0
)

echo [INFO ] Layer manifest is registered.
echo [INFO ] Value name: %MANIFEST%
exit /b 0

:usage
echo Usage: %~nx0 ^<install^|remove^|status^>
exit /b 1
