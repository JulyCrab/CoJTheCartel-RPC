# Build script for CartelRichPresence
# Finds Visual Studio and compiles the project

param(
    [string]$Configuration = "Release"
)

Write-Host "=== CartelRichPresence Build Script ===" -ForegroundColor Cyan

# Find MSBuild
$msbuildPath = $null
$vsPaths = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
)

foreach ($path in $vsPaths) {
    if (Test-Path $path) {
        $msbuildPath = $path
        break
    }
}

if (-not $msbuildPath) {
    # Try vswhere
    $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -property installationPath
        if ($vsPath) {
            $msbuildPath = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
            if (-not (Test-Path $msbuildPath)) {
                $msbuildPath = $null
            }
        }
    }
}

if (-not $msbuildPath) {
    Write-Host "ERROR: MSBuild not found!" -ForegroundColor Red
    Write-Host "Install Visual Studio 2019 or 2022 with C++ Desktop Development" -ForegroundColor Yellow
    exit 1
}

Write-Host "Found MSBuild: $msbuildPath" -ForegroundColor Green
Write-Host "Building $Configuration|x86" -ForegroundColor Cyan

# Build
& $msbuildPath CartelRichPresence.sln `
    /p:Configuration=$Configuration `
    /p:Platform=x86 `
    /v:minimal `
    /m

if ($LASTEXITCODE -eq 0) {
    Write-Host "`nBuild succeeded!" -ForegroundColor Green
    
    $outputPath = "bin\$Configuration\CartelRichPresence.asi"
    if (Test-Path $outputPath) {
        $fileInfo = Get-Item $outputPath
        Write-Host "Output: $outputPath ($([math]::Round($fileInfo.Length / 1KB, 2)) KB)" -ForegroundColor Cyan
    }
} else {
    Write-Host "`nBuild failed!" -ForegroundColor Red
    exit 1
}
