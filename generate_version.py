#
# generate_version.py -- PlatformIO extra_scripts (pre: build hook).
# ============================================================
# Dijalankan OTOMATIS oleh PlatformIO SEBELUM tiap compile (baik lewat
# GUI maupun `pio run`), lihat platformio.ini (extra_scripts).
#
# Menjalankan `git describe --tags --always --dirty` di folder
# project, lalu generate include/version.h berisi
# FIRMWARE_VERSION string yang dipakai screen_settings.cpp untuk
# menampilkan versi firmware yang SEDANG BERJALAN (bukan versi
# terbaru di GitHub -- itu ditampilkan terpisah, hasil dari
# GithubOtaManager::checkAndUpdate(), lihat github_ota.h/.cpp).
#
# KENAPA PRE-BUILD SCRIPT (bukan diisi manual di config.h): supaya
# versi yang ditampilkan SELALU sinkron dengan tag git terakhir tanpa
# perlu diingat-ingat update manual tiap kali bikin release baru --
# risiko lupa update angka manual di config.h itu nyata (versi
# ditampilkan jadi bohong/basi), git describe menghindari itu.
#
# FALLBACK: kalau git tidak tersedia di PATH, atau folder project
# bukan git repo (mis. di-zip lalu di-extract tanpa histori git),
# atau belum ada tag SAMA SEKALI -- FIRMWARE_VERSION diisi
# "unknown" alih-alih bikin build gagal. Prioritas: build TETAP
# BERHASIL walau versi tidak diketahui, lebih baik daripada compile
# error gara-gara git tidak ada.
#

import subprocess
import os

Import("env")


def get_git_version():
    try:
        result = subprocess.run(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=os.path.dirname(os.path.abspath(__file__)),
            capture_output=True,
            text=True,
            timeout=5,
        )
        if result.returncode == 0:
            version = result.stdout.strip()
            if version:
                return version
    except Exception as e:
        print(f"[generate_version.py] git describe gagal: {e}")
    return "unknown"


def generate_version_header():
    version = get_git_version()
    header_path = os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "include", "version.h"
    )
    content = (
        "#pragma once\n"
        "// FILE INI DI-GENERATE OTOMATIS oleh generate_version.py tiap\n"
        "// compile -- JANGAN edit manual, perubahan akan tertimpa build\n"
        "// berikutnya. Isi/format ditentukan git describe --tags --always\n"
        "// --dirty (contoh: 'v1.0.1', 'v1.0.1-3-gabc1234' kalau ada commit\n"
        "// setelah tag terakhir, 'v1.0.1-dirty' kalau ada perubahan belum\n"
        "// di-commit saat compile).\n"
        f'#define FIRMWARE_VERSION "{version}"\n'
    )
    with open(header_path, "w") as f:
        f.write(content)
    print(f"[generate_version.py] FIRMWARE_VERSION = {version}")


generate_version_header()
