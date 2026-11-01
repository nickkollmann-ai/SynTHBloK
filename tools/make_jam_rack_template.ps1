# Exports the JAM "Template 4" from Controller Editor's settings as "JAM Template 4 Rack.ncmj"
# for the SynTHBloK-K JAM module. Same MIDI numbers and pages; only the lights change:
#   - Pads, scene buttons and group buttons use Color Mode "Indexed", so the colour the
#     module sends (yellow -> deep dark red) shows on the JAM.
#   - Touch strip pages A..H light in their bank's colour.
#   - LEDs follow MIDI from the module ("For MIDI In"), not the buttons' own presses.
#   - The bottom button row sends CC 112-119 (see below); SELECT is switched on (CC 37).
#   - Knob push sends CC 46 (it sent pad notes).
param(
    [string]$Config = "$env:USERPROFILE\Documents\Native Instruments\Controller Editor\Configuration.ncc",
    [string]$Out = (Join-Path $PSScriptRoot 'JAM Template 4 Rack.ncmj')
)

$text = [IO.File]::ReadAllText($Config)
$m = [regex]::Match($text, '(?s)<midi-map type="MaschineJam" name="Template 4".*?</midi-map>')
if (-not $m.Success) { throw 'No JAM "Template 4" in Controller Editor settings.' }

$doc = New-Object System.Xml.XmlDocument
$doc.PreserveWhitespace = $true
$doc.LoadXml("<?xml version=`"1.0`"?>`n<ni-controller-midi-map version=`"1`">`n  " + $m.Value + "`n</ni-controller-midi-map>`n")
$map = $doc.SelectSingleNode('/ni-controller-midi-map/midi-map')
$map.SetAttribute('name', 'JAM Template 4 Rack')

# Settings-file-only bookkeeping that exported .ncmj files don't carry.
foreach ($n in @($map.SelectNodes('.//last | .//lastExt'))) { [void]$n.ParentNode.RemoveChild($n) }

foreach ($b in @($map.SelectNodes('.//behavior[@onIfDown]'))) { $b.RemoveAttribute('onIfDown') }

$indexed = $map.SelectNodes("scenePages/scene/led/display/unit | controls/led[starts-with(@id,'BtnScene') or starts-with(@id,'BtnGroup')]/display/unit")
foreach ($u in @($indexed)) { $u.SetAttribute('color-mode', '3') }

# Controller Editor colour list: 0 red, 1 orange, 2 light orange, 3 warm yellow, 4 yellow.
$stripColor = @(4, 3, 2, 1, 0, 0, 0, 0)
$pages = @($map.SelectNodes('touchstripPages/page'))
for ($i = 0; $i -lt $pages.Count -and $i -lt 8; $i++) {
    foreach ($u in @($pages[$i].SelectNodes('led/display/unit'))) { $u.SetAttribute('color-on-index', "$($stripColor[$i])") }
}

# Bottom row: Template 4 sends Mackie notes (91-95, 16, 24, 73), which clash with pad notes on
# higher matrix pages, so they become CC 112-119. SOLO, MUTE, GRID and SELECT are switched on.
$bottom = [ordered]@{ BtnPlay = 112; BtnRecord = 113; BtnArrowLeft = 114; BtnArrowRight = 115
                      BtnTempo = 116; BtnGrid = 117; BtnSolo = 118; BtnMute = 119; BtnSelect = 37 }
foreach ($id in $bottom.Keys) {
    $btn = $map.SelectSingleNode("controls/button[@id='$id']")
    $dis = $btn.SelectSingleNode('disabled')
    if ($dis) { [void]$btn.RemoveChild($dis) }
    $old = $btn.SelectSingleNode('note|controller|polyat|mcu-button')
    foreach ($n in @($btn.SelectNodes('channel|off|on'))) { [void]$btn.RemoveChild($n) }
    $after = $old
    foreach ($pair in @(@('controller', $bottom[$id]), @('channel', 0), @('off', 0), @('on', 127))) {
        $el = $doc.CreateElement($pair[0]); $el.InnerText = "$($pair[1])"
        [void]$btn.InsertAfter($el, $after); $after = $el
    }
    [void]$btn.RemoveChild($old)
}

# Knob push sent notes 42-45 (the same as four pads), so it becomes CC 46 on every knob page.
foreach ($push in @($map.SelectNodes("iolevelPages/page/button[@id='PshBrowse']"))) {
    $old = $push.SelectSingleNode('note|controller|polyat')
    $el = $doc.CreateElement('controller'); $el.InnerText = '46'
    [void]$push.ReplaceChild($el, $old)
    $push.SelectSingleNode('behavior').InnerText = 'gate'
}

$doc.Save($Out)
"Wrote $Out ($(@($indexed).Count) lights set to Indexed)"
