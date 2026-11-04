# Records the workbench window with OBS through OBS's WebSocket server (Tools > WebSocket Server Settings).
#   setup   make a "Workbench" scene that captures the Firefox window showing the workbench, fitted to the canvas
#   start   switch to that scene and start recording (does nothing if OBS is already recording)
#   stop    stop recording and print where the file went; -Delay waits that many seconds first
#   status  say whether OBS is recording
# -Mine on start leaves a marker when this script started the recording; -Mine on stop only stops a recording
# that has the marker, so a build never stops a recording someone started by hand.
# The password is read from OBS's own config file, so none is kept here. When OBS is not running or its
# server is off, the script says so and exits 0: build_and_reload.ps1 calls it and must not fail over it.
param(
    [Parameter(Position = 0)][ValidateSet('setup', 'start', 'stop', 'status')][string]$Action = 'status',
    [int]$Delay = 0,
    [string]$Scene = 'Workbench',
    [switch]$Mine,
    # OBS's window id is title:class:exe. The title's dash is an em dash, built here so the file stays ASCII.
    [string]$Window = "SynTHBloK Workbench $([char]0x2014) Mozilla Firefox:MozillaWindowClass:firefox.exe"
)

$marker = Join-Path $env:TEMP 'synthblok_obs_recording'
if ($Action -eq 'stop' -and $Mine -and -not (Test-Path $marker)) { exit 0 }
$cfgPath = Join-Path $env:APPDATA 'obs-studio\plugin_config\obs-websocket\config.json'
$ct = [Threading.CancellationToken]::None

function Send($ws, $obj) {
    $bytes = [Text.Encoding]::UTF8.GetBytes(($obj | ConvertTo-Json -Depth 10 -Compress))
    $ws.SendAsync((New-Object ArraySegment[byte] -ArgumentList (, $bytes)), 'Text', $true, $ct).Wait() | Out-Null
}

function Receive($ws) {
    $buf = New-Object byte[] 65536
    $ms = New-Object IO.MemoryStream
    do {
        $r = $ws.ReceiveAsync((New-Object ArraySegment[byte] -ArgumentList (, $buf)), $ct).Result
        $ms.Write($buf, 0, $r.Count)
    } until ($r.EndOfMessage)
    [Text.Encoding]::UTF8.GetString($ms.ToArray()) | ConvertFrom-Json
}

function Sha64([string]$s) {
    $sha = [Security.Cryptography.SHA256]::Create()
    [Convert]::ToBase64String($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($s)))
}

$script:reqId = 0
function Request($ws, [string]$type, $data = $null) {
    $script:reqId++
    $d = @{ requestType = $type; requestId = "$script:reqId" }
    if ($null -ne $data) { $d.requestData = $data }
    Send $ws @{ op = 6; d = $d }
    while ($true) {
        $m = Receive $ws
        if ($m.op -eq 7 -and $m.d.requestId -eq "$script:reqId") { return $m.d }   # skip events (op 5)
    }
}

function Connect {
    if (-not (Get-Process obs64 -ErrorAction SilentlyContinue)) { Write-Host 'OBS: not running, not recording.'; return $null }
    $cfg = Get-Content $cfgPath -Raw -ErrorAction SilentlyContinue | ConvertFrom-Json
    if (-not $cfg -or -not $cfg.server_enabled) { Write-Host 'OBS: WebSocket server is off (Tools > WebSocket Server Settings), not recording.'; return $null }
    $ws = New-Object Net.WebSockets.ClientWebSocket
    try {
        if (-not $ws.ConnectAsync([uri]"ws://127.0.0.1:$($cfg.server_port)", $ct).Wait(3000)) { throw 'timed out' }
    } catch { Write-Host "OBS: could not connect on port $($cfg.server_port), not recording."; return $null }
    $hello = Receive $ws
    $identify = @{ rpcVersion = 1; eventSubscriptions = 0 }
    if ($hello.d.authentication) {
        $secret = Sha64 ($cfg.server_password + $hello.d.authentication.salt)
        $identify.authentication = Sha64 ($secret + $hello.d.authentication.challenge)
    }
    Send $ws @{ op = 1; d = $identify }
    if ((Receive $ws).op -ne 2) { Write-Host 'OBS: the server refused the login, not recording.'; return $null }
    return $ws
}

if ($Delay -gt 0) { Start-Sleep -Seconds $Delay }
$ws = Connect
if (-not $ws) { exit 0 }

switch ($Action) {
    'setup' {
        $scenes = (Request $ws 'GetSceneList').responseData.scenes.sceneName
        if ($scenes -notcontains $Scene) { Request $ws 'CreateScene' @{ sceneName = $Scene } | Out-Null }
        $src = "$Scene window"
        $made = Request $ws 'CreateInput' @{
            sceneName = $Scene; inputName = $src; inputKind = 'window_capture'
            inputSettings = @{ window = $Window; method = 2; cursor = $true }   # method 2 = Windows Graphics Capture
        }
        if (-not $made.requestStatus.result) {
            # Already there from an earlier setup: point it at the window again.
            Request $ws 'SetInputSettings' @{ inputName = $src; inputSettings = @{ window = $Window; method = 2; cursor = $true } } | Out-Null
        }
        $video = (Request $ws 'GetVideoSettings').responseData
        $item = (Request $ws 'GetSceneItemId' @{ sceneName = $Scene; sourceName = $src }).responseData.sceneItemId
        Request $ws 'SetSceneItemTransform' @{ sceneName = $Scene; sceneItemId = $item; sceneItemTransform = @{
            positionX = 0; positionY = 0; boundsType = 'OBS_BOUNDS_SCALE_INNER'
            boundsWidth = [double]$video.baseWidth; boundsHeight = [double]$video.baseHeight
        } } | Out-Null
        Request $ws 'SetCurrentProgramScene' @{ sceneName = $Scene } | Out-Null
        Write-Host "OBS: scene '$Scene' captures '$($Window.Split(':')[0])' at $($video.baseWidth)x$($video.baseHeight)."
    }
    'start' {
        if ((Request $ws 'GetRecordStatus').responseData.outputActive) { Write-Host 'OBS: already recording.'; break }
        $scenes = (Request $ws 'GetSceneList').responseData.scenes.sceneName
        if ($scenes -contains $Scene) { Request $ws 'SetCurrentProgramScene' @{ sceneName = $Scene } | Out-Null }
        $r = Request $ws 'StartRecord'
        if ($r.requestStatus.result) {
            if ($Mine) { Set-Content $marker '' }
            Write-Host 'OBS: recording the workbench.'
        } else { Write-Host "OBS: could not start ($($r.requestStatus.comment))." }
    }
    'stop' {
        $r = Request $ws 'StopRecord'
        Remove-Item $marker -ErrorAction SilentlyContinue
        if ($r.requestStatus.result) { Write-Host "OBS: saved $($r.responseData.outputPath)" } else { Write-Host 'OBS: was not recording.' }
    }
    'status' {
        $s = (Request $ws 'GetRecordStatus').responseData
        if ($s.outputActive) { Write-Host "OBS: recording, $($s.outputTimecode)." } else { Write-Host 'OBS: connected, not recording.' }
    }
}
$ws.CloseAsync('NormalClosure', '', $ct).Wait(1000) | Out-Null
