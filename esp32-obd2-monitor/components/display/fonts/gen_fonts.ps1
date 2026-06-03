$env:Path = "C:\Program Files\nodejs;" + $env:Path
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$conv = Join-Path $dir "node_modules\lv_font_conv\lv_font_conv.js"
$ttf = Join-Path $dir "Montserrat-Medium.ttf"
$fa = Join-Path $dir "fa-solid-900.ttf"

if (-not (Test-Path $conv)) {
    Set-Location $dir
    npm install lv_font_conv
    $conv = Join-Path $dir "node_modules\lv_font_conv\lv_font_conv.js"
}

if (-not (Test-Path $fa)) {
    Invoke-WebRequest -Uri "https://github.com/FortAwesome/Font-Awesome/raw/6.x/webfonts/fa-solid-900.ttf" `
        -OutFile $fa -UseBasicParsing
}

$uiRange = "0x20-0x7E,0xA0-0x17F"
$iconRange = "61451,61459,61473,61523,61671,61931"

foreach ($sz in @(12, 14, 16, 20)) {
    & node $conv --font $ttf --size $sz --bpp 4 --format lvgl --no-compress `
        -o (Join-Path $dir "font_ui_$sz.c") -r $uiRange --lv-font-name "font_ui_$sz"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

foreach ($pair in @(@(24, "font_icons_24"), @(28, "font_icons_28"))) {
    & node $conv --font $fa --size $pair[0] --bpp 4 --format lvgl --no-compress `
        -o (Join-Path $dir ($pair[1] + ".c")) -r $iconRange --lv-font-name $pair[1]
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "OK: check font_ui_14.c contains U+0041"
