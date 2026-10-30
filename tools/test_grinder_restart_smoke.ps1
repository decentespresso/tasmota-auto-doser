$ErrorActionPreference = 'Stop'
$ExpectedMac = '1C:69:20:0B:54:20'
$ScaleMac = 'AA:BB:CC:11:22:33'
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'grinder_plug_smoke_test.ps1'), [ref]$null, [ref]$errors)
if ($errors.Count) { throw $errors[0] }
foreach ($name in @('Is-OkState', 'Test-ControlledRestart')) {
  $definition = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $true)
  if ($null -eq $definition) { throw "Missing function: $name" }
  . ([scriptblock]::Create($definition.Extent.Text))
}

function Invoke-TasmotaCommand($cmd) {
  if ($cmd -eq 'GrinderStatus') {
    $script:statusQueries++
    return [pscustomobject]@{ GrinderStatus = $script:before }
  }
  if ($cmd -eq 'GrinderRestart') { return [pscustomobject]@{ GrinderStatus = $script:after } }
  throw "Unexpected HTTP command: $cmd"
}

function New-GrinderClient {
  if ($script:scenario -eq 'missing-listener') { throw 'Mock listener unavailable' }
  $script:clients++
  [pscustomobject]@{ Id = $script:clients }
}

function Send-GrinderCommand($conn, $cmd) {
  if ($conn.Id -ne 1) { throw 'Unexpected client' }
  $script:commands = @($script:commands) + $cmd
  if ($script:scenario -eq 'wrong-mac') { return 'OK FF:FF:FF:FF:FF:FF state=OFF' }
  "OK $ExpectedMac state=OFF"
}

function Set-RelayOff {}
function Get-PowerState { 'OFF' }
function Start-Sleep($Milliseconds) {}
function Close-GrinderClient($conn) { if ($null -ne $conn) { $script:closed++ } }
function Add-Result($name, $pass, $detail) { $script:result = [bool]$pass }

foreach ($scenario in @('working', 'no-op', 'missing-listener', 'wrong-mac', 'missing-before')) {
  $script:scenario = $scenario
  $script:before = if ($scenario -eq 'missing-before') { $null } else { [pscustomobject]@{ TCP = [pscustomobject]@{ Listen = 1; Gen = 7 } } }
  $generation = if ($scenario -eq 'no-op') { 7 } else { 8 }
  $script:after = [pscustomobject]@{ TCP = [pscustomobject]@{ Listen = 1; Gen = $generation } }
  $script:statusQueries = 0
  $script:clients = 0
  $script:commands = @()
  $script:closed = 0
  $script:result = $null
  try { Test-ControlledRestart } catch {
    if ($scenario -ne 'missing-listener' -or $_.Exception.Message -ne 'Mock listener unavailable') { throw }
  }
  if ($scenario -eq 'working') {
    if (-not $script:result -or $script:statusQueries -ne 1 -or ($script:commands -join ',') -ne "HELLO $ScaleMac,BYE" -or $script:closed -ne 1) { throw 'Restart did not prove generation advance and a fresh session with cleanup' }
  } elseif ($script:result) {
    throw "Restart incorrectly passed: $scenario"
  }
}
Write-Output 'test_grinder_restart_smoke passed (offline)'
