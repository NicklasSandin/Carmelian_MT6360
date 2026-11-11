param(
    [string[]]$Lines = @(),
    [int]$SettleMs = 2500
)
$log = "C:\stuff\Carmelian hacks\work\artifacts\probe_serial.log"
"=== burst_cmd start $(Get-Date -Format HH:mm:ss) ===" | Tee-Object $log -Append
$com = $null
foreach ($i in 1..40) {
    $devs = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue | Where-Object { $_.Name -match "COM\d+" -and $_.Name -match "Serial" }
    foreach ($e in ($devs | Where-Object { $_.InstanceId -match "VID_1D6B" })) { if ($e.Name -match "(COM(\d+))") { $com = $Matches[1]; break } }
    if (-not $com) { foreach ($e in $devs) { if ($e.Name -match "(COM(\d+))") { $com = $Matches[1]; break } } }
    if ($com) { break }
    Start-Sleep -Milliseconds 500
}
if (-not $com) { "NO PORT" | Tee-Object $log -Append; exit 1 }
$sp = New-Object System.IO.Ports.SerialPort($com, 115200, "None", 8, "One")
$sp.ReadTimeout = 1200; $sp.WriteTimeout = 4000
$sp.DtrEnable = $true; $sp.RtsEnable = $true
$sp.Open()
"opened $com" | Tee-Object $log -Append
Start-Sleep -Milliseconds 700
try { $sp.ReadExisting() } catch {}
foreach ($c in $Lines) {
    ">>> $c" | Tee-Object $log -Append
    try { $sp.Write("$c`r") } catch { "WRITE-BLOCKED" | Tee-Object $log -Append }
    Start-Sleep -Milliseconds $SettleMs
    try { $t = $sp.ReadExisting() } catch { $t = "" }
    if ($t) { $t.TrimEnd() | Tee-Object $log -Append }
}
$sp.Close()
"=== burst_cmd done $(Get-Date -Format HH:mm:ss) ===" | Tee-Object $log -Append
