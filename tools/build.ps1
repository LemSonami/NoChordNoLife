param([ValidateSet('Release','Debug')][string]$Configuration='Release',[int]$Jobs=4)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$compiler=(Join-Path $root 'vendor/w64devkit/bin').Replace('\','/')
$cmake=Join-Path $root 'vendor/build_tools/cmake/data/bin/cmake.exe'
$ninja=Join-Path $root 'vendor/build_tools/bin/ninja.exe'
if(!(Test-Path $cmake) -or !(Test-Path (Join-Path $compiler 'g++.exe'))) { throw '缺少编译工具，请先运行 tools/bootstrap.ps1。' }
$originalPath=$env:PATH
try {
    $env:PATH=$compiler+';'+$env:PATH
    & $cmake --fresh -S $root -B (Join-Path $root 'build') -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_C_COMPILER=$compiler/gcc.exe" "-DCMAKE_CXX_COMPILER=$compiler/g++.exe" "-DCMAKE_RC_COMPILER=$compiler/windres.exe"
    if($LASTEXITCODE -ne 0) { throw '配置插件构建失败。' }
    & $cmake --build (Join-Path $root 'build') --target package_plugin -j $Jobs
    if($LASTEXITCODE -ne 0) { throw '插件编译失败。请先关闭加载旧版插件的 DAW 再重新编译。' }
    Write-Output ('构建完成：'+(Join-Path $root 'dist/NoChordNoLife.vst3'))
} finally { $env:PATH=$originalPath }
