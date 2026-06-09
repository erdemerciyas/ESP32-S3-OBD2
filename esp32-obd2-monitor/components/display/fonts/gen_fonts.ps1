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

$fab = Join-Path $dir "fa-brands-400.ttf"
if (-not (Test-Path $fab)) {
    Invoke-WebRequest -Uri "https://github.com/FortAwesome/Font-Awesome/raw/6.x/webfonts/fa-brands-400.ttf" `
        -OutFile $fab -UseBasicParsing
}

$uiRange = "0x20-0x7E,0xA0-0x17F,0x2014,0x2192"
$iconSolid = "61451,61453,61459,61473,61523,61671,61931,62189"
$iconBrand = "62099"

foreach ($sz in @(12, 14, 16, 20)) {
    & node $conv --font $ttf --size $sz --bpp 4 --format lvgl --no-compress `
        -o (Join-Path $dir "font_ui_$sz.c") -r $uiRange --lv-font-name "font_ui_$sz"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$gaugeRange = "0x30-0x39,0x2E,0x2D"
foreach ($pair in @(@(96, "font_gauge_96"), @(128, "font_gauge_128"))) {
    & node $conv --font $ttf --size $pair[0] --bpp 4 --format lvgl --no-compress `
        -o (Join-Path $dir ($pair[1] + ".c")) -r $gaugeRange --lv-font-name $pair[1]
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

foreach ($pair in @(@(24, "font_icons_24"), @(28, "font_icons_28"))) {
    & node $conv --font $fa -r $iconSolid --font $fab -r $iconBrand --size $pair[0] --bpp 4 --format lvgl --no-compress `
        -o (Join-Path $dir ($pair[1] + ".c")) --lv-font-name $pair[1]
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "OK: check font_ui_14.c contains U+0041"
