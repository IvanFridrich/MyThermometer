# scripts/coverage.ps1
# Builds native tests with clang++ + LLVM coverage and opens the HTML report.
# Run from the project root: .\scripts\coverage.ps1
#
# Requires: LLVM 21+ installed (clang++, llvm-profdata, llvm-cov on PATH or in C:\Program Files\LLVM\bin)
# Requires: doctest already downloaded (run `pio test -e native` once to populate .pio\libdeps\native)
#
# Each test/test_*/ directory defines its own DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
# and is therefore compiled into a SEPARATE runner (one main per binary). Every
# runner links all src/core + HAL fakes, runs with its own .profraw, and the raw
# profiles are merged before reporting -- the same model CI uses.

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
        Write-Error "Not found: $tool. Install LLVM or pass -LlvmBin PATH"
    }
}

if (-not (Test-Path $doctest)) {
    Write-Error "doctest not found at $doctest. Run 'pio test -e native' once first to download it"
}

Set-Location $root

# Sources shared by every test runner: domain logic + HAL fakes (native seam).
$coreSrcs = @(Get-ChildItem src\core -Recurse -Filter *.cpp -ErrorAction SilentlyContinue |
              Select-Object -ExpandProperty FullName)
$fakeSrcs = @(Get-ChildItem src\hal -Filter *_fake.cpp -ErrorAction SilentlyContinue |
              Select-Object -ExpandProperty FullName)

if ($coreSrcs.Count -eq 0) {
    Write-Error "No .cpp files found in src/core"
}

# Every test directory that contains at least one .cpp becomes its own runner.
$testDirs = @(Get-ChildItem test -Directory -ErrorAction SilentlyContinue | Where-Object {
    @(Get-ChildItem $_.FullName -Recurse -Filter *.cpp -ErrorAction SilentlyContinue).Count -gt 0
})

if ($testDirs.Count -eq 0) {
    Write-Error "No test/test_*/ directories with .cpp files found"
}

$profraws = @()
$runners  = @()

foreach ($dir in $testDirs) {
    $name     = $dir.Name
    $testSrcs = @(Get-ChildItem $dir.FullName -Recurse -Filter *.cpp |
                  Select-Object -ExpandProperty FullName)
    $runner   = Join-Path $root "test_runner_$name.exe"
    $profraw  = Join-Path $root "$name.profraw"

    Write-Host "=== Building $name ===" -ForegroundColor Cyan
    & $clang `
        -std=c++17 `
        -fno-exceptions -fno-rtti `
        -DNATIVE_BUILD -D_CRT_SECURE_NO_WARNINGS `
        -fprofile-instr-generate -fcoverage-mapping `
        -Wall -Wextra `
        -I src -I include -I $dir.FullName `
        "-I$doctest" `
        @coreSrcs @fakeSrcs @testSrcs `
        -o $runner
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "=== Running $name ===" -ForegroundColor Cyan
    $env:LLVM_PROFILE_FILE = $profraw
    & $runner
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $profraws += $profraw
    $runners  += $runner
}

Write-Host "`n=== Merging coverage profiles ===" -ForegroundColor Cyan
& $profdata merge -sparse @profraws -o coverage.profdata
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Report against one runner that contains the full core mapping (every runner
# compiles all of src/core) with the merged profile, which sums execution counts
# across all suites.  Same model as CI.  Prefer the link suite.
$reportRunner = Join-Path $root "test_runner_test_phase1_link.exe"
if (-not (Test-Path $reportRunner)) { $reportRunner = $runners[0] }

# llvm-cov prints benign notes to stderr (template instantiations present only in
# other binaries); under $ErrorActionPreference='Stop' PowerShell would treat
# those as fatal. The exit code is the real signal, so relax it for these calls.
$ErrorActionPreference = "Continue"

Write-Host "`n=== Coverage summary (src/core) ===" -ForegroundColor Green
& $cov report "$reportRunner" "-instr-profile=$root\coverage.profdata" @coreSrcs

New-Item -ItemType Directory -Force coverage | Out-Null
& $cov show "$reportRunner" `
    "-instr-profile=$root\coverage.profdata" `
    -format=html -output-dir=coverage `
    -show-line-counts-or-regions `
    @coreSrcs

Write-Host "`n=== Opening coverage\index.html ===" -ForegroundColor Green
Start-Process (Join-Path $root "coverage\index.html")
