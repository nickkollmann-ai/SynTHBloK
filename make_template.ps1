# Builds "JAM Live 12.ncmj" from NI's own "Ableton Live 10.ncmj" Maschine JAM template.
# Every control gets ONE fixed MIDI number on MIDI channel 1, identical on every page,
# so the JAM_Live12 remote script always knows which physical control was touched.
#
#   Matrix pads    notes 22-85   (row 1 = 22..29, row 8 = 78..85)
#   Scene 1-8      notes 0-7     (top row buttons)
#   Group A-H      notes 8-15    (buttons under the grid)
#   Strips A-H     CC 8-15       (value + LED bar), touch = CC 20-27
#   Encoder        CC 86 turn (relative), CC 87 push, CC 88 touch
#   Level meters   CC 38 / 39
#   Footswitch     CC 114 tip / CC 115 ring
#   Other buttons  keep NI's CCs (Play 108, Rec 109, Tempo 110, Solo 111, Mute 112, ...)
param(
    [string]$Source = "$env:USERPROFILE\Desktop\Ableton Live 10.ncmj",
    [string]$Out = (Join-Path $PSScriptRoot 'JAM Live 12.ncmj')
)

$doc = New-Object System.Xml.XmlDocument
$doc.PreserveWhitespace = $true
$doc.Load($Source)
$map = $doc.SelectSingleNode('/ni-controller-midi-map/midi-map')
$map.SetAttribute('name', 'JAM Live 12')

# Our script, not NI's, drives Live: drop NI's Live-script handshake and JAM-side transport.
$live = $map.SelectSingleNode('liveScriptMode')
if ($live) { [void]$map.RemoveChild($live) }
$map.SelectSingleNode('handleTransport').InnerText = '0'

# Replace whichever number element a control uses (note/controller/polyat) with $tag=$num, channel 0.
function Set-Midi($el, [string]$tag, [int]$num) {
    if (-not $el) { return }
    $dis = $el.SelectSingleNode('disabled')
    if ($dis) { [void]$el.RemoveChild($dis) }
    $old = $el.SelectSingleNode('note|controller|polyat')
    $new = $doc.CreateElement($tag)
    $new.InnerText = "$num"
    if ($tag -eq 'controller' -and $old -and $old.Name -eq 'controller' -and $old.HasAttribute('mode')) {
        $new.SetAttribute('mode', $old.GetAttribute('mode'))
    }
    [void]$el.ReplaceChild($new, $old)
    $el.SelectSingleNode('channel').InnerText = '0'
}
function Get-Ctl($parent, [string]$id) { $parent.SelectSingleNode("*[@id='$id']") }

$controls = $map.SelectSingleNode('controls')
$letters = 'ABCDEFGH'.ToCharArray()

for ($i = 0; $i -lt 8; $i++) {
    $L = $letters[$i]
    foreach ($id in "BtnScene$($i + 1)", "BtnScene$($i + 1)IDX") { Set-Midi (Get-Ctl $controls $id) 'note' $i }
    foreach ($id in "BtnGroup$L", "BtnGroup${L}IDX") { Set-Midi (Get-Ctl $controls $id) 'note' (8 + $i) }
}
Set-Midi (Get-Ctl $controls 'FswTip') 'controller' 114
Set-Midi (Get-Ctl $controls 'FswRing') 'controller' 115

# Matrix: all 8 pages identical. Column letter A-H, row digit 1-8 (row 1 = top).
foreach ($page in $map.SelectNodes('scenePages/scene')) {
    for ($c = 0; $c -lt 8; $c++) {
        for ($r = 0; $r -lt 8; $r++) {
            $id = "Btn$($letters[$c])$($r + 1)"
            $n = 22 + 8 * $r + $c
            Set-Midi (Get-Ctl $page $id) 'note' $n
            Set-Midi (Get-Ctl $page "${id}IDX") 'note' $n
            # NI ships these as "toggle"; clip launching needs press=127 / release=0.
            (Get-Ctl $page $id).SelectSingleNode('behavior').InnerText = 'gate'
        }
    }
}

# Touch strips: all pages identical.
foreach ($page in $map.SelectNodes('touchstripPages/page')) {
    for ($i = 0; $i -lt 8; $i++) {
        $L = $letters[$i]
        Set-Midi (Get-Ctl $page "Tst$L") 'controller' (8 + $i)
        Set-Midi (Get-Ctl $page "Tst${L}IDX") 'controller' (8 + $i)
        Set-Midi (Get-Ctl $page "CapTst$L") 'controller' (20 + $i)
    }
}

# Encoder + level meters: every level page behaves like NI's "Master" page.
foreach ($page in $map.SelectNodes('iolevelPages/page')) {
    $enc = Get-Ctl $page 'EncBrowse'
    $encNum = $enc.SelectSingleNode('note|controller|polyat')
    if ($encNum.Name -ne 'controller' -or -not $encNum.HasAttribute('mode')) {
        $tmp = $doc.CreateElement('controller'); $tmp.SetAttribute('mode', 'comp'); $tmp.InnerText = '0'
        [void]$enc.ReplaceChild($tmp, $encNum)
    }
    Set-Midi $enc 'controller' 86
    Set-Midi (Get-Ctl $page 'PshBrowse') 'controller' 87
    Set-Midi (Get-Ctl $page 'CapBrowse') 'controller' 88
    Set-Midi (Get-Ctl $page 'MetLevel1') 'controller' 38
    Set-Midi (Get-Ctl $page 'MetLevel2') 'controller' 39
    foreach ($id in 'PshBrowse', 'CapBrowse') {
        $b = $page.SelectSingleNode("*[@id='$id']/behavior")
        if ($b) { $b.InnerText = 'gate' }
    }
}

# --- Check: no two INPUT controls on the same page set share a MIDI message. ---
function Get-Key($el) {
    $num = $el.SelectSingleNode('note|controller|polyat')
    if (-not $num -or $el.SelectSingleNode('disabled')) { return $null }
    "$($num.Name):$($num.InnerText)/ch$($el.SelectSingleNode('channel').InnerText)"
}
$inputs = 'button|knob|wheel|footswitch'
$global = @($controls.SelectNodes($inputs))
$sections = @(
    @{ name = 'matrix'; nodes = $map.SelectSingleNode('scenePages/scene').SelectNodes($inputs) },
    @{ name = 'strips'; nodes = $map.SelectSingleNode('touchstripPages/page').SelectNodes($inputs) },
    @{ name = 'level'; nodes = $map.SelectSingleNode('iolevelPages/page').SelectNodes($inputs) }
)
$all = @{}
$clash = @()
foreach ($el in $global + ($sections | ForEach-Object { @($_.nodes) })) {
    $k = Get-Key $el
    if (-not $k) { continue }
    if ($all.ContainsKey($k)) { $clash += "$k used by $($all[$k]) and $($el.GetAttribute('id'))" }
    else { $all[$k] = $el.GetAttribute('id') }
}
if ($clash) { $clash | ForEach-Object { Write-Warning $_ }; throw 'MIDI clashes found, template not written.' }

$doc.Save($Out)
"Wrote $Out ($($all.Count) unique input controls, no clashes)"
