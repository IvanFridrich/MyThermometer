# scripts/coverage.ps1
# Builds native tests with clang++ + LLVM coverage and opens the HTML report.
# Run from the project root: .\scripts\coverage.ps1
#
# Requires: LLVM 21+ installed (clang++, llvm-profdata, llvm-cov on PATH or in C:\Program Files\LLVM\bin)
# Requires: doctest already downloaded (run `pio test -e native` once to populate .pio\libdeps\native)

param(
    [string]$LlvmBin = "C:\Program Files\LLVM\bin"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root     = $PSScriptRoot | Split-Path
$clang    = Join-Path $LlvmBin "clang++.exe"
$profdata = Join-Path $LlvmBin "llvm-profdata.exe"
$cov      = Join-Path $LlvmBin "llvm-cov.exe"
$doctest  = Join-Path $root ".pio\libdeps\native\doctest"

foreach ($tool in $clang, $profdata, $cov) {
    if (-not (Test-Path $tool)) {
        Write-Error "Not found: $tool — install LLVM or pass -LlvmBin <path>"
    }
}

if (-not (Test-Path $doctest)) {
    Write-Error "doctest not found at $doctest — run 'pio test -e native' once first to download it"
}

Set-Location $root

# Collect source files: all src/core/**/*.cpp + all test/**/*.cpp
$srcs = @(Get-ChildItem src\core -Recurse -Filter *.cpp -ErrorAction SilentlyContinue |
          Select-Object -ExpandProperty FullName) +
        @(Get-ChildItem test -Recurse -Filter *.cpp -ErrorAction SilentlyContinue |
          Select-Object -ExpandProperty FullName)

if ($srcs.Count -eq 0) {
    Write-Error "No .cpp files found in src/core or test/"
}

Write-Host "=== Compiling $($srcs.Count) file(s) with clang++ + coverage ===" -ForegroundColor Cyan
& $clang `
    -std=c++17 `
    -fno-exceptions -fno-rtti `
    -fprofile-instr-generate -fcoverage-mapping `
    -Wall -Wextra `
    -I src -I include `
    -I (Join-Path $root "test\test_skeleton") `
    "-I$doctest" `
    @srcs `
    -o test_runner.exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "`n=== Running tests ===" -ForegroundColor Cyan
$env:LLVM_PROFILE_FILE = Join-Path $root "default.profraw"
.\test_runner.exe -v
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "`n=== Generating coverage report ===" -ForegroundColor Cyan
& $profdata merge -sparse default.profraw -o coverage.profdata

$coreSrcs = @(Get-ChildItem src\core -Recurse -Filter *.cpp -ErrorAction SilentlyContinue |
              Select-Object -ExpandProperty FullName)

& $cov report "$root\test_runner.exe" "-instr-profile=$root\coverage.profdata" @coreSrcs

New-Item -ItemType Directory -Force coverage | Out-Null
& $cov show "$root\test_runner.exe" `
    "-instr-profile=$root\coverage.profdata" `
    -format=html -output-dir=coverage `
    -show-line-counts-or-regions `
    @coreSrcs

Write-Host "`n=== Opening coverage\index.html ===" -ForegroundColor Green
Start-Process (Join-Path $root "coverage\index.html")
