# OrcaCubic Windows Build & Toolchain Guide

This document outlines the toolchain audit findings, dependency management strategy, prerequisites, and build instructions for **OrcaCubic** on Windows 11 x64.

---

## 1. System Toolchain Audit

An audit of the build tools installed on this system yielded the following locations and versions:

| Tool | Resolved Path | Version / Details |
| :--- | :--- | :--- |
| **Visual Studio Locator** (`vswhere.exe`) | `C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe` | 3.1.7 (prerelease enabled) |
| **Visual Studio IDE** | `X:\Program Files\Microsoft Visual Studio\18\Insiders` | Visual Studio Community 2026 Insiders (Dev18 / 18.5) |
| **MSVC Compiler** (`cl.exe`) | `X:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64\cl.exe` | MSVC 19.50.35726 for x64 |
| **MSBuild** (`MSBuild.exe`) | `X:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\amd64\MSBuild.exe` | MSBuild 18.5.0.12604 (.NET Framework) |
| **CMake** (`cmake.exe`) | `X:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` | CMake 4.2.3-msvc3 |
| **Git** | `C:\Program Files\Git\cmd\git.exe` | 2.50+ |
| **Python** | `C:\Python311\python.exe` / `C:\Users\User\AppData\Local\Programs\Python\Python312\python.exe` | Python 3.11 / 3.12 |
| **Installed OrcaSlicer Reference** | `C:\Program Files\OrcaSlicer` | Runtime binaries & DLLs only |

---

## 2. Critical Prerequisite: Windows SDK

### Toolchain Blocker
During compilation testing with `cl.exe` and CMake configuration with `-G "Visual Studio 18 2026"`, compilation failed during linking:
```
LINK : fatal error LNK1181: cannot open input file 'kernel32.lib'
MSB8003: The WindowsSDKDir property is not defined. Some build tools may not be found.
```
While Visual Studio Community 2026 and C++ toolset 14.50 are installed, the **Windows 10/11 Software Development Kit (SDK)** is currently missing. The Windows SDK provides essential standard headers (`windows.h`, `winsock2.h`, etc.) and system import libraries (`kernel32.lib`, `user32.lib`, etc.).

### Resolution
Install the Windows 11 SDK before starting a full compilation:

**Option A (via WinGet):**
```cmd
winget install --id=Microsoft.WindowsSDK.10.0.22621 -e
```

**Option B (via Visual Studio Installer):**
1. Open `Visual Studio Installer`.
2. Select **Modify** on Visual Studio Community 2026.
3. Under **Desktop development with C++**, ensure **Windows 11 SDK (10.0.22621.0)** or **Windows 10 SDK** is checked.
4. Complete the installation.

**Option C (Additional tools):**
OpenSSL and certain build scripts also require Perl:
```cmd
winget install --id=StrawberryPerl.StrawberryPerl -e
```

---

## 3. Dependency Requirements & Cache Strategy

OrcaSlicer relies on extensive third-party libraries defined in `deps/CMakeLists.txt`:
- **Core:** `Boost` (system, iostreams, filesystem, thread, log, locale, regex, date_time), `TBB`, `Eigen`, `CGAL`
- **Graphics / UI:** `wxWidgets` (3.2+), `GLEW`, `GLFW`, `OpenCSG`, `NanoSVG`, `WebView2`
- **Formats & I/O:** `OpenVDB`, `OpenEXR`, `Blosc`, `Assimp`, `Draco`, `PNG`, `JPEG`, `ZLIB`, `EXPAT`, `Cereal`, `Qhull`
- **Math / Crypto / Net:** `GMP`, `MPFR`, `NLopt`, `OpenSSL` (1.1.1), `CURL`, `FFMPEG`

### Can dependencies be reused from the installed OrcaSlicer?
**No.** `C:\Program Files\OrcaSlicer` and packaged release zip distributions only contain compiled runtime binaries (`.exe`) and dynamic link libraries (`.dll`). They do **not** provide:
- C++ development header trees (`include/boost/`, `include/wx/`, etc.)
- Linker import/static libraries (`.lib`)
- CMake package configurations (`lib/cmake/`)

### Dependency Build Destination
The build system installs all dependencies to:
```
<repo>/deps/build/OrcaSlicer_dep/usr/local
```
and `CMakeLists.txt` searches for this prefix at:
```
${DEP_BUILD_DIR}/OrcaSlicer_dep/usr/local
```

### Reusing a Dependency Cache
If you have a previously generated dependency archive (e.g. from CI or another build machine named `OrcaSlicer_dep_win-x64_*.zip`), you can extract it directly to:
```
deps/build/OrcaSlicer_dep
```
Once present, you can skip building dependencies and build only the slicer with:
```cmd
build_win.bat -s
```

---

## 4. Build Scripts Updates for VS 2026

The build scripts `build_win.bat` and `build_release_vs.bat` have been patched to support the Visual Studio 2026 Insiders environment present on this system:

1. **Prerelease Support (`vswhere.exe`)**:
   - Added `-prerelease` to `vswhere` queries in `build_win.bat` and `build_release_vs.bat` so Visual Studio Insiders/Preview installations are detected.
   - Changed property query to `installationVersion` so major version `18` is resolved correctly (mapping to generator `"Visual Studio 18 2026"`).
2. **Path Auto-Resolution**:
   - Updated `build_win.bat` to automatically add Visual Studio's bundled CMake (`Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin`) to `PATH` if `cmake` is not globally on the system `PATH`.
   - Updated `build_release_vs.bat` to automatically invoke `VsDevCmd.bat` if `msbuild` is not in the active command shell.

---

## 5. Build Instructions

All commands can be run from a standard Command Prompt or Git Bash.

### Step 0: Verification / Dry Run
Test that your environment and build scripts properly detect Visual Studio 2026 and CMake:

```cmd
build_win.bat -D -d
build_win.bat -D -s
```

Expected output:
```
Detected Visual Studio 18 (2026)
Configuration: Release, x64
+ cmake -S deps -B "deps/build" -G "Visual Studio 18 2026" -A x64 -DCMAKE_BUILD_TYPE=Release
+ cmake -B "build" -G "Visual Studio 18 2026" -A x64 -DORCA_TOOLS=ON -DCMAKE_BUILD_TYPE=Release
```

---

### Step 1: Build Dependencies (First Time Only)

Once the Windows SDK is installed, build the external dependencies:

```cmd
build_win.bat -d
```
*Or using the legacy script:*
```cmd
build_release_vs.bat deps
```

> **Note:** Building dependencies from source takes between 30 and 60 minutes depending on CPU core count. To pack the resulting dependencies into a portable zip for reuse:
> ```cmd
> build_win.bat -p
> ```

---

### Step 2: Build OrcaCubic Slicer

Once dependencies exist in `deps/build/OrcaSlicer_dep`:

```cmd
build_win.bat -s
```
*Or using the legacy script:*
```cmd
build_release_vs.bat slicer
```

To build only a specific target or perform incremental rebuilds without re-running CMake configure:
```cmd
build_win.bat -s --no-configure
```

---

### Step 3: Run OrcaCubic

After a successful build, the executable and Visual Studio solution are located at:
- **Executable:** `build\src\Release\orca-slicer.exe`
- **Solution:** `build\OrcaSlicer.slnx`
