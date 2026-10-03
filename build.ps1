param(
    [string]$JuceSource = $env:ENTROPY_JUCE_SOURCE,
    [string]$BuildDirectory = (Join-Path $PSScriptRoot "cmake-build-ninja"),
    [int]$Jobs = 4,
    [switch]$DspOnly
)
$ErrorActionPreference = "Stop"
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmake = if ($cmakeCommand) { $cmakeCommand.Source } else { "C:\Program Files\CMake\bin\cmake.exe" }
if (!(Test-Path $cmake)) { throw "CMake 3.22 or newer is required." }
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (!(Test-Path $vswhere)) { throw "Visual Studio with Desktop development with C++ is required." }
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsPath) { throw "No x64 MSVC toolchain found." }
$msvc = Get-ChildItem "$vsPath\VC\Tools\MSVC" -Directory | Sort-Object { [version]$_.Name } | Select-Object -Last 1
$sdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10"
$sdk = Get-ChildItem "$sdkRoot\Include" -Directory | Where-Object { Test-Path "$sdkRoot\Lib\$($_.Name)\um\x64" } | Sort-Object { [version]$_.Name } | Select-Object -Last 1
if (!$msvc -or !$sdk) { throw "MSVC or Windows SDK is missing." }
$env:INCLUDE = "$($msvc.FullName)\include;$($sdk.FullName)\ucrt;$($sdk.FullName)\shared;$($sdk.FullName)\um;$($sdk.FullName)\winrt"
$env:LIB = "$($msvc.FullName)\lib\x64;$sdkRoot\Lib\$($sdk.Name)\ucrt\x64;$sdkRoot\Lib\$($sdk.Name)\um\x64"
$env:LIBPATH = $env:LIB
$env:PATH = "$($msvc.FullName)\bin\Hostx64\x64;$sdkRoot\bin\$($sdk.Name)\x64;$vsPath\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;$(Split-Path $cmake);$env:PATH"
if (!(Get-Command ninja -ErrorAction SilentlyContinue)) { throw "Ninja is required." }
if (!$JuceSource -and (Test-Path "I:\Organic Chemistry\cmake-build-release-visual-studio\_deps\juce-src\CMakeLists.txt")) {
    $JuceSource = "I:\Organic Chemistry\cmake-build-release-visual-studio\_deps\juce-src"
}
$configure = @("-S", $PSScriptRoot, "-B", $BuildDirectory, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_C_COMPILER=cl", "-DCMAKE_CXX_COMPILER=cl", "-DENTROPY_BUILD_TESTS=ON", "-DENTROPY_COPY_PLUGIN_AFTER_BUILD=OFF")
$configure += "-DENTROPY_BUILD_PLUGIN=$(if ($DspOnly) { 'OFF' } else { 'ON' })"
if ($JuceSource -and !$DspOnly) {
    if (!(Test-Path "$JuceSource\CMakeLists.txt")) { throw "Invalid JUCE source path: $JuceSource" }
    $configure += "-DFETCHCONTENT_SOURCE_DIR_JUCE=$JuceSource"
}
$log = Join-Path $PSScriptRoot "build.log"
Write-Output "MSVC $($msvc.Name) / Windows SDK $($sdk.Name)"
$ErrorActionPreference = "Continue"
& $cmake @configure *> $log
if ($LASTEXITCODE -ne 0) { $code = $LASTEXITCODE; Get-Content $log -Tail 90; exit $code }
& $cmake --build $BuildDirectory --parallel $Jobs *>> $log
if ($LASTEXITCODE -ne 0) { $code = $LASTEXITCODE; Get-Content $log -Tail 100; exit $code }
$ctest = Join-Path (Split-Path $cmake) "ctest.exe"
& $ctest --test-dir $BuildDirectory --output-on-failure -C Release *>> $log
if ($LASTEXITCODE -ne 0) { $code = $LASTEXITCODE; Get-Content $log -Tail 100; exit $code }
Get-Content $log -Tail 14
Write-Output "BUILD_OK"
exit 0
