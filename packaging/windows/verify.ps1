[CmdletBinding()]
param(
    [string]$BundleDirectory,
    [string]$InstallerPath,
    [string]$LogDirectory = '.'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$logRoot = [IO.Path]::GetFullPath($LogDirectory)
New-Item -ItemType Directory -Force $logRoot | Out-Null
function Invoke-CheckedProgram {
    param([string]$Program, [string[]]$ProgramArguments, [string]$Name, [int]$TimeoutSeconds = 60)
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Program
    $info.WorkingDirectory = [IO.Path]::GetDirectoryName($Program)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $ProgramArguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    if (-not $process.Start()) { throw "Could not launch $Program" }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill($true)
        $process.WaitForExit()
        throw "$Name timed out after $TimeoutSeconds seconds."
    }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    @("PROGRAM: $Program", "ARGUMENTS: $($ProgramArguments -join ' ')", "EXIT: $($process.ExitCode)", $stdout, $stderr) |
        Set-Content (Join-Path $logRoot "windows-$Name.log") -Encoding utf8
    if ($stdout) { Write-Host $stdout }
    if ($stderr) { Write-Host $stderr }
    if ($process.ExitCode -ne 0) { throw "$Name failed with exit code $($process.ExitCode)." }
    $process.Dispose()
}
$names = @('Path', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_ROOT_DIR', 'QML2_IMPORT_PATH',
    'QML_IMPORT_PATH', 'QTWEBENGINEPROCESS_PATH', 'QTWEBENGINE_RESOURCES_PATH', 'QTWEBENGINE_LOCALES_PATH',
    'QT_QPA_PLATFORM', 'AURORA_NO_AUDIO', 'QTWEBENGINE_CHROMIUM_FLAGS', 'QTWEBENGINE_DISABLE_SANDBOX')
$savedEnvironment = @{}
foreach ($name in $names) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $null, 'Process') }
    # Only Windows system DLLs remain externally available: Qt/VC DLLs must be local.
    $env:Path = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:AURORA_NO_AUDIO = '1'
    $env:QTWEBENGINE_CHROMIUM_FLAGS = '--disable-gpu'
    if ($BundleDirectory) {
        $bundle = [IO.Path]::GetFullPath($BundleDirectory)
        foreach ($relative in @('aurora86.exe', 'QtWebEngineProcess.exe', 'Qt6WebEngineCore.dll',
            'platforms/qwindows.dll', 'platforms/qoffscreen.dll', 'resources/qtwebengine_resources.pak',
            'translations/qtwebengine_locales/en-US.pak', 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll')) {
            if (-not (Test-Path (Join-Path $bundle $relative))) { throw "Missing deployed file: $relative" }
        }
        $exe = Join-Path $bundle 'aurora86.exe'
        Invoke-CheckedProgram $exe @('--self-test') 'portable-self-test'
        $state = Join-Path ([IO.Path]::GetTempPath()) ('aurora86-browser-' + [guid]::NewGuid().ToString('N'))
        try {
            Invoke-CheckedProgram $exe @('--browser-check', 'https://example.com', '--state-dir', $state) 'portable-browser' 75
        } finally {
            if (Test-Path $state) { Remove-Item -Recurse -Force $state }
        }
    }
    if ($InstallerPath) {
        $setup = [IO.Path]::GetFullPath($InstallerPath)
        $testRoot = Join-Path ([IO.Path]::GetTempPath()) ('aurora86-setup-' + [guid]::NewGuid().ToString('N'))
        $installed = Join-Path $testRoot 'application'
        $state = Join-Path $testRoot 'saved-virtual-disk'
        $picture = Join-Path $testRoot 'desktop.png'
        Invoke-CheckedProgram $setup @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/DIR=$installed") 'setup-install' 180
        $exe = Join-Path $installed 'aurora86.exe'
        if (-not (Test-Path $exe)) { throw 'The installer did not install aurora86.exe.' }
        Invoke-CheckedProgram $exe @('--self-test') 'installed-self-test'
        Invoke-CheckedProgram $exe @('--screenshot', $picture, '--state-dir', $state) 'installed-persistence'
        $stateFile = Join-Path $state 'vfs.json'
        if (-not (Test-Path $stateFile)) { throw 'The installed application did not save its virtual disk.' }
        $before = (Get-FileHash -Algorithm SHA256 $stateFile).Hash
        Invoke-CheckedProgram (Join-Path $installed 'unins000.exe') @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') 'setup-uninstall' 120
        if (-not (Test-Path $stateFile) -or (Get-FileHash -Algorithm SHA256 $stateFile).Hash -ne $before) {
            throw 'Uninstall changed or removed the saved virtual disk.'
        }
        Remove-Item -Recurse -Force $testRoot
    }
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
}
Write-Host 'PASS: deployed Windows runtime verified without developer PATH or external Qt plugin paths.'
