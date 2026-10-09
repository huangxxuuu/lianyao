[CmdletBinding()]
param(
    [ValidateSet('All', 'Static', 'Firmware')]
    [string]$Mode = 'All',
    [string]$IdfPath,
    [string]$IdfToolsPath,
    [string]$PythonPath,
    [string]$WslDistribution,
    [switch]$KeepBuildDirectory
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$buildRoot = Join-Path $repoRoot 'build'
$logRoot = Join-Path $buildRoot 'logs'
$runId = Get-Date -Format 'yyyyMMdd-HHmmss'
$logPath = Join-Path $logRoot "firmware-build-$runId.log"
$summaryPath = Join-Path $buildRoot 'last-build-result.json'
$validationBuildDir = $null
$firmwarePath = $null
$firmwareSha256 = $null
$archivePath = $null
$elfSha256 = $null
$idfVersion = $null
$hostTests = if ($Mode -eq 'Firmware') { 'NOT_RUN' } else { 'FAIL' }
$firmwareBuild = if ($Mode -eq 'Static') { 'NOT_RUN' } else { 'FAIL' }
$currentStage = 'initialization'
$failureMessage = $null
$startedAt = (Get-Date).ToUniversalTime()
$exitCode = 1

New-Item -ItemType Directory -Path $logRoot -Force | Out-Null

function Write-BuildMessage {
    param([string]$Message)
    Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $Message"
}

function Invoke-LoggedNative {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$ArgumentList = @()
    )
    Write-BuildMessage "RUN: $FilePath $($ArgumentList -join ' ')"
    & $FilePath @ArgumentList 2>&1 | ForEach-Object { $_ | Out-Host }
    $nativeExitCode = $LASTEXITCODE
    if ($nativeExitCode -ne 0) {
        throw "Command failed with exit code ${nativeExitCode}: $FilePath"
    }
}

function Resolve-EspIdfPath {
    param([string]$RequestedPath)
    $candidates = [System.Collections.Generic.List[string]]::new()
    if ($RequestedPath) { $candidates.Add($RequestedPath) }
    if ($env:IDF_PATH) { $candidates.Add($env:IDF_PATH) }
    $idfCommand = Get-Command idf.py -ErrorAction SilentlyContinue
    if ($idfCommand) {
        $candidates.Add((Split-Path -Parent (Split-Path -Parent $idfCommand.Source)))
    }
    if ($env:SystemDrive) {
        $candidates.Add((Join-Path $env:SystemDrive 'Espressif\frameworks\esp-idf-v5.5.3'))
    }
    if ($env:USERPROFILE) {
        $candidates.Add((Join-Path $env:USERPROFILE 'esp\v5.5.3\esp-idf'))
        $candidates.Add((Join-Path $env:USERPROFILE 'esp\esp-idf'))
    }
    foreach ($candidate in $candidates | Select-Object -Unique) {
        if (-not $candidate) { continue }
        $resolved = Resolve-Path -LiteralPath $candidate -ErrorAction SilentlyContinue
        if ($resolved -and (Test-Path -LiteralPath (Join-Path $resolved.Path 'tools\idf.py'))) {
            return $resolved.Path
        }
    }
    throw 'ESP-IDF was not found. Activate ESP-IDF 5.5.3 or pass -IdfPath explicitly.'
}

function Resolve-EspIdfToolsPath {
    param([string]$RequestedPath, [string]$ResolvedIdfPath)
    $candidates = [System.Collections.Generic.List[string]]::new()
    if ($RequestedPath) { $candidates.Add($RequestedPath) }
    if ($env:IDF_TOOLS_PATH) { $candidates.Add($env:IDF_TOOLS_PATH) }
    $frameworksDirectory = Split-Path -Parent $ResolvedIdfPath
    if ((Split-Path -Leaf $frameworksDirectory) -eq 'frameworks') {
        $candidates.Add((Split-Path -Parent $frameworksDirectory))
    }
    if ($env:USERPROFILE) { $candidates.Add((Join-Path $env:USERPROFILE '.espressif')) }
    foreach ($candidate in $candidates | Select-Object -Unique) {
        if (-not $candidate) { continue }
        $resolved = Resolve-Path -LiteralPath $candidate -ErrorAction SilentlyContinue
        if ($resolved -and (Test-Path -LiteralPath (Join-Path $resolved.Path 'tools'))) {
            return $resolved.Path
        }
    }
    throw 'ESP-IDF tools were not found. Pass -IdfToolsPath explicitly.'
}

