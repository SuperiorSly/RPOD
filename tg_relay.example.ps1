# ============================================================================
#  tg_relay.example.ps1  -  PC-side Telegram relay for RPOD (template)
#
#  Copy this file to tg_relay.ps1 (it is gitignored), paste YOUR bot token
#  into $Token, and run it on any PC on the same LAN as the iPod.
#
#      iPod --> plain LAN HTTP (tiny) --> this script --> HTTPS --> Telegram
#
#  The ESP32 cannot run WiFi + TLS together (RAM fragmentation kills the
#  HTTPS handshake), so this PC script bridges the gap. It listens on:
#
#      TCP 8461: the API relay   (getUpdates / sendMessage / getFile / file/*)
#      UDP 8461: discovery       (answers the device's "IPIPOD1?" broadcast,
#                                 so the PC's IP may change freely)
#
#  Autostart tip: add to HKCU Run key
#    powershell.exe -ExecutionPolicy Bypass -WindowStyle Minimized -File <path>
# ============================================================================
$ErrorActionPreference = 'Continue'
$Token = 'PASTE-YOUR-BOT-TOKEN-HERE'   # stays on the PC - never sent to the device
$Port  = 8461
$Dir   = Join-Path $env:TEMP 'ipod_relay'
$Tmp   = Join-Path $Dir 'relay_body.tmp'
$CodeF = Join-Path $Dir 'relay_code.tmp'
$L     = Join-Path $Dir 'relay.log'
New-Item -ItemType Directory -Force -Path $Dir | Out-Null

function Log($m) { Add-Content -Path $L -Value ("<{0}> {1}" -f (Get-Date -Format HH:mm:ss), $m) -ErrorAction SilentlyContinue }

$listener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Any, $Port)
try { $listener.Start() } catch { Log "LISTENER FAIL: $_"; exit 1 }
$udp2 = New-Object System.Net.Sockets.UdpClient($Port)        # same number, UDP flavour
Log "relay listening on TCP+UDP port $Port"

function Respond($s, $code, $ctype, [byte[]]$body) {
  $hdr = "HTTP/1.1 $code`r`nContent-Type: $ctype`r`nContent-Length: $($body.Length)`r`nConnection: close`r`n`r`n"
  $hb  = [Text.Encoding]::ASCII.GetBytes($hdr)
  $s.Write($hb, 0, $hb.Length)
  if ($body.Length -gt 0) { $s.Write($body, 0, $body.Length) }
  $s.Flush()
}

while ($true) {
  $client = $null
  try {
    # ---------------- UDP discovery: "where is the PC?" ----------------
    while ($udp2.Available -gt 0) {
      try {
        $epAny = [Net.IPEndPoint]::new([Net.IPAddress]::Any, 0)
        $req   = [Text.Encoding]::ASCII.GetString($udp2.Receive([ref]$epAny))
        if ($req -match 'IPIPOD1') {
          $b = [Text.Encoding]::ASCII.GetBytes('IPIPOD1!')
          [void]$udp2.Send($b, $b.Length, $epAny.Address, $epAny.Port)
          Log "DISC answered -> $($epAny.Address):$($epAny.Port)"
        }
      } catch { Log "UDP ERR: $_"; break }
    }

    # ---------------- TCP relay request --------------------------------
    if (-not $listener.Pending()) { Start-Sleep -Milliseconds 100; continue }
    $client = $listener.AcceptTcpClient()
    $client.ReceiveTimeout = 80000
    $client.SendTimeout    = 600000
    $st = $client.GetStream()
    $buf = New-Object byte[] 16384
    $sb = New-Object Text.StringBuilder
    while ($true) {
      $n = $st.Read($buf, 0, 16384)
      if ($n -le 0) { break }
      [void]$sb.Append([Text.Encoding]::ASCII.GetString($buf, 0, $n))
      if ($sb.ToString().Contains("`r`n`r`n")) { break }
    }
    $req  = $sb.ToString()
    $line = ($req -split "`r`n")[0]
    if ($line -notmatch '^GET\s+(\S+)\s') {
      Respond $st '400 Bad Request' 'text/plain' ([Text.Encoding]::ASCII.GetBytes('nope'))
      $st.Close(); $client.Close(); $client = $null
      continue
    }
    $path = $Matches[1]                       # kept EXACTLY as sent (already url-encoded)
    Log "REQ $line"

    if ($path -match '^/file/(.+)$') {        # ---------- MP3 delivery ----------
      $turl = "https://api.telegram.org/file/bot$Token/$($Matches[1])"
      try {
        # STREAM THROUGH: forward Telegram's response headers immediately,
        # then pump the bytes live. The device gets headers within ~1 s.
        $wreq = [Net.HttpWebRequest]::Create($turl)
        $wreq.Proxy = $null
        $wreq.KeepAlive = $false
        $wreq.Timeout = 100000                # ms until response headers arrive
        $wreq.ReadWriteTimeout = 100000       # ms between stream reads
        $wresp = $wreq.GetResponse()
        $clen = $wresp.ContentLength
        Log "FILE: TG status $([int]$wresp.StatusCode), length $clen"
        if ($clen -ge 0) {
          $hdr = "HTTP/1.1 200 OK`r`nContent-Type: application/octet-stream`r`nContent-Length: $clen`r`nConnection: close`r`n`r`n"
          $hb = [Text.Encoding]::ASCII.GetBytes($hdr)
          $st.Write($hb, 0, $hb.Length)
          $rs = $wresp.GetResponseStream()
          $cbuf = New-Object byte[] 16384
          $sent = 0
          while (($n = $rs.Read($cbuf, 0, $cbuf.Length)) -gt 0) { $st.Write($cbuf, 0, $n); $sent += $n }
          $st.Flush()
          Log "FILE STREAMED: $sent of $clen bytes"
          $rs.Close(); $wresp.Close()
        } else {                              # no Content-Length advertised: buffer then send
          $ms = New-Object IO.MemoryStream
          $wresp.GetResponseStream().CopyTo($ms)
          $data = $ms.ToArray(); $ms.Close(); $wresp.Close()
          Respond $st '200 OK' 'application/octet-stream' $data
          Log ("FILE buffered: " + $data.Length + " bytes delivered")
        }
      } catch {
        Respond $st '200 OK' 'application/json' ([Text.Encoding]::ASCII.GetBytes('{"ok":false}'))
        Log "FILE FAIL: $_"
      }
    } else {                                  # ---------- API pass-through ------
      $turl = "https://api.telegram.org/bot$Token$path"
      & curl.exe -s --noproxy '*' --connect-timeout 10 --max-time 40 -o $Tmp -w "%{http_code}" $turl > $CodeF 2>$null
      $code = '000'; if ((Test-Path $CodeF) -and (Get-Item $CodeF).Length -gt 0) { $code = (Get-Content $CodeF -Raw).Trim() }
      if ((Test-Path $Tmp) -and ($code -ne '000')) {
        $bytes = [IO.File]::ReadAllBytes($Tmp)
        $ctype = if ($code -eq '200') { 'application/json' } else { 'text/plain' }
        Respond $st "$code" $ctype $bytes
        Log ("FWD -> $code (" + $bytes.Length + "B)")
      } else {
        Respond $st '502 Bad Gateway' 'text/plain' ([Text.Encoding]::ASCII.GetBytes('relay could not reach Telegram'))
        Log "CURL FAIL $code"
      }
    }
    $st.Close()
  } catch { Log "LOOP ERR: $_" }
  if ($client) { $client.Close(); $client = $null }
}
