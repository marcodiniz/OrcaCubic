import subprocess, sys, time, os

cmake = r"X:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
log_path = r"X:\code\marcodiniz\OrcaCubic\deps\build\build_deps.log"

env = os.environ.copy()
# Ensure Strawberry Perl is first on PATH
strawberry_perl = r"C:\Strawberry\perl\bin"
env["PATH"] = strawberry_perl + ";" + env.get("PATH", "")

print(f"Starting deps build with Strawberry Perl on PATH...")
print(f"Logging to {log_path}...")
with open(log_path, "w", encoding="utf-8") as log_file:
    cmd = [cmake, "--build", "deps/build", "--config", "Release", "--target", "deps"]
    p = subprocess.Popen(cmd, cwd=r"X:\code\marcodiniz\OrcaCubic", env=env, stdout=log_file, stderr=subprocess.STDOUT)
    print(f"Launched PID {p.pid}")
    p.wait()
    print(f"Deps build completed with return code {p.returncode}")
    sys.exit(p.returncode)
