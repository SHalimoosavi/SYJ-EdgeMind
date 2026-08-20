# SYJ EdgeMind — Windows build script (authoritative implementation).
#
# scripts/build-windows.bat is a thin delegator to this script — it does
# not maintain a second, independent build implementation.
#
# CPU MODES (compile-time instruction-set selection, NOT runtime dispatch):
#
#   Portable (default) — GGML_NATIVE=OFF, GGML_AVX=OFF, GGML_AVX2=OFF.
#     No architecture-specific instructions beyond baseline x86-64. Safe on
#     any x86-64 Windows machine. Intended for unknown/unverified target
#     hardware, or when you don't know your deployment target's CPU.
#
#   -EnableAVX — GGML_AVX=ON, GGML_AVX2=OFF, GGML_NATIVE=OFF.
#     Requires an AVX-capable CPU. Explicit opt-in only — this script never
#     enables this automatically based on the machine building it.
#
#   -EnableAVX2 — GGML_AVX2=ON, GGML_AVX=ON. This script explicitly passes
#     BOTH flags on the command line — it does not assume or rely on
#     ggml's own CMake implicitly deriving GGML_AVX from GGML_AVX2 (that
#     internal dependency, if any, was not conclusively confirmed against
#     upstream source; see docs/supported-platforms.md for exactly what
#     was and wasn't verified). Passing both explicitly is correct and
#     deterministic regardless of ggml's internal option wiring — every
#     AVX2-capable CPU does support AVX as a hardware fact, so there is no
#     scenario where this combination is wrong, only one where it might be
#     technically redundant if ggml already infers it internally.
#     GGML_NATIVE=OFF. Requires an AVX2-capable CPU. Explicit opt-in only.
#
#   Passing both -EnableAVX and -EnableAVX2 is well-defined (not rejected as
#   ambiguous, unlike this project's --model/--model-id): AVX2 is a superset
#   of AVX, so the result is identical to -EnableAVX2 alone.
#
# IMPORTANT — read before choosing a mode:
# The currently pinned ggml/llama.cpp build performs COMPILE-TIME
# instruction-set selection, not runtime CPU-feature dispatch (verified
# against ggml's actual CMake configuration, not assumed). A binary built
# with -EnableAVX or -EnableAVX2 will attempt those instructions
# unconditionally on whatever CPU it runs on — there is no fallback, and no
# detection of whether the running CPU actually supports them. This is a
# dependency limitation, not something SYJ EdgeMind chooses to omit; see
# docs/supported-platforms.md for the full explanation. GGML_NATIVE is
# never enabled by this script for exactly the same reason (it would make
# the *build machine's* CPU the implicit target, which may differ from
# wherever the binary is actually run).
#
# This script never downloads anything itself — the only dependency fetch
# is llama.cpp's own pinned CMake FetchContent (see the top-level
# CMakeLists.txt's SYJ_EDGEMIND_LLAMA_CPP_TAG), unmodified by this script.

[CmdletBinding()]
param(
    [switch]$EnableAVX,
    [switch]$EnableAVX2,
    [switch]$Clean,
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    # Optional: explicitly select a CMake generator (e.g. "Visual Studio
    # 17 2022"). Not defaulted to any specific Visual Studio version — the
    # project's own documentation (docs/supported-platforms.md) commits
    # only to "CMake + MSVC", never a specific VS release, so hardcoding
    # one here would invent a constraint that doesn't actually exist. Left
    # unset (the default), CMake auto-detects a generator from whatever's
    # installed — this is expected to resolve to a Visual Studio-family
    # generator on the documented toolchain, but is not guaranteed
    # identical across every machine (different installed VS versions,
    # different invoking shell). Set this explicitly for a fully
    # deterministic, repeatable configure on a specific machine.
    [string]$Generator = ""
)

$ErrorActionPreference = "Stop"

# --- 1. Fail on errors (set above) + verify required tools ---
function Require-Command($name, $hint) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        # Write-Host, not Write-Error: with $ErrorActionPreference = "Stop",
        # Write-Error is itself a terminating call, which is not guaranteed
        # to let the following `exit 1` run at all — the same defect class
        # fixed at the two cmake-step checks below. Using Write-Host here
        # keeps the explicit `exit 1` the sole, reliable source of this
        # script's exit code for this failure path.
        Write-Host "ERROR: Required tool '$name' was not found on PATH. $hint" -ForegroundColor Red
        exit 1
    }
}
Require-Command "cmake" "Install CMake (https://cmake.org/download/) and ensure it's on PATH."
Require-Command "git" "Required for llama.cpp's pinned CMake FetchContent step. Install Git for Windows."

# --- 2. Resolve CPU mode (explicit opt-in only — never inferred from the build machine) ---
$avx = $false
$avx2 = $false
$modeName = "Portable"

if ($EnableAVX2) {
    $avx = $true   # explicitly set alongside AVX2 — see header comment: this
                   # does not assume ggml's CMake derives it automatically
    $avx2 = $true
    $modeName = "AVX2"
} elseif ($EnableAVX) {
    $avx = $true
    $modeName = "AVX"
}

