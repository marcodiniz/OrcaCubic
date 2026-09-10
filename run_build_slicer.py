import subprocess, sys, time, os

cmake = r"X:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
log_path = r"X:\code\marcodiniz\OrcaCubic\build_slicer.log"

env = os.environ.copy()
# Ensure Strawberry Perl is first on PATH
strawberry_perl = r"C:\Strawberry\perl\bin"
env["PATH"] = strawberry_perl + ";" + env.get("PATH", "")

print(f"Starting OrcaCubic slicer build, logging to {log_path}...")
with open(log_path, "w", encoding="utf-8") as log_file:
    # 1. Configure slicer with CMake
    prefix_path = r"X:\code\marcodiniz\OrcaCubic\deps\build\OrcaSlicer_dep\usr\local"
    cfg_cmd = [
        cmake, "-S", ".", "-B", "build",
        "-G", "Visual Studio 18 2026", "-A", "x64",
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_PREFIX_PATH={prefix_path}"
    ]
    print("Configuring with CMake...")
    p_cfg = subprocess.run(cfg_cmd, cwd=r"X:\code\marcodiniz\OrcaCubic", env=env, stdout=log_file, stderr=subprocess.STDOUT)
    if p_cfg.returncode != 0:
        print(f"CMake configuration failed with code {p_cfg.returncode}")
        sys.exit(p_cfg.returncode)

    print("Building OrcaCubic target...")
    build_cmd = [cmake, "--build", "build", "--config", "Release", "--target", "OrcaSlicer", "--", "-m"]
    p_build = subprocess.Popen(build_cmd, cwd=r"X:\code\marcodiniz\OrcaCubic", env=env, stdout=log_file, stderr=subprocess.STDOUT)
    print(f"Launched build PID {p_build.pid}")
    p_build.wait()
    print(f"Build completed with return code {p_build.returncode}")
    sys.exit(p_build.returncode)
