# Waits for the game to exit, then swaps the staged SilicaTest build in.
# Runs as a detached process so it survives the Qoder session.
param([string]$Dir)

$ErrorActionPreference = 'SilentlyContinue'
while (Get-Process -Name 'Minecraft.Windows' -ErrorAction SilentlyContinue) {
    Start-Sleep -Seconds 2
}

$target    = Join-Path $Dir 'SilicaTest.dll'
$stagedDll = Join-Path $Dir 'SilicaTest.dll.staged'
$stagedUi  = Join-Path $Dir 'panel.sil.staged'
$gameUi    = Join-Path $Dir 'ui\panel.sil'
$result    = Join-Path $Dir 'swap.result.disabled.txt'

if (-not (Test-Path $stagedDll) -or -not (Test-Path $stagedUi)) {
    'NO-STAGED' | Set-Content $result
    exit 1
}

for ($i = 0; $i -lt 60; $i++) {
    try {
        if (Test-Path $target) {
            $old = Join-Path $Dir ('SilicaTest.dll.' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.old')
            Move-Item -Force $target $old
        }
        Copy-Item -Force $stagedDll $target
        Copy-Item -Force $stagedUi $gameUi
        Remove-Item -Force $stagedDll
        Remove-Item -Force $stagedUi
        (Get-FileHash -Algorithm MD5 $target).Hash + ' ' + (Get-FileHash -Algorithm MD5 $gameUi).Hash | Set-Content $result
        exit 0
    } catch {
        Start-Sleep -Seconds 1
    }
}

'FAIL' | Set-Content $result
exit 1
