param(
    [int]$WaitSeconds = 180,
    [string[]]$Cmds = @(),
    [string]$CmdFile = ""
)
$log = "C:\stuff\Carmelian hacks\work\artifacts\probe_serial.log"
"=== probe_serial start $(Get-Date -Format HH:mm:ss) ===" | Tee-Object $log -Append

if ($CmdFile -and (Test-Path $CmdFile)) {
    $Cmds = Get-Content $CmdFile | Where-Object { $_ -and ($_ -notmatch '^\s*#') }
}

if (-not $Cmds -or $Cmds.Count -eq 0) {
    $Cmds = @(
        "id; uname -a; cat /proc/uptime",
        "ifconfig -a 2>/dev/null || ifconfig",
        "ls /sys/class/net",
        "ps",
        "cat /sys/class/udc/*/state 2>/dev/null"
    )
}

# 1. wait for a USB serial COM port (the probe's ACM function)
$com = $null
$deadline = (Get-Date).AddSeconds($WaitSeconds)
while ((Get-Date) -lt $deadline -and -not $com) {
    $devs = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue | Where-Object { $_.Name -match "COM\d+" }
    # prefer the OMARCHY gadget (VID_1D6B) - the MTK bootrom ports (VID_0E8D)
    # are dead once the kernel runs, and there can be several serial devices
    foreach ($e in ($devs | Where-Object { $_.InstanceId -match "VID_1D6B" })) {
        if ($e.Name -match "USB Serial|UART|Communications|Serial Device") {
            if ($e.Name -match "(COM(\d+))") { $com = $Matches[1]; break }
        }
    }
    if (-not $com) {
        foreach ($e in $devs) {
            if ($e.Name -match "USB Serial|UART|Communications|Serial Device") {
                if ($e.Name -match "(COM(\d+))") { $com = $Matches[1]; break }
            }
        }
    }
    if (-not $com) { Start-Sleep -Milliseconds 500 }
}
if (-not $com) { "NO COM PORT FOUND within ${WaitSeconds}s" | Tee-Object $log -Append; exit 1 }
"COM port: $com at $(Get-Date -Format HH:mm:ss)" | Tee-Object $log -Append

$sp = New-Object System.IO.Ports.SerialPort($com, 115200, "None", 8, "One")
$sp.ReadTimeout = 1500
$sp.WriteTimeout = 3000
$sp.DtrEnable = $true
$sp.RtsEnable = $true
try { $sp.Open() } catch { "open failed: $_" | Tee-Object $log -Append; exit 1 }
"opened $com" | Tee-Object $log -Append

function Drain($ms) {
    Start-Sleep -Milliseconds $ms
    try { $t = $sp.ReadExisting() } catch { $t = "" }
    if ($t) { $t.TrimEnd() | Tee-Object $log -Append }
}

# wake the shell (getty may need DTR settle + newline)
Drain 1200 | Out-Null
try { $sp.Write("`r`n") } catch { "WRITE-BLOCKED" | Tee-Object $log -Append }
Drain 1500 | Out-Null
try { $sp.Write("`r`n") } catch { "WRITE-BLOCKED" | Tee-Object $log -Append }
Drain 1500 | Out-Null

foreach ($c in $Cmds) {
    ">>> $c" | Tee-Object $log -Append
    # char-by-char writes: the MTK ACM drops bytes between fast/burst writes
    foreach ($ch in ("$c; echo __EOC__").ToCharArray()) {
        try { $sp.Write([string]$ch) } catch { "WRITE-BLOCKED" | Tee-Object $log -Append }
        Start-Sleep -Milliseconds 12
    }
    try { $sp.Write("`r") } catch { "WRITE-BLOCKED" | Tee-Object $log -Append }
    $buf = ""
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt 10000) {
        Start-Sleep -Milliseconds 300
        try { $buf += $sp.ReadExisting() } catch {}
        if ($buf -match "__EOC__") { break }
    }
    # the ECHO of the typed marker arrives before the executed marker's
    # output - drain for another 2.5 s so real results land in the buffer
    Start-Sleep -Milliseconds 2500
    try { $buf += $sp.ReadExisting() } catch {}
    if ($buf.Trim()) { $buf.TrimEnd() | Tee-Object $log -Append }
    Start-Sleep -Milliseconds 1200
}

$sp.Close()
"=== done $(Get-Date -Format HH:mm:ss) ===" | Tee-Object $log -Append
