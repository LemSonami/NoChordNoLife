param([string]$Compiler='g++',[string]$ResourceCompiler='windres',[string]$Output='NoChordNoLife.exe')
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Push-Location $projectRoot
try {
    $outputPath=[IO.Path]::GetFullPath((Join-Path $projectRoot $Output))
    foreach($process in Get-Process NoChordNoLife -ErrorAction SilentlyContinue) {
        if($process.Path -eq $outputPath) { throw '请先关闭正在运行的应用，再重新构建。' }
    }
    New-Item -ItemType Directory -Force build | Out-Null
    & (Join-Path $PSScriptRoot 'create_icon.ps1') -Source 'assets/icon.png' -Output 'assets/icon.ico'
    $files=Get-ChildItem assets -Recurse -File | Where-Object {
        $_.Extension.ToLowerInvariant() -in @('.png','.gif','.ani','.ttf') -and $_.FullName -notmatch '[\\/]source[\\/]'
    } | Sort-Object FullName
    $index=@('#pragma once','namespace ncnl {','struct AssetEntry { const wchar_t* path; unsigned id; };','static const AssetEntry ASSET_INDEX[]={')
    $resources=@('1 ICON "assets/icon.ico"')
    $id=1000
    foreach($file in $files) {
        $relative=$file.FullName.Substring((Join-Path $projectRoot 'assets').Length+1).Replace('\','/')
        $index+='    {L"'+$relative+'",'+$id+'},'
        $resources+=([string]$id+' RCDATA "assets/'+$relative+'"')
        ++$id
    }
    $index+=@('};','}')
    $encoding=New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText((Join-Path $projectRoot 'src/resources/asset_index.hpp'),($index -join "`n")+"`n",$encoding)
    [IO.File]::WriteAllText((Join-Path $projectRoot 'src/resources/assets.rc'),($resources -join "`n")+"`n",$encoding)
    & $ResourceCompiler -I . --codepage=65001 src/resources/assets.rc -O coff -o build/assets.o
    if($LASTEXITCODE -ne 0) { throw '资源打包失败。' }
    $sources=@('src/algorithms/chord_progression.cpp','src/algorithms/chord_emotion.cpp','src/generation/chord_generator.cpp',
        'src/config/progression_config.cpp','src/midi/midi_rhythm.cpp','src/ui/mouse_feedback.cpp','src/ui/preset_library.cpp',
        'src/ui/midi_file_drag.cpp','src/ui/chord_gui.cpp','src/plugins/plugin_manager.cpp')
    & $Compiler -std=c++11 -Wall -Wextra -O2 -ffunction-sections -fdata-sections -mwindows @sources build/assets.o -o $outputPath -static-libgcc -static-libstdc++ '-Wl,--gc-sections' -lgdiplus -lcomdlg32 -lwinmm -lshell32 -lole32 -luuid
    if($LASTEXITCODE -ne 0) { throw '应用编译失败。' }
    Write-Output "构建完成：$outputPath（已嵌入 $($files.Count) 个资源文件）"
} finally { Pop-Location }