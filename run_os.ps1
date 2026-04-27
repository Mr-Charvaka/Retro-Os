# Retro-OS QEMU Launch Script
$QEMU_PATH = "C:\Program Files\qemu\qemu-system-i386.exe"

if (-not (Test-Path $QEMU_PATH)) {
    Write-Error "QEMU not found at $QEMU_PATH. Please check your installation."
    exit
}

Write-Host "Launching Retro-OS..." -ForegroundColor Cyan
Write-Host "Cores: 4 | Memory: 2GB | Graphics: VESA/BGA" -ForegroundColor Gray

& $QEMU_PATH `
    -m 2G `
    -smp 4 `
    -machine q35 `
    -drive file=os.img, format=raw `
    -drive file=brain_disk.img, format=raw `
    -serial stdio `
    -vga std `
    -device e1000, netdev=net0 `
    -netdev user, id=net0
