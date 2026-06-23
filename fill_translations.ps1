#Requires -Version 5.1
# fill_translations.ps1
# Fills translations for all .ts files in translations/ (except en/es).
# Calls the Python engine fill_translations.py, then verifies with lrelease.

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PyScript = Join-Path $ScriptDir "fill_translations.py"
$TranslationsDir = Join-Path $ScriptDir "translations"

Write-Host "===== ZipFX Translation Tool =====" -ForegroundColor Cyan
Write-Host ""

# Step 1: Run the Python translation engine
Write-Host "Step 1: Generating translated .ts files..." -ForegroundColor Yellow
$pyResult = python $PyScript 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Python script failed!" -ForegroundColor Red
    Write-Host $pyResult
    exit 1
}
Write-Host $pyResult

# Step 2: Find lrelease and compile .qm files
Write-Host ""
Write-Host "Step 2: Compiling .qm files with lrelease..." -ForegroundColor Yellow

$lrelease = Get-ChildItem -Path "C:\Qt" -Recurse -Filter "lrelease.exe" -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName

if (-not $lrelease) {
    $lrelease = Get-Command "lrelease" -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty Source
}

if (-not $lrelease) {
    Write-Host "WARNING: lrelease not found. Install Qt or add it to PATH to compile .qm files." -ForegroundColor Yellow
    Write-Host "The .ts files are ready for use. Skipping .qm compilation." -ForegroundColor Yellow
} else {
    Write-Host "Using lrelease: $lrelease" -ForegroundColor Gray

    $tsFiles = Get-ChildItem -Path $TranslationsDir -Filter "zipfx_*.ts" |
        Where-Object { $_.Name -notin @("zipfx_en.ts", "zipfx_es.ts") }

    $qmOk = 0
    $qmFail = 0
    foreach ($f in $tsFiles) {
        $qmFile = Join-Path $TranslationsDir ($f.BaseName + ".qm")
        Write-Host -NoNewline "  $($f.Name)... "
        $output = & $lrelease $f.FullName -qm $qmFile 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Host "OK" -ForegroundColor Green
            $qmOk++
        } else {
            Write-Host "FAILED" -ForegroundColor Red
            Write-Host "    $output" -ForegroundColor Red
            $qmFail++
        }
    }
    Write-Host ".qm compilation: $qmOk succeeded, $qmFail failed" -ForegroundColor $(if ($qmFail -eq 0) { "Green" } else { "Red" })
}

# Step 3: Summary
Write-Host ""
Write-Host "===== SUMMARY =====" -ForegroundColor Cyan

$tsFiles = Get-ChildItem -Path $TranslationsDir -Filter "zipfx_*.ts" |
    Where-Object { $_.Name -notin @("zipfx_en.ts", "zipfx_es.ts") }

$langNames = @{
    fr = "French"; de = "German"; it = "Italian"; pt = "Portuguese"
    nl = "Dutch"; sv = "Swedish"; no = "Norwegian"; da = "Danish"
    fi = "Finnish"; ru = "Russian"; ja = "Japanese"; zh = "Chinese"
    ko = "Korean"; ar = "Arabic"
}

foreach ($f in $tsFiles) {
    $code = $f.BaseName -replace 'zipfx_', ''
    $name = $langNames[$code]
    if (-not $name) { $name = $code }

    # Parse: count translated vs unfinished
    $content = Get-Content $f.FullName -Raw
    $sourceCount = [regex]::Matches($content, '<source>').Count
    $unfinishedCount = [regex]::Matches($content, 'type="unfinished"').Count
    $translatedCount = $sourceCount - $unfinishedCount

    Write-Host "  $($name.PadRight(12)) ($code): $translatedCount translated / $sourceCount total  -> $($f.Name)"
}

Write-Host ""
Write-Host "All done!" -ForegroundColor Green
