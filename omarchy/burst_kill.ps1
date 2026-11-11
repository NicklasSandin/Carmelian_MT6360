$log = "C:\stuff\Carmelian hacks\work\artifacts\probe_serial.log"
"=== burst start $(Get-Date -Format HH:mm:ss) ===" | Tee-Object $log -Append
$com = $null
foreach ($i in 1..60) {
    $devs = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue | Where-Object { $_.Name -match "COM\d+" -and $_.Name -match "Serial" }
    foreach ($e in ($devs | Where-Object { $_.InstanceId -match "VID_1D6B" })) { if ($e.Name -match "(COM(\d+))") { $com = $Matches[1]; break } }
    if (-not $com) { foreach ($e in $devs) { if ($e.Name -match "(COM(\d+))") { $com = $Matches[1]; break } } }
    if ($com) { break }
    Start-Sleep -Milliseconds 500
}
if (-not $com) { "NO PORT" | Tee-Object $log -Append; exit 1 }
$sp = New-Object System.IO.Ports.SerialPort($com, 115200, "None", 8, "One")
$sp.ReadTimeout = 1500; $sp.WriteTimeout = 5000
$sp.DtrEnable = $true; $sp.RtsEnable = $true
$sp.Open()
"opened $com" | Tee-Object $log -Append
Start-Sleep -Milliseconds 800
try { $sp.ReadExisting() } catch {}
# 20-line burst: round-robin readers guarantee the bash gets several
$burst = "pkill -9 -x sh`r" * 20
try { $sp.Write($burst) } catch { "WRITE-BLOCKED" | Tee-Object $log -Append }
Start-Sleep -Seconds 6
try { $t = $sp.ReadExisting() } catch { $t = "" }
if ($t) { $t.TrimEnd() | Tee-Object $log -Append }
# settle then ask the state
try { $sp.Write("`recho BURST-DONE; cat /proc/1/comm; cat /proc/uptime`r") } catch { "WRITE2-BLOCKED" | Tee-Object $log -Append }
Start-Sleep -Seconds 6
try { $t = $sp.ReadExisting() } catch { $t = "" }
if ($t) { $t.TrimEnd() | Tee-Object $log -Append }
$sp.Close()
"=== burst done $(Get-Date -Format HH:mm:ss) ===" | Tee-Object $log -Append
