$ErrorActionPreference = 'Stop'
$ExpectedMac = '1C:69:20:0B:54:20'
$ScaleMac = 'AA:BB:CC:11:22:33'
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'grinder_plug_smoke_test.ps1'), [ref]$null, [ref]$errors)
if ($errors.Count) { throw $errors[0] }
foreach ($name in @('Is-OkState', 'Test-NoHelloTimeout', 'Test-ReconnectAfterHeartbeat')) {
  $definition = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $true)
  if ($null -eq $definition) { throw "Missing function: $name" }
  . ([scriptblock]::Create($definition.Extent.Text))
}

function New-GrinderClient {
  if ($script:clients -eq 1 -and $script:closed -contains 1) { throw 'Stale handle closed before replacement session' }
  $script:clients++
  [pscustomobject]@{ Id = $script:clients }
}

function Send-GrinderCommand($conn, $cmd) {
  if ($conn.Id -eq 1 -and $cmd -like 'HELLO *') { $script:greeted = $true }
  if ($conn.Id -eq 2 -and -not $script:released) { return "BUSY $ExpectedMac" }
  "OK $ExpectedMac state=OFF"
}

function Start-Sleep($Milliseconds) {
  $timeout = if ($script:greeted) { 2000 } else { 1000 }
  $script:released = $script:reclaims -and $Milliseconds -ge $timeout
}

function Close-GrinderClient($conn) {
  if ($null -ne $conn) { $script:closed = @($script:closed) + $conn.Id }
}

function Get-PowerState { 'OFF' }
function Add-Result($name, $pass, $detail) { $script:result = [bool]$pass }

foreach ($test in @('Test-NoHelloTimeout', 'Test-ReconnectAfterHeartbeat')) {
  foreach ($reclaims in @($true, $false)) {
    $script:clients = 0
    $script:closed = @()
    $script:greeted = $false
    $script:released = $false
    $script:reclaims = $reclaims
    $script:result = $null
    & $test
    if ($script:result -ne $reclaims) { throw "$test accepted broken timeout reclamation or rejected working reclamation" }
    if ($script:clients -ne 2 -or ($script:closed -join ',') -ne '2,1') { throw "$test did not clean up both handles after the replacement session" }
  }
}
Write-Output 'test_grinder_timeout_smoke passed (offline)'
