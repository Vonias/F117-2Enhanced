# VR: builds libf117.so (arm64-v8a) with the Android NDK and copies it into the Quest app.
param([string]$Config = "Release")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$sdk = if ($env:ANDROID_SDK_ROOT) { $env:ANDROID_SDK_ROOT } else { "C:\Program Files (x86)\Android\android-sdk" }
$ndk = Get-ChildItem "$sdk\ndk" -Directory | Sort-Object Name -Descending | Select-Object -First 1
$vs = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake" -Directory | Select-Object -First 1
$cmake = "$($vs.FullName)\CMake\bin\cmake.exe"
$ninja = "$($vs.FullName)\Ninja\ninja.exe"
$build = "$root\f117port\build-android"
& $cmake -S "$root\f117port\android" -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_TOOLCHAIN_FILE=$($ndk.FullName)\build\cmake\android.toolchain.cmake" `
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 "-DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE) { throw "cmake configure failed" }
& $cmake --build $build
if ($LASTEXITCODE) { throw "build failed" }
$dest = Join-Path (Split-Path -Parent $root) "F117A Stealth Fighter 2.0\lib\arm64-v8a"
New-Item -ItemType Directory -Force $dest | Out-Null
Copy-Item "$build\libf117.so" $dest -Force
Write-Host "libf117.so -> $dest"
