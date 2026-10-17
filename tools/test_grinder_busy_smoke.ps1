$ErrorActionPreference = 'Stop'
$ExpectedMac = '1C:69:20:0B:54:20'
$ScaleMac = 'AA:BB:CC:11:22:33'
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'grinder_plug_smoke_test.ps1'), [ref]$null, [ref]$errors)
if ($errors.Count) { throw $errors[0] }
foreach ($name in @('Is-OkState', 'Test-RepeatedBusy')) {
  $definition = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $true)
  if ($null -eq $definition) { throw "Missing function: $name" }
  . ([scriptblock]::Create($definition.Extent.Text))
}

function New-GrinderClient {
  if ($script:clients -and ($script:now - $script:lastRx) -ge 2000) { throw 'Owner heartbeat expired' }
  $script:clients++
  [pscustomobject]@{ Id = $script:clients; Reader = [System.IO.StringReader]::new("BUSY $ExpectedMac") }
}

function Send-GrinderCommand($conn, $cmd) {
  if ($conn.Id -ne 1) { throw 'Command sent to non-owner' }
  if (($script:now - $script:lastRx) -ge 2000) { throw 'Owner heartbeat expired' }
  $script:lastRx = $script:now
  if ($cmd -eq 'PING') {
    $script:pings++
    if ($script:badHeartbeat) { return "ERR $ExpectedMac reason=test" }
  }
  "OK $ExpectedMac state=OFF"
}

function Start-Sleep($Milliseconds) { $script:now += $Milliseconds }
function Close-GrinderClient($conn) { if ($null -ne $conn) { $conn.Reader.Dispose() } }
function Add-Result($name, $pass, $detail) { if (-not $pass) { throw "$name failed: $detail" } }

function Reset-FakeTransport {
  $script:clients = 0
  $script:now = 0
  $script:lastRx = 0
  $script:pings = 0
  $script:badHeartbeat = $false
}

Reset-FakeTransport
Test-RepeatedBusy
if ($script:clients -ne 21 -or $script:pings -ne 20 -or $script:now -ne 5500) { throw 'BUSY sequence was incomplete' }

Reset-FakeTransport
$script:badHeartbeat = $true
$rejected = $false
try { Test-RepeatedBusy } catch {
  if ($_.Exception.Message -notlike '*heartbeat*') { throw }
  $rejected = $true
}
if (-not $rejected) { throw 'Invalid owner heartbeat was accepted' }
Write-Output 'test_grinder_busy_smoke passed (offline)'