function Resolve-EspIdfPython {
    param([string]$RequestedPath, [string]$ResolvedToolsPath)
    if ($RequestedPath) {
        $resolved = Resolve-Path -LiteralPath $RequestedPath -ErrorAction SilentlyContinue
        if ($resolved) { return $resolved.Path }
        throw "Python executable does not exist: $RequestedPath"
    }
    if ($env:IDF_PYTHON_ENV_PATH) {
        $activePython = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe'
        if (Test-Path -LiteralPath $activePython) { return $activePython }
    }
    $pythonEnvironmentRoot = Join-Path $ResolvedToolsPath 'python_env'
    if (Test-Path -LiteralPath $pythonEnvironmentRoot) {
        $matches = @(Get-ChildItem -LiteralPath $pythonEnvironmentRoot -Directory -Filter 'idf5.5_py*_env' |
            ForEach-Object { Join-Path $_.FullName 'Scripts\python.exe' } |
            Where-Object { Test-Path -LiteralPath $_ })
        if ($matches.Count -eq 1) { return $matches[0] }
        if ($matches.Count -gt 1) {
            throw 'Multiple ESP-IDF 5.5 Python environments were found. Pass -PythonPath explicitly.'
        }
    }
    throw 'The ESP-IDF 5.5 Python environment was not found. Pass -PythonPath explicitly.'
}

