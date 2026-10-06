param([string]$Python='python',[string]$Git='git')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$vendor=Join-Path $root 'vendor'
New-Item -ItemType Directory -Force $vendor | Out-Null
$sdk=Join-Path $vendor 'vst3sdk'
if(!(Test-Path (Join-Path $sdk 'CMakeLists.txt'))) {
    & $Git -c http.sslBackend=openssl clone --depth 1 --branch v3.8.1_build_84 --recurse-submodules --shallow-submodules https://github.com/steinbergmedia/vst3sdk.git $sdk
    if($LASTEXITCODE -ne 0) { throw '下载 VST3 SDK 失败。请检查网络后重试。' }
}
$revision=(& $Git -C $sdk rev-parse HEAD).Trim()
if($revision -ne '3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96') { throw 'SDK 版本不匹配，请使用固定的 v3.8.1_build_84 版本。' }
$archive=Join-Path $vendor 'w64devkit-x64-2.6.0.7z.exe'
$compiler=Join-Path $vendor 'w64devkit/bin/g++.exe'
if(!(Test-Path $compiler)) {
    if(!(Test-Path $archive)) {
        & $Python -c "import urllib.request,sys; urllib.request.urlretrieve('https://github.com/skeeto/w64devkit/releases/download/v2.6.0/w64devkit-x64-2.6.0.7z.exe',sys.argv[1])" $archive
        if($LASTEXITCODE -ne 0) { throw '下载 64 位编译器失败。' }
    }
    if((Get-FileHash $archive -Algorithm SHA256).Hash -ne 'BB212F0012A9324B8CF4104A76C07199D9C17607B4C5755632108D0432359BEB') { throw '编译器安装包校验失败。请移走该文件后重试。' }
    $extractor=Get-Command 7z.exe -ErrorAction SilentlyContinue
    if($extractor) {
        & $extractor.Source x $archive "-o$vendor" -y -bso0
        if($LASTEXITCODE -ne 0) { throw '解压编译器失败。' }
    } else {
        $process=Start-Process -FilePath $archive -ArgumentList @('-y',('"-o'+$vendor+'"')) -WindowStyle Hidden -Wait -PassThru
        if($process.ExitCode -ne 0) { throw '解压编译器失败。' }
    }
    if(!(Test-Path $compiler)) { throw '未找到编译器，请把安装包解压到 vendor/w64devkit 后重试。' }
}
if(!(Test-Path (Join-Path $vendor 'build_tools/cmake/data/bin/cmake.exe')) -or !(Test-Path (Join-Path $vendor 'build_tools/bin/ninja.exe'))) {
    & $Python -m pip install --target (Join-Path $vendor 'build_tools') cmake==3.31.6 ninja==1.11.1.3
    if($LASTEXITCODE -ne 0) { throw '下载 CMake 或 Ninja 失败。' }
}
Write-Output '准备完成。工具只放在 vendor 中，不会替换系统工具链。'
