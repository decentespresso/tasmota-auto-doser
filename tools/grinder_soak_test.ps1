param(
  [string]$Ip = '192.168.178.30',
  [int]$Port = 31980,
  [string]$ExpectedMac = '1C:69:20:0B:54:20',
  [string]$ScaleMac = 'AA:BB:CC:11:22:33',
  [double]$DurationHours = 12,
  [int]$IntervalSeconds = 60,
  [int]$DiscoveryEvery = 10,
  [string]$Output = 'grinder-soak.csv'
)

$ErrorActionPreference = 'Stop'
$base = "http://$Ip"
$deadline = [DateTime]::UtcNow.AddHours($DurationHours)
$iteration = 0

function Invoke-TasmotaCommand($cmd) {
  $encoded = [System.Uri]::EscapeDataString($cmd)
  Invoke-RestMethod -Uri "$base/cm?cmnd=$encoded" -TimeoutSec 5
}

function Test-GrinderSession() {
  $client = [System.Net.Sockets.TcpClient]::new()
  try {
    $client.ReceiveTimeout = 2000
    $client.SendTimeout = 2000
    $client.Connect($Ip, $Port)
    $stream = $client.GetStream()
    $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::ASCII)
    $writer = [System.IO.StreamWriter]::new($stream, [System.Text.Encoding]::ASCII)
    $writer.NewLine = "`n"
    $writer.AutoFlush = $true
    $writer.WriteLine("HELLO $ScaleMac")
    $hello = $reader.ReadLine()
    $writer.WriteLine('PING')
    $ping = $reader.ReadLine()
    $writer.WriteLine('BYE')
    $bye = $reader.ReadLine()
    return ($hello -eq "OK $ExpectedMac state=OFF") -and ($ping -eq "OK $ExpectedMac state=OFF") -and ($bye -eq "OK $ExpectedMac state=OFF")
  } catch {
    return $false
  } finally {
    $client.Close()
  }
}

while ([DateTime]::UtcNow -lt $deadline) {
  $started = [DateTime]::UtcNow
  $session = Test-GrinderSession
  Start-Sleep -Milliseconds 300
  $status = $null
  try {
    $status = (Invoke-TasmotaCommand 'GrinderStatus').GrinderStatus
  } catch {
  }
  $discovery = $null
  if (($iteration % $DiscoveryEvery) -eq 0) {
    & python "$PSScriptRoot/grinder_mdns_probe.py" --expected-mac $ExpectedMac --target-ip $Ip --timeout 3 | Out-Null
    $discovery = $LASTEXITCODE -eq 0
  }
  [pscustomobject]@{
    TimestampUtc = $started.ToString('o')
    TcpSession = $session
    Discovery = $discovery
    PowerOn = $status.Relay.On
    Ip = $status.Net.IP
    Bssid = $status.Net.BSSID
    Rssi = $status.Net.RSSI
    NetworkGeneration = $status.Net.Gen
    ServerGeneration = $status.TCP.Gen
    Restarts = $status.Count.Restart
    MdnsRefreshes = $status.Count.MdnsRefresh
    FreeHeap = $status.Heap.Free
    MinimumHeap = $status.Heap.Min
  } | Export-Csv -LiteralPath $Output -NoTypeInformation -Append
  $iteration++
  Start-Sleep -Seconds $IntervalSeconds
}