function Enable-EspIdfEnvironment {
    $resolvedIdfPath = Resolve-EspIdfPath -RequestedPath $IdfPath
    $resolvedToolsPath = Resolve-EspIdfToolsPath -RequestedPath $IdfToolsPath -ResolvedIdfPath $resolvedIdfPath
    $resolvedPython = Resolve-EspIdfPython -RequestedPath $PythonPath -ResolvedToolsPath $resolvedToolsPath
    $env:IDF_PATH = $resolvedIdfPath
    $env:IDF_TOOLS_PATH = $resolvedToolsPath
    $exportOutput = & $resolvedPython (Join-Path $resolvedIdfPath 'tools\idf_tools.py') export --format key-value 2>&1
    if ($LASTEXITCODE -ne 0) {
        $exportOutput | ForEach-Object { $_ | Out-Host }
        throw 'ESP-IDF environment export failed.'
    }
    foreach ($lineObject in $exportOutput) {
        $line = [string]$lineObject
        if ($line -notmatch '^([A-Z][A-Z0-9_]*)=(.*)$') { continue }
        $name = $Matches[1]
        $value = $Matches[2]
        if ($name -eq 'PATH') { $value = $value.Replace('%PATH%', $env:PATH) }
        [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    }
    $versionOutput = & $resolvedPython (Join-Path $resolvedIdfPath 'tools\idf.py') --version 2>&1
    if ($LASTEXITCODE -ne 0) {
        $versionOutput | ForEach-Object { $_ | Out-Host }
        throw 'Unable to read the ESP-IDF version.'
    }
    $script:idfVersion = (($versionOutput | Select-Object -Last 1) -as [string]).Trim()
    if ($script:idfVersion -notmatch 'v5\.5\.3(?:\s|$)') {
        throw "ESP-IDF v5.5.3 is required; detected '$($script:idfVersion)'."
    }
    Write-BuildMessage "ESP-IDF: $($script:idfVersion)"
    Write-BuildMessage "IDF_PATH: $resolvedIdfPath"
    return @{ IdfPath = $resolvedIdfPath; Python = $resolvedPython }
}

function Invoke-StaticGate {
    $wsl = Get-Command wsl.exe -ErrorAction SilentlyContinue
    if (-not $wsl) { throw 'wsl.exe is required for host/static tests on Windows.' }
    $arguments = [System.Collections.Generic.List[string]]::new()
    if ($WslDistribution) {
        $arguments.Add('--distribution')
        $arguments.Add($WslDistribution)
    }
    $arguments.Add('--cd')
    $arguments.Add($repoRoot)
    $arguments.Add('bash')
    $arguments.Add('./tools/validate.sh')
    $arguments.Add('--static')
    Invoke-LoggedNative -FilePath $wsl.Source -ArgumentList $arguments.ToArray()
    $script:hostTests = 'PASS'
}

function Invoke-FirmwareGate {
    $environment = Enable-EspIdfEnvironment
    $idfPy = Join-Path $environment.IdfPath 'tools\idf.py'
    $verifyScript = Join-Path $repoRoot 'tools\verify_firmware.py'
    $archiveScript = Join-Path $repoRoot 'tools\archive_firmware.py'
    $archiveRoot = Join-Path $buildRoot 'firmware'
    $script:validationBuildDir = Join-Path $buildRoot ("validate-firmware-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $script:validationBuildDir -Force | Out-Null
    $previousSdkconfigDefaults = $env:SDKCONFIG_DEFAULTS
    try {
        $env:SDKCONFIG_DEFAULTS = Join-Path $repoRoot 'sdkconfig.defaults'
        Invoke-LoggedNative -FilePath $environment.Python -ArgumentList @(
            $idfPy, '-B', $script:validationBuildDir,
            '-D', "SDKCONFIG=$(Join-Path $script:validationBuildDir 'sdkconfig')", 'build')
        Invoke-LoggedNative -FilePath $environment.Python -ArgumentList @(
            $idfPy, '-B', $script:validationBuildDir, 'merge-bin', '-o',
            (Join-Path $script:validationBuildDir 'FoloToy-AI-Passport-full.bin'))
        Invoke-LoggedNative -FilePath $environment.Python -ArgumentList @($verifyScript, $script:validationBuildDir)
        Invoke-LoggedNative -FilePath $environment.Python -ArgumentList @(
            $archiveScript, 'create', $script:validationBuildDir, '--archive-root', $archiveRoot)
        $mergedImage = Join-Path $script:validationBuildDir 'FoloToy-AI-Passport-full.bin'
        $script:firmwareSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $mergedImage).Hash.ToLowerInvariant()
        $script:archivePath = Join-Path $archiveRoot $script:firmwareSha256
        Invoke-LoggedNative -FilePath $environment.Python -ArgumentList @($archiveScript, 'verify', $script:archivePath)
        $manifest = Get-Content -LiteralPath (Join-Path $script:archivePath 'manifest.json') -Raw | ConvertFrom-Json
        if ($manifest.full_bin_sha256 -ne $script:firmwareSha256) {
            throw 'The archived firmware hash does not match the built merged image.'
        }
        $script:elfSha256 = $manifest.app_elf_sha256
        $script:firmwarePath = Join-Path $buildRoot 'FoloToy-AI-Passport-full.bin'
        Copy-Item -LiteralPath $mergedImage -Destination $script:firmwarePath -Force
        $copiedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $script:firmwarePath).Hash.ToLowerInvariant()
        if ($copiedHash -ne $script:firmwareSha256) {
            throw 'The delivered firmware hash changed while copying the image.'
        }
        $script:firmwareBuild = 'PASS'
    }
    finally {
        $env:SDKCONFIG_DEFAULTS = $previousSdkconfigDefaults
    }
}

