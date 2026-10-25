[CmdletBinding()]
param(
    [string]$BuildDirectory = 'work/windows-build',
    [string]$QtRoot = $env:QT_ROOT_DIR,
    [string]$OutputDirectory = 'outputs'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$buildRoot = [IO.Path]::GetFullPath($BuildDirectory, $projectRoot)
$packageOutput = [IO.Path]::GetFullPath($OutputDirectory, $projectRoot)
if (-not $QtRoot -or -not (Test-Path (Join-Path $QtRoot 'bin/windeployqt.exe'))) {
    throw 'QtRoot must point to the installed Windows MSVC Qt SDK.'
}
$binary = @((Join-Path $buildRoot 'Release/aurora86.exe'), (Join-Path $buildRoot 'aurora86.exe')) |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $binary) { throw 'Build the Release aurora86.exe first.' }
New-Item -ItemType Directory -Force $packageOutput | Out-Null
$bundle = Join-Path $packageOutput 'AURORA86-Windows-x86_64'
if (Test-Path $bundle) { Remove-Item -Recurse -Force $bundle }
New-Item -ItemType Directory $bundle | Out-Null
$deployedExe = Join-Path $bundle 'aurora86.exe'
$doomHelper = Join-Path (Split-Path $binary) 'aurora86-doom.exe'
if (-not (Test-Path $doomHelper)) { throw 'Build aurora86-doom.exe before packaging.' }
Copy-Item $doomHelper (Join-Path $bundle 'aurora86-doom.exe') -Force
Copy-Item $binary $deployedExe
$deployLog = Join-Path $packageOutput 'windows-deployment.log'
& (Join-Path $QtRoot 'bin/windeployqt.exe') --release --compiler-runtime --translations en,pt_BR --dir $bundle $deployedExe 2>&1 |
    Tee-Object -FilePath $deployLog
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit $LASTEXITCODE" }

# Ensure headless verification uses the same self-contained plugin layout.
foreach ($name in @('qoffscreen.dll', 'qminimal.dll')) {
    $source = Join-Path $QtRoot "plugins/platforms/$name"
    if (Test-Path $source) { Copy-Item $source (Join-Path $bundle "platforms/$name") -Force }
}
$webProcess = @((Join-Path $QtRoot 'bin/QtWebEngineProcess.exe'), (Join-Path $QtRoot 'libexec/QtWebEngineProcess.exe')) |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $webProcess) { throw 'The installed Qt SDK is missing QtWebEngineProcess.exe.' }
Copy-Item $webProcess (Join-Path $bundle 'QtWebEngineProcess.exe') -Force
New-Item -ItemType Directory -Force (Join-Path $bundle 'resources') | Out-Null
Get-ChildItem (Join-Path $QtRoot 'resources') -File | Where-Object { $_.Extension -in '.pak', '.bin', '.dat' } |
    Copy-Item -Destination (Join-Path $bundle 'resources') -Force
$localeTarget = Join-Path $bundle 'translations/qtwebengine_locales'
New-Item -ItemType Directory -Force $localeTarget | Out-Null
foreach ($locale in @('en-US', 'pt-BR')) {
    Copy-Item (Join-Path $QtRoot "translations/qtwebengine_locales/$locale.pak") $localeTarget -Force
}
foreach ($pattern in @('avcodec-*.dll', 'avformat-*.dll', 'avutil-*.dll', 'swresample-*.dll', 'swscale-*.dll')) {
    Get-ChildItem (Join-Path $QtRoot "bin/$pattern") -File -ErrorAction SilentlyContinue |
        Copy-Item -Destination $bundle -Force
}
@'
[Paths]
Prefix=.
Plugins=.
LibraryExecutables=.
Data=.
Translations=translations
'@ | Set-Content (Join-Path $bundle 'qt.conf') -Encoding ascii

# The Qt deployment tool may copy only the redistributable installer. App-local
# VC143 DLLs make both the ZIP and per-user setup independent of that install.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio 2022 C++ tools were not found.' }
$crt = Get-ChildItem (Join-Path $visualStudio 'VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT') -Directory |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $crt) { throw 'The redistributable VC143 x64 runtime folder was not found.' }
Copy-Item (Join-Path $crt.FullName '*.dll') $bundle -Force
foreach ($name in @('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll')) {
    if (-not (Test-Path (Join-Path $bundle $name))) { throw "Missing app-local runtime: $name" }
}
Get-ChildItem $bundle -File -Filter '*vc_redist*.exe' | Remove-Item -Force

foreach ($name in @('LICENSE', 'README.md', 'README.pt-BR.md')) { Copy-Item (Join-Path $projectRoot $name) $bundle }
Copy-Item (Join-Path $PSScriptRoot 'README.txt') (Join-Path $bundle 'README-Windows.txt')
Copy-Item (Join-Path $PSScriptRoot 'aurora86.ico') $bundle
$licenses = Join-Path $bundle 'licenses'
New-Item -ItemType Directory -Force $licenses | Out-Null
Copy-Item (Join-Path $PSScriptRoot 'licenses/*') $licenses -Recurse -Force
$doomLicenses = Join-Path $licenses 'doom'
New-Item -ItemType Directory -Force $doomLicenses | Out-Null
Get-ChildItem (Join-Path $projectRoot 'third_party/doom') -File | Where-Object { $_.Extension -ne '.wad' -and $_.Extension -ne '.h' } |
    Copy-Item -Destination $doomLicenses -Force
