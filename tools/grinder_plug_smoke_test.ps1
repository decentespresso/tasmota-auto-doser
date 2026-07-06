param(
  [string]$Ip = '192.168.178.30',
  [int]$Port = 31980,
  [string]$ExpectedMac = '1C:69:20:0B:54:20',
  [string]$ScaleMac = 'AA:BB:CC:11:22:33'
)

$ErrorActionPreference = 'Stop'
$base = "http://$Ip"
$results = New-Object System.Collections.Generic.List[object]

function Add-Result($name, $pass, $detail) {
  $results.Add([pscustomobject]@{
    Name = $name
    Pass = [bool]$pass
    Detail = $detail
  })
}

function Invoke-TasmotaCommand($cmd) {
  $encoded = [System.Uri]::EscapeDataString($cmd)
  Invoke-RestMethod -Uri "$base/cm?cmnd=$encoded" -TimeoutSec 5
}

function Get-PowerState() {
  $response = Invoke-TasmotaCommand 'Power1'
  [string]$response.POWER
}

function Wait-PowerState($wanted, $timeoutMs) {
  $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
  $last = $null
  while ([DateTime]::UtcNow -lt $deadline) {
    $last = Get-PowerState
    if ($last -eq $wanted) {
      return [pscustomobject]@{
        Reached = $true
        Last = $last
      }
    }
    Start-Sleep -Milliseconds 100
  }
  [pscustomobject]@{
    Reached = $false
    Last = $last
  }
}

function New-GrinderClient() {
  $client = [System.Net.Sockets.TcpClient]::new()
  $client.ReceiveTimeout = 2000
  $client.SendTimeout = 2000
  $client.Connect($Ip, $Port)
  $stream = $client.GetStream()
  $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::ASCII)
  $writer = [System.IO.StreamWriter]::new($stream, [System.Text.Encoding]::ASCII)
  $writer.NewLine = "`n"
  $writer.AutoFlush = $true
  [pscustomobject]@{
    Client = $client
    Reader = $reader
    Writer = $writer
  }
}

function Send-GrinderCommand($conn, $cmd) {
  $conn.Writer.WriteLine($cmd)
  $conn.Reader.ReadLine()
}

function Send-GrinderFastOff($conn) {
  $stream = $conn.Client.GetStream()
  $stream.WriteByte([byte][char]'!')
  $stream.Flush()
  $conn.Reader.ReadLine()
}

function Close-GrinderClient($conn) {
  if ($null -ne $conn) {
    try {
      $conn.Client.Close()
    } catch {
    }
  }
}

function Is-OkState($line, $state) {
  $line -eq "OK $ExpectedMac state=$state"
}

function Set-RelayOff() {
  try {
    $null = Invoke-TasmotaCommand 'Power1 OFF'
  } catch {
  }
}

function Test-InitialState() {
  Set-RelayOff
  Start-Sleep -Milliseconds 250
  $initialPower = Get-PowerState
  Add-Result 'Initial relay OFF' ($initialPower -eq 'OFF') "Power1=$initialPower"
}

function Test-HttpOnBlocked() {
  Set-RelayOff
  Start-Sleep -Milliseconds 250
  $before = Get-PowerState
  $response = Invoke-TasmotaCommand 'Power1 ON'
  Start-Sleep -Milliseconds 250
  $after250 = Get-PowerState
  Start-Sleep -Milliseconds 1000
  $after1250 = Get-PowerState
  $pass = ($before -eq 'OFF') -and ($after250 -eq 'OFF') -and ($after1250 -eq 'OFF')
  Add-Result 'HTTP Power1 ON blocked' $pass "before=$before response=$($response.POWER) after250ms=$after250 after1250ms=$after1250"
}

function Test-HttpToggleBlocked() {
  Set-RelayOff
  Start-Sleep -Milliseconds 250
  $before = Get-PowerState
  $response = Invoke-TasmotaCommand 'Power1 TOGGLE'
  Start-Sleep -Milliseconds 250
  $after250 = Get-PowerState
  Start-Sleep -Milliseconds 1000
  $after1250 = Get-PowerState
  $pass = ($before -eq 'OFF') -and ($after250 -eq 'OFF') -and ($after1250 -eq 'OFF')
  Add-Result 'HTTP Power1 TOGGLE blocked' $pass "before=$before response=$($response.POWER) after250ms=$after250 after1250ms=$after1250"
}

function Test-BacklogOnBlocked() {
  Set-RelayOff
  Start-Sleep -Milliseconds 250
  $before = Get-PowerState
  $null = Invoke-TasmotaCommand 'Backlog Power1 ON'
  Start-Sleep -Milliseconds 250
  $after250 = Get-PowerState
  Start-Sleep -Milliseconds 1000
  $after1250 = Get-PowerState
  $pass = ($before -eq 'OFF') -and ($after250 -eq 'OFF') -and ($after1250 -eq 'OFF')
  Add-Result 'Backlog Power1 ON blocked' $pass "before=$before after250ms=$after250 after1250ms=$after1250"
}

function Test-BadHello() {
  Set-RelayOff
  $conn = $null
  try {
    $conn = New-GrinderClient
    $response = Send-GrinderCommand $conn 'HELLO not-a-mac'
    Start-Sleep -Milliseconds 150
    $power = Get-PowerState
    $pass = ($response -like "ERR $ExpectedMac reason=*") -and ($power -eq 'OFF')
    Add-Result 'Bad HELLO rejected safe' $pass "response=$response Power1=$power"
  } finally {
    Close-GrinderClient $conn
  }
}

