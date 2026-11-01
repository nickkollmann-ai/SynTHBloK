# Exports the JAM "Template 1" from Controller Editor's settings as "JAM Template 1 Live.ncmj",
# with the same MIDI numbers, plus two changes so it can drive the JAM_Live12 script:
#   - Matrix pages and touch strip pages are switched off, so the scene buttons (CC 0-7) and
#     group buttons (CC 8-15) send MIDI instead of flipping pages, and the grid stays on
#     Matrix Page 1 (notes 0-63) and the strips on Page A (CC 48-55).
#   - Encoder push sends CC 46 (it sent notes 42-45, the same as four grid pads).
#   - GRID, SOLO, MUTE and SELECT are switched on as CC 117, 118, 119 and 37.
#   - LEDs follow MIDI from Live instead of lighting from the button's own presses.
#   - Grid pads, scene and group buttons use Color Mode "Indexed": Live's value picks the colour.
param(
    [string]$Config = "$env:USERPROFILE\Documents\Native Instruments\Controller Editor\Configuration.ncc",
    [string]$Out = (Join-Path $PSScriptRoot 'JAM Template 1 Live.ncmj')
)

$text = [IO.File]::ReadAllText($Config)
$m = [regex]::Match($text, '(?s)<controller type="Maschine Jam - 1">.*?(<midi-map type="MaschineJam" name="Template 1".*?</midi-map>)')
if (-not $m.Success) { throw 'No "Template 1" for Maschine Jam - 1 in Controller Editor settings.' }

$doc = New-Object System.Xml.XmlDocument
$doc.PreserveWhitespace = $true
$doc.LoadXml("<?xml version=`"1.0`"?>`n<ni-controller-midi-map version=`"1`">`n  " + $m.Groups[1].Value + "`n</ni-controller-midi-map>`n")
$map = $doc.SelectSingleNode('/ni-controller-midi-map/midi-map')
$map.SetAttribute('name', 'JAM Template 1 Live')

# Settings-file-only bookkeeping that exported .ncmj files don't carry.
foreach ($n in @($map.SelectNodes('.//last | .//lastExt'))) { [void]$n.ParentNode.RemoveChild($n) }

$first = $map.FirstChild
foreach ($flag in 'disableTouchstripPages', 'disableMatrixButtonPages') {
    if (-not $map.SelectSingleNode($flag)) {
        [void]$map.InsertBefore($doc.CreateElement($flag), $first)
        [void]$map.InsertBefore($doc.CreateWhitespace("`n      "), $first)
        $first = $map.FirstChild
    }
}

# "LED On: For MIDI Out" (onIfDown) lights a button from its own presses. Drop it so every
# LED shows what Live sends back ("For MIDI In").
foreach ($b in @($map.SelectNodes(".//behavior[@onIfDown]"))) { $b.RemoveAttribute('onIfDown') }

# Grid pads, scene and group buttons: Color Mode "Indexed" (3), so the value Live sends picks the colour.
$indexed = $map.SelectNodes("scenePages/scene/led/display/unit | controls/led[starts-with(@id,'BtnScene') or starts-with(@id,'BtnGroup')]/display/unit")
foreach ($u in @($indexed)) { $u.SetAttribute('color-mode', '3') }

# Buttons Template 1 switches off because their Mackie notes clash with pad notes get free CCs
# instead (the same numbers as the Rack template); everything else keeps its number.
# The knob push sent pad notes 42-45; it becomes CC 46 on every knob page.
function Set-CC($btn, [int]$cc) {
    $dis = $btn.SelectSingleNode('disabled')
    if ($dis) { [void]$btn.RemoveChild($dis) }
    $old = $btn.SelectSingleNode('note|controller|polyat|mcu-button')
    foreach ($n in @($btn.SelectNodes('channel|off|on'))) { [void]$btn.RemoveChild($n) }
    $after = $old
    foreach ($pair in @(@('controller', $cc), @('channel', 0), @('off', 0), @('on', 127))) {
        $el = $doc.CreateElement($pair[0]); $el.InnerText = "$($pair[1])"
        [void]$btn.InsertAfter($el, $after); $after = $el
    }
    [void]$btn.RemoveChild($old)
    $btn.SelectSingleNode('behavior').InnerText = 'gate'
}
$freed = [ordered]@{ BtnGrid = 117; BtnSolo = 118; BtnMute = 119; BtnSelect = 37 }
foreach ($id in $freed.Keys) { Set-CC $map.SelectSingleNode("controls/button[@id='$id']") $freed[$id] }
foreach ($push in @($map.SelectNodes("iolevelPages/page/button[@id='PshBrowse']"))) { Set-CC $push 46 }

$doc.Save($Out)
"Wrote $Out"