Write-Host ""
Write-Host "=== SYJ EdgeMind Windows build ===" -ForegroundColor Cyan
Write-Host "CPU mode: $modeName" -ForegroundColor Yellow
if ($modeName -ne "Portable") {
    Write-Host "  WARNING: this binary requires a $modeName-capable CPU. It will NOT" -ForegroundColor Yellow
    Write-Host "  detect or fall back if the CPU it runs on lacks $modeName support —" -ForegroundColor Yellow
    Write-Host "  the pinned ggml build has no runtime dispatch (compile-time selection" -ForegroundColor Yellow
    Write-Host "  only). Only use this mode if you've confirmed the target CPU." -ForegroundColor Yellow
}
Write-Host "GGML_NATIVE=OFF (always — never inferred from the build machine's CPU)"
Write-Host ""

# --- 3. Clean, if requested ---
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Removing existing build directory: $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
} elseif ((Test-Path $BuildDir) -and -not $Clean) {
    Write-Host "NOTE: '$BuildDir' already exists. CMake will reconfigure it with the" -ForegroundColor DarkYellow
    Write-Host "requested CPU mode, but if you're switching modes from a previous run" -ForegroundColor DarkYellow
    Write-Host "and want a guaranteed-fresh configure, rerun with -Clean." -ForegroundColor DarkYellow
}

# --- 4. Configure ---
# Architecture: x64 is pinned explicitly (-A x64) whenever the resolved
# generator supports the -A flag at all — this project's ONLY documented
# Windows target is x64 (docs/supported-platforms.md), so this is closing
# a real reproducibility gap, not inventing a new constraint. CMake's -A
# flag is specific to certain generator families (Visual Studio and a
# handful of others) — it is REJECTED as an error by generators like
# Ninja/NMake Makefiles, which instead take their architecture from the
# invoking shell/environment. Since -Generator is left unset by default
# and this project's documented toolchain is specifically MSVC/Visual-
# Studio-family, -A x64 is applied whenever no -Generator was given, or
# when the given -Generator name starts with "Visual Studio". For any
# other explicitly-requested generator, -A is omitted and a note is
# printed instead — silently passing -A x64 to an incompatible generator
# would fail the configure step outright, which would be worse than
# omitting it and saying so.
$cmakeArgs = @(
    "-S", ".",
    "-B", $BuildDir,
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DGGML_NATIVE=OFF",
    "-DGGML_AVX=$(if ($avx) { 'ON' } else { 'OFF' })",
    "-DGGML_AVX2=$(if ($avx2) { 'ON' } else { 'OFF' })"
)

$isVsFamily = ($Generator -eq "") -or ($Generator -like "Visual Studio*")
if ($Generator -ne "") {
    $cmakeArgs = @("-G", $Generator) + $cmakeArgs
}
if ($isVsFamily) {
    $cmakeArgs += @("-A", "x64")
} else {
    Write-Host "NOTE: generator '$Generator' does not take CMake's -A flag;" -ForegroundColor DarkYellow
    Write-Host "ensure x64 architecture via your build environment/shell instead" -ForegroundColor DarkYellow
    Write-Host "(this project's only documented Windows target is x64)." -ForegroundColor DarkYellow
}

Write-Host "Configuring: cmake $($cmakeArgs -join ' ')"
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    # Capture immediately, before any other cmdlet call, and use
    # Write-Host rather than Write-Error: with $ErrorActionPreference =
    # "Stop", Write-Error is itself a terminating call, which is not
    # guaranteed to let the following `exit $exitCode` run at all — this
    # was a real defect (script could exit with an unreliable/wrong code
    # on configure failure), found and fixed via external review.
    $exitCode = $LASTEXITCODE
    Write-Host "ERROR: CMake configure failed (exit code $exitCode)." -ForegroundColor Red
    exit $exitCode
}

# --- 5. Build ---
Write-Host ""
Write-Host "Building..."
& cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) {
    $exitCode = $LASTEXITCODE
    Write-Host "ERROR: Build failed (exit code $exitCode)." -ForegroundColor Red
    exit $exitCode
}

# --- 6/7. Report result ---
Write-Host ""
Write-Host "=== Build complete ===" -ForegroundColor Green
Write-Host "CPU mode:        $modeName"
Write-Host "Generator:       $(if ($Generator -ne '') { $Generator } else { '(CMake default)' })"
Write-Host "Architecture:    $(if ($isVsFamily) { 'x64 (explicit)' } else { 'not pinned by this script — see NOTE above' })"
Write-Host "Build directory: $BuildDir"
Write-Host "Look for syj-edgemind.exe under '$BuildDir' (exact subpath depends on your"
Write-Host "CMake generator — e.g. '$BuildDir\$Config\' for a multi-config generator"
Write-Host "like Visual Studio, or directly in '$BuildDir' for others)."
Write-Host ""
Write-Host "NOTE: this script's own execution has not been validated on a real" -ForegroundColor DarkYellow
Write-Host "Windows/MSVC machine — see docs/supported-platforms.md for the exact" -ForegroundColor DarkYellow
Write-Host "current validation status." -ForegroundColor DarkYellow
