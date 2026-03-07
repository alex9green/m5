@echo off
echo ==========================================
echo  COMPILARE M5Stack K141 - U088 SGP30
echo ==========================================
echo.

cd /d "K:\PlatformIO\Projects\U088_SGP30_Sensor"

echo [1/3] Verificare PlatformIO...
pio --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: PlatformIO nu este instalat!
    echo Instaleaza extensia in VS Code mai intai.
    pause
    exit /b 1
)

echo [2/3] Compilare...
pio run

if errorlevel 1 (
    echo.
    echo COMPILARE ESUATA!
    pause
    exit /b 1
)

echo.
echo [3/3] COMPILARE REUSITA!
echo.

set /p upload="Doresti sa incarci pe placa? (d/n): "
if /i "%upload%"=="d" (
    echo Incarcare...
    pio run --target upload
)

pause
