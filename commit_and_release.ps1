# commit_and_release.ps1
# Untuk dipakai SETELAH file .cpp/.h sudah ditimpa manual (zip sudah
# di-extract sendiri). Langsung: commit -> tag -> push -> compile ->
# CEK VERSI BERSIH -> gh release create (dengan jeda konfirmasi).
#
# WAJIB DIISI SEBELUM JALANKAN:
$repoPath    = "D:\gbw_manuale"
$version     = "v1.0.15"                          # <-- GANTI sesuai versi berikutnya yang benar
$commitMsg   = "Tambah Settle Time setting; fix overlap Settings; perbesar gear icon"
$releaseNote = "Settle Time (200-2000ms) bisa diatur di Settings. Fix overlap teks Manual Grind/Debug row. Gear icon diperbesar (font 14->16)."

$ErrorActionPreference = "Stop"

if (-not (Test-Path $repoPath)) { Write-Host "ERROR: repo tidak ditemukan: $repoPath" -ForegroundColor Red; exit 1 }
Set-Location $repoPath

# 1. Cek dulu file apa saja yang berubah -- supaya bisa direview sebelum commit
Write-Host "`n[1/5] File yang berubah (git status):" -ForegroundColor Cyan
git status --short

$proceed = Read-Host "`nSesuai yang diharapkan? Ketik 'yes' untuk lanjut commit, lainnya untuk batal"
if ($proceed -ne "yes") { Write-Host "Dibatalkan." -ForegroundColor Yellow; exit 0 }

# 2. Commit & push
Write-Host "`n[2/5] Commit & push..." -ForegroundColor Cyan
git add -A
git commit -m "$commitMsg"
git push
if ($LASTEXITCODE -ne 0) { Write-Host "ERROR saat commit/push." -ForegroundColor Red; exit 1 }

# 3. Tag & push tag (WAJIB setelah commit, SEBELUM compile)
Write-Host "`n[3/5] Tag & push tag $version..." -ForegroundColor Cyan
git tag $version
git push --tags
if ($LASTEXITCODE -ne 0) { Write-Host "ERROR saat tag/push tag." -ForegroundColor Red; exit 1 }

# 4. Compile
Write-Host "`n[4/5] Compiling (bisa beberapa menit)..." -ForegroundColor Cyan
$pioLines = & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e esp32-c6-devkitc-1 2>&1
$pioLines | ForEach-Object { Write-Host $_ }

if ($LASTEXITCODE -ne 0) {
    Write-Host "`nCOMPILE GAGAL. Paste output di atas ke Claude untuk diperbaiki." -ForegroundColor Red
    exit 1
}

$versionLine = $pioLines | Select-String "FIRMWARE_VERSION"
Write-Host "`nCek versi: $versionLine" -ForegroundColor Cyan

if ($versionLine -notmatch [regex]::Escape($version) -or $versionLine -match "-dirty") {
    Write-Host "`nSTOP -- versi TIDAK bersih atau tidak sesuai ($version diharapkan)." -ForegroundColor Red
    Write-Host "JANGAN lanjut ke release." -ForegroundColor Yellow
    exit 1
}
Write-Host "Versi bersih, aman lanjut." -ForegroundColor Green

# 5. Release -- jeda konfirmasi manual
Write-Host "`n[5/5] Siap release $version ke GitHub." -ForegroundColor Cyan
$confirm = Read-Host "Ketik 'yes' untuk lanjut gh release create, lainnya untuk batal"
if ($confirm -ne "yes") {
    Write-Host "Dibatalkan. Firmware sudah ter-compile di .pio\build\esp32-c6-devkitc-1\firmware.bin." -ForegroundColor Yellow
    exit 0
}

$notesFile = Join-Path $env:TEMP "release_notes_$version.md"
$releaseNote | Out-File -FilePath $notesFile -Encoding utf8

gh release create $version ".pio\build\esp32-c6-devkitc-1\firmware.bin" --title "$version" --notes-file "$notesFile"

Write-Host "`nSelesai. Kalau berhasil, ada URL release di atas." -ForegroundColor Green
Write-Host "Langkah terakhir: di board, Settings -> CHECK untuk trigger OTA." -ForegroundColor Yellow