function Remove-SuccessfulValidationDirectory {
    if (-not $script:validationBuildDir -or $KeepBuildDirectory -or $script:firmwareBuild -ne 'PASS') { return }
    $resolvedBuildRoot = (Resolve-Path -LiteralPath $buildRoot).Path
    $resolvedValidation = (Resolve-Path -LiteralPath $script:validationBuildDir).Path
    $requiredPrefix = $resolvedBuildRoot.TrimEnd('\') + '\validate-firmware-'
    if (-not $resolvedValidation.StartsWith($requiredPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected validation directory: $resolvedValidation"
    }
    Remove-Item -LiteralPath $resolvedValidation -Recurse -Force
    $script:validationBuildDir = $null
}

function Write-BuildSummary {
    $finishedAt = (Get-Date).ToUniversalTime()
    $result = if ($script:exitCode -eq 0) { 'PASS' } else { 'FAIL' }
    $summary = [ordered]@{
        schema_version = 1; result = $result; exit_code = $script:exitCode; mode = $Mode
        failed_stage = if ($result -eq 'FAIL') { $script:currentStage } else { $null }
        error = $script:failureMessage; started_at_utc = $startedAt.ToString('o')
        finished_at_utc = $finishedAt.ToString('o')
        duration_seconds = [math]::Round(($finishedAt - $startedAt).TotalSeconds, 3)
        idf_version = $script:idfVersion; host_tests = $script:hostTests
        firmware_build = $script:firmwareBuild; device_tests = 'NOT_RUN'
        firmware_path = $script:firmwarePath; firmware_sha256 = $script:firmwareSha256
        archive_path = $script:archivePath; elf_sha256 = $script:elfSha256
        retained_validation_build = $script:validationBuildDir; log_path = $logPath
    }
    $temporarySummary = "$summaryPath.tmp"
    $summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $temporarySummary -Encoding utf8
    Move-Item -LiteralPath $temporarySummary -Destination $summaryPath -Force
    Write-Host ''
    Write-Host "BUILD_RESULT=$result"
    Write-Host "EXIT_CODE=$($script:exitCode)"
    Write-Host "FAILED_STAGE=$(if ($result -eq 'FAIL') { $script:currentStage } else { '' })"
    Write-Host "HOST_TESTS=$($script:hostTests)"
    Write-Host "FIRMWARE_BUILD=$($script:firmwareBuild)"
    Write-Host 'DEVICE_TESTS=NOT_RUN'
    Write-Host "FIRMWARE_PATH=$($script:firmwarePath)"
    Write-Host "FIRMWARE_SHA256=$($script:firmwareSha256)"
    Write-Host "ARCHIVE_PATH=$($script:archivePath)"
    Write-Host "ELF_SHA256=$($script:elfSha256)"
    Write-Host "RETAINED_BUILD_PATH=$($script:validationBuildDir)"
    Write-Host "LOG_PATH=$logPath"
    Write-Host "SUMMARY_PATH=$summaryPath"
    if ($script:failureMessage) { Write-Host "ERROR=$($script:failureMessage)" }
}

$transcriptStarted = $false
try {
    Start-Transcript -LiteralPath $logPath -Force | Out-Null
    $transcriptStarted = $true
    Write-BuildMessage "Repository: $repoRoot"
    Write-BuildMessage "Mode: $Mode"
    if ($Mode -in @('All', 'Static')) {
        $script:currentStage = 'static-validation'
        Invoke-StaticGate
    }
    if ($Mode -in @('All', 'Firmware')) {
        $script:currentStage = 'firmware-validation'
        Invoke-FirmwareGate
    }
    $script:currentStage = 'cleanup'
    Remove-SuccessfulValidationDirectory
    $script:currentStage = 'complete'
    $script:exitCode = 0
}
catch {
    $script:failureMessage = $_.Exception.Message
    Write-Error $script:failureMessage
}
finally {
    Write-BuildSummary
    if ($transcriptStarted) { Stop-Transcript | Out-Null }
}
exit $script:exitCode
