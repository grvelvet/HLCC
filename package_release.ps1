$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$stage = Join-Path $root "release\staging"
$zipPath = Join-Path $root "release\HLCC-win32.zip"

if (!(Test-Path (Join-Path $root "version.dll"))) {
    throw "version.dll not found. Run src\build.bat first."
}

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }

New-Item -ItemType Directory -Force -Path $stage, "$stage\valve", "$stage\gearbox", "$stage\bshift" | Out-Null

Copy-Item -Force (Join-Path $root "version.dll"), (Join-Path $root "subtitles.ini"), (Join-Path $root "README.txt") $stage
Copy-Item -Force (Join-Path $root "valve\subtitles_ru.tsv"), (Join-Path $root "valve\subtitles_en.tsv"), (Join-Path $root "valve\titles_ru.tsv") "$stage\valve"
Copy-Item -Force (Join-Path $root "gearbox\subtitles_ru.tsv"), (Join-Path $root "gearbox\subtitles_en.tsv"), (Join-Path $root "gearbox\titles_ru.tsv") "$stage\gearbox"
Copy-Item -Force (Join-Path $root "bshift\subtitles_ru.tsv"), (Join-Path $root "bshift\subtitles_en.tsv"), (Join-Path $root "bshift\titles_ru.tsv") "$stage\bshift"

Compress-Archive -Path "$stage\*" -DestinationPath $zipPath -CompressionLevel Optimal
Remove-Item -Recurse -Force $stage

Get-Item $zipPath | Select-Object FullName, Length