Get-ChildItem (Join-Path $projectRoot 'assets/*LICENSE*') -File | Copy-Item -Destination $licenses
if (Test-Path (Join-Path $QtRoot 'LICENSES')) {
    Copy-Item (Join-Path $QtRoot 'LICENSES') (Join-Path $licenses 'Qt-SDK') -Recurse -Force
}
foreach ($folder in @('mods', 'examples')) {
    if (Test-Path (Join-Path $projectRoot $folder)) {
        Copy-Item (Join-Path $projectRoot $folder) (Join-Path $bundle $folder) -Recurse -Force
    }
}
if (Test-Path (Join-Path $projectRoot 'third_party/lua')) {
    Get-ChildItem (Join-Path $projectRoot 'third_party/lua') -File | Where-Object { $_.Name -match 'LICENSE|COPYING|README' } |
        Copy-Item -Destination $licenses -Force
}
Get-ChildItem (Join-Path $projectRoot 'AURORA86-*.png') -File -ErrorAction SilentlyContinue | Copy-Item -Destination $bundle
@'
Qt runtime: official Qt 6.8.3 Windows MSVC2022 x64 SDK, deployed without source changes.
Qt sources and licenses: https://download.qt.io/archive/qt/6.8/6.8.3/
QtWebEngine/Chromium sources: https://code.qt.io/cgit/qt/qtwebengine.git/tree/?h=v6.8.3
Microsoft VC143 redistributable DLLs: unchanged application-local files from Visual Studio 2022.
Microsoft redistribution terms and file list:
https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution
Windows itself supplies system DLLs and the Universal CRT; Windows 10/11 x64 is required.
Application source and dependency recipes: https://github.com/fis4med2/Aurora86
'@ | Set-Content (Join-Path $licenses 'DEPENDENCIES.txt') -Encoding utf8

# Inspect every shipped PE import before stripping the developer environment.
$dumpbin = Get-ChildItem (Join-Path $visualStudio 'VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe') -File |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $dumpbin) { throw 'dumpbin.exe was not found for dependency verification.' }
$missing = [Collections.Generic.List[string]]::new()
foreach ($file in (Get-ChildItem $bundle -Recurse -File | Where-Object { $_.Extension -in '.dll', '.exe' })) {
    $imports = & $dumpbin.FullName /NOLOGO /DEPENDENTS $file.FullName
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect PE imports: $($file.FullName)" }
    foreach ($line in $imports) {
        $name = $line.Trim()
        if ($name -notmatch '^[a-zA-Z0-9_.+-]+\.dll$' -or $name -match '^(api-ms-win-|ext-ms-win-)') { continue }
        if ((Test-Path (Join-Path $bundle $name)) -or (Test-Path (Join-Path $file.DirectoryName $name))) { continue }
        if ($name -notmatch '^(Qt6|vcruntime|msvcp|concrt)' -and (Test-Path (Join-Path "$env:SystemRoot/System32" $name))) { continue }
        $missing.Add("$($file.Name): $name")
    }
}
if ($missing.Count) { throw "Unresolved runtime dependencies:`n$($missing -join "`n")" }
& (Join-Path $PSScriptRoot 'verify.ps1') -BundleDirectory $bundle -LogDirectory $packageOutput

$versionMatch = [regex]::Match((Get-Content (Join-Path $projectRoot 'CMakeLists.txt') -Raw), 'project\(\s*Aurora86\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)', 'IgnoreCase')
if (-not $versionMatch.Success) { throw 'Could not determine the project version.' }
$version = $versionMatch.Groups[1].Value
$iscc = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe'
if (-not (Test-Path $iscc)) { $iscc = (Get-Command ISCC.exe -ErrorAction Stop).Source }
& $iscc "/DSourceDir=$bundle" "/DOutputDir=$packageOutput" "/DAppVersion=$version" (Join-Path $PSScriptRoot 'aurora86.iss')
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup failed.' }
$setup = Join-Path $packageOutput 'AURORA86-Setup-x64.exe'
& (Join-Path $PSScriptRoot 'verify.ps1') -InstallerPath $setup -LogDirectory $packageOutput
$zip = Join-Path $packageOutput 'AURORA86-Windows-x86_64.zip'
Compress-Archive -LiteralPath $bundle -DestinationPath $zip -CompressionLevel Optimal -Force
$hashes = foreach ($path in @($zip, $setup)) {
    $line = (Get-FileHash -Algorithm SHA256 $path).Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($path)
    $line | Set-Content "$path.sha256" -Encoding ascii
    $line
}
$hashes | Set-Content (Join-Path $packageOutput 'SHA256SUMS-Windows.txt') -Encoding ascii
Write-Host "Windows ZIP and per-user installer are ready in $packageOutput"
