param(
  [string]$Ip = '192.168.178.30',
  [int]$Port = 31980,
  [string]$ExpectedMac = '1C:69:20:0B:54:20',
  [string]$ScaleMac = 'AA:BB:CC:11:22:33',
  [double]$DurationHours = 12,
  [int]$IntervalSeconds = 2,
  [int]$DiscoveryEvery = 5,
  [int]$ProbeTimeoutMs = 2000,
  [string]$Output = 'grinder-soak.csv'
)

$ErrorActionPreference = 'Stop'
$base = "http://$Ip"
$deadline = [DateTime]::UtcNow.AddHours($DurationHours)
$iteration = 0
$previousStarted = $null

function Invoke-TasmotaStatus {
  $watch = [System.Diagnostics.Stopwatch]::StartNew()
  try {
    $encoded = [System.Uri]::EscapeDataString('GrinderStatus')
    $timeoutSeconds = [Math]::Max(1, [Math]::Ceiling($ProbeTimeoutMs / 1000.0))
    $response = Invoke-RestMethod -Uri "$base/cm?cmnd=$encoded" -TimeoutSec $timeoutSeconds
    return [pscustomobject]@{
      Success = $true
      LatencyMs = $watch.ElapsedMilliseconds
      Status = $response.GrinderStatus
      Error = $null
    }
  } catch {
    return [pscustomobject]@{
      Success = $false
      LatencyMs = $watch.ElapsedMilliseconds
      Status = $null
      Error = $_.Exception.Message
    }
  } finally {
    $watch.Stop()
  }
}

function Test-GrinderSession {
  $watch = [System.Diagnostics.Stopwatch]::StartNew()
  $client = [System.Net.Sockets.TcpClient]::new()
  try {
    $connect = $client.ConnectAsync($Ip, $Port)
    if (!$connect.Wait($ProbeTimeoutMs)) {
      throw "TCP connect timeout after $ProbeTimeoutMs ms"
    }
    $client.ReceiveTimeout = $ProbeTimeoutMs
    $client.SendTimeout = $ProbeTimeoutMs
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
    $success = ($hello -eq "OK $ExpectedMac state=OFF") -and
               ($ping -eq "OK $ExpectedMac state=OFF") -and
               ($bye -eq "OK $ExpectedMac state=OFF")
    return [pscustomobject]@{
      Success = $success
      LatencyMs = $watch.ElapsedMilliseconds
      Error = if ($success) { $null } else { "Unexpected response: HELLO=$hello PING=$ping BYE=$bye" }
    }
  } catch {
    return [pscustomobject]@{
      Success = $false
      LatencyMs = $watch.ElapsedMilliseconds
      Error = $_.Exception.Message
    }
  } finally {
    $watch.Stop()
    $client.Close()
  }
}

function Test-GrinderDiscovery {
  $watch = [System.Diagnostics.Stopwatch]::StartNew()
  try {
    $timeoutSeconds = [Math]::Max(1, [Math]::Ceiling($ProbeTimeoutMs / 1000.0))
    & python "$PSScriptRoot/grinder_mdns_probe.py" --expected-mac $ExpectedMac --target-ip $Ip --timeout $timeoutSeconds | Out-Null
    return [pscustomobject]@{
      Success = ($LASTEXITCODE -eq 0)
      LatencyMs = $watch.ElapsedMilliseconds
      Error = if ($LASTEXITCODE -eq 0) { $null } else { "mDNS probe exit code $LASTEXITCODE" }
    }
  } catch {
    return [pscustomobject]@{
      Success = $false
      LatencyMs = $watch.ElapsedMilliseconds
      Error = $_.Exception.Message
    }
  } finally {
    $watch.Stop()
  }
}

while ([DateTime]::UtcNow -lt $deadline) {
  $started = [DateTime]::UtcNow
  $probeGapMs = if ($null -eq $previousStarted) { 0 } else { [int]($started - $previousStarted).TotalMilliseconds }
  $previousStarted = $started

  $tcp = Test-GrinderSession
  $http = Invoke-TasmotaStatus
  $status = $http.Status
  $discovery = $null
  if (($iteration % $DiscoveryEvery) -eq 0) {
    $discovery = Test-GrinderDiscovery
  }

  [pscustomobject]@{
    TimestampUtc = $started.ToString('o')
    ProbeGapMs = $probeGapMs
    TcpSession = $tcp.Success
    TcpLatencyMs = $tcp.LatencyMs
    TcpError = $tcp.Error
    HttpStatus = $http.Success
    HttpLatencyMs = $http.LatencyMs
    HttpError = $http.Error
    Discovery = if ($null -eq $discovery) { $null } else { $discovery.Success }
    DiscoveryLatencyMs = if ($null -eq $discovery) { $null } else { $discovery.LatencyMs }
    DiscoveryError = if ($null -eq $discovery) { $null } else { $discovery.Error }
    PowerOn = $status.Relay.On
    WifiUp = $status.Net.Up
    WifiStatus = $status.Net.WL
    Ip = $status.Net.IP
    Bssid = $status.Net.BSSID
    Rssi = $status.Net.RSSI
    LinkCount = $status.Net.Link
    DisconnectReason = $status.Net.Reason
    RecoveryMs = $status.Net.RecoveryMs
    NetworkGeneration = $status.Net.Gen
    ServerGeneration = $status.TCP.Gen
    NetworkUps = $status.Count.NetUp
    NetworkDowns = $status.Count.NetDn
    LinkChanges = $status.Count.Link
    WifiDisconnectEvents = $status.Count.WiFiDn
    WifiGotIpEvents = $status.Count.GotIP
    Restarts = $status.Count.Restart
    MdnsRefreshes = $status.Count.MdnsRefresh
    MaxMdnsOperationMs = $status.mDNS.MaxMs
    MaxLoopGapMs = $status.Loop.MaxGapMs
    LastWifiEvent = $status.Last.WiFi
    LastWifiEventAgeMs = $status.Last.WiFiAge
    LastNetworkReason = $status.Last.Net
    FreeHeap = $status.Heap.Free
    MinimumHeap = $status.Heap.Min
  } | Export-Csv -LiteralPath $Output -NoTypeInformation -Append

  $iteration++
  $next = $started.AddSeconds($IntervalSeconds)
  $remaining = [int]($next - [DateTime]::UtcNow).TotalMilliseconds
  if ($remaining -gt 0) {
    Start-Sleep -Milliseconds $remaining
  }
}