function Test-OneClientRule() {
  Set-RelayOff
  $first = $null
  $second = $null
  try {
    $first = New-GrinderClient
    $hello = Send-GrinderCommand $first "HELLO $ScaleMac"
    $second = New-GrinderClient
    $busy = $second.Reader.ReadLine()
    try {
      $second.Writer.WriteLine('ON')
    } catch {
    }
    Start-Sleep -Milliseconds 250
    $power = Get-PowerState
    $bye = Send-GrinderCommand $first 'BYE'
    $pass = (Is-OkState $hello 'OFF') -and ($busy -eq "BUSY $ExpectedMac") -and ($power -eq 'OFF') -and (Is-OkState $bye 'OFF')
    Add-Result 'One client and BUSY isolation' $pass "hello=$hello busy=$busy busyOnPower=$power bye=$bye"
  } finally {
    Close-GrinderClient $second
    Close-GrinderClient $first
  }
}

function Test-TcpOnOff() {
  Set-RelayOff
  $conn = $null
  try {
    $conn = New-GrinderClient
    $hello = Send-GrinderCommand $conn "HELLO $ScaleMac"
    $on = Send-GrinderCommand $conn 'ON'
    $onPower = Wait-PowerState 'ON' 1500
    $stateOn = Send-GrinderCommand $conn 'STATE'
    $off = Send-GrinderFastOff $conn
    $offPower = Wait-PowerState 'OFF' 1500
    $dupOff = Send-GrinderCommand $conn 'OFF'
    $dupOffPower = Wait-PowerState 'OFF' 1500
    $bye = Send-GrinderCommand $conn 'BYE'
    $pass = (Is-OkState $hello 'OFF') -and (Is-OkState $on 'ON') -and $onPower.Reached -and (Is-OkState $stateOn 'ON') -and (Is-OkState $off 'OFF') -and $offPower.Reached -and (Is-OkState $dupOff 'OFF') -and $dupOffPower.Reached -and (Is-OkState $bye 'OFF')
    Add-Result 'TCP ON fast OFF STATE duplicate OFF' $pass "hello=$hello on=$on onPower=$($onPower.Last) state=$stateOn off=$off offPower=$($offPower.Last) dupOff=$dupOff dupOffPower=$($dupOffPower.Last) bye=$bye"
  } finally {
    Close-GrinderClient $conn
  }
}

function Test-ByeWhileOn() {
  Set-RelayOff
  $conn = $null
  try {
    $conn = New-GrinderClient
    $hello = Send-GrinderCommand $conn "HELLO $ScaleMac"
    $on = Send-GrinderCommand $conn 'ON'
    $onPower = Wait-PowerState 'ON' 1500
    $bye = Send-GrinderCommand $conn 'BYE'
    $offPower = Wait-PowerState 'OFF' 1500
    $pass = (Is-OkState $hello 'OFF') -and (Is-OkState $on 'ON') -and $onPower.Reached -and (Is-OkState $bye 'OFF') -and $offPower.Reached
    Add-Result 'BYE while ON forces OFF' $pass "hello=$hello on=$on onPower=$($onPower.Last) bye=$bye finalPower=$($offPower.Last)"
  } finally {
    Close-GrinderClient $conn
  }
}

function Test-DroppedClient() {
  Set-RelayOff
  $conn = $null
  try {
    $conn = New-GrinderClient
    $hello = Send-GrinderCommand $conn "HELLO $ScaleMac"
    $on = Send-GrinderCommand $conn 'ON'
    $onPower = Wait-PowerState 'ON' 1500
    Close-GrinderClient $conn
    $conn = $null
    $offPower = Wait-PowerState 'OFF' 3000
    $pass = (Is-OkState $hello 'OFF') -and (Is-OkState $on 'ON') -and $onPower.Reached -and $offPower.Reached
    Add-Result 'Dropped TCP client forces OFF' $pass "hello=$hello on=$on onPower=$($onPower.Last) finalPower=$($offPower.Last)"
  } finally {
    Close-GrinderClient $conn
  }
}

function Test-HeartbeatTimeout() {
  Set-RelayOff
  $conn = $null
  try {
    $conn = New-GrinderClient
    $hello = Send-GrinderCommand $conn "HELLO $ScaleMac"
    $on = Send-GrinderCommand $conn 'ON'
    $onPower = Wait-PowerState 'ON' 1500
    Start-Sleep -Milliseconds 2200
    $offPower = Get-PowerState
    $pass = (Is-OkState $hello 'OFF') -and (Is-OkState $on 'ON') -and $onPower.Reached -and ($offPower -eq 'OFF')
    Add-Result 'Heartbeat timeout forces OFF' $pass "hello=$hello on=$on onPower=$($onPower.Last) finalPower=$offPower"
  } finally {
    Close-GrinderClient $conn
  }
}

try {
  Test-InitialState
  Test-HttpOnBlocked
  Test-HttpToggleBlocked
  Test-BacklogOnBlocked
  Test-BadHello
  Test-OneClientRule
  Test-TcpOnOff
  Test-ByeWhileOn
  Test-DroppedClient
  Test-HeartbeatTimeout
} finally {
  Set-RelayOff
}

$failed = 0
foreach ($result in $results) {
  $prefix = 'PASS'
  if (-not $result.Pass) {
    $prefix = 'FAIL'
    $failed = $failed + 1
  }
  Write-Output "$prefix`t$($result.Name)`t$($result.Detail)"
}

Write-Output "SUMMARY`t$($results.Count - $failed)/$($results.Count) passed"

if ($failed -gt 0) {
  exit 1
}
