# Compilare È™i upload automatÄƒ M5Stack K141
param(
    [switch]$Upload,
    [switch]$Monitor,
    [switch]$Clean
)

$projectPath = "K:\PlatformIO\Projects\U088_SGP30_Sensor"
Set-Location $projectPath

Write-Host "=== M5Stack K141 U088 Controller ===" -ForegroundColor Cyan

if ($Clean) {
    Write-Host "Curatare cache..." -ForegroundColor Yellow
    pio run --target clean
}

Write-Host "Compilare..." -ForegroundColor Green
pio run

if ($LASTEXITCODE -eq 0) {
    Write-Host "COMPILARE REUSITA!" -ForegroundColor Green
    
    if ($Upload) {
        Write-Host "Incarcare pe placa..." -ForegroundColor Yellow
        pio run --target upload
        
        if ($Monitor -and $LASTEXITCODE -eq 0) {
            Write-Host "Pornire monitor serial..." -ForegroundColor Cyan
            pio device monitor
        }
    }
} else {
    Write-Host "EROARE COMPILARE!" -ForegroundColor Red
}
