# REQ: NFR-12
"""绿色版打包与体积检查（NFR-12）。

windeployqt 把 Qt 运行时（含 MSVC 运行库）收集到 dist/portable，连同翻译打成
ScreenShot-<版本>-win64-portable.zip；要求 ≤ 50 MB，且包里不能有 Qt Network（零网络）。
有 Inno Setup（ISCC.exe）时再用 scripts/installer.iss 生成安装包，同样检查体积。
"""
import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
import zipfile

LIMIT = 50 * 1024 * 1024


def find_iscc():
    found = shutil.which("ISCC")
    if found:
        return found
    for base in (os.environ.get("ProgramFiles(x86)", ""), os.environ.get("ProgramFiles", ""), os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs")):
        cand = pathlib.Path(base) / "Inno Setup 6" / "ISCC.exe"
        if base and cand.exists():
            return str(cand)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--windeployqt", required=True)
    ap.add_argument("--translations", required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--iss", required=True)
    a = ap.parse_args()

    out = pathlib.Path(a.out).resolve()
    if a.version == "auto":
        # 从 src/CMakeLists.txt 的 project(... VERSION x.y.z) 取版本
        text = (pathlib.Path(__file__).resolve().parents[2] / "src" / "CMakeLists.txt").read_text(encoding="utf-8")
        a.version = re.search(r"project\(.*?VERSION\s+([0-9.]+)", text, re.S).group(1)
    staging = out / "portable"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    exe = pathlib.Path(a.exe).resolve()
    shutil.copy2(exe, staging / exe.name)
    # MSVC 运行库：优先把 CRT 的 DLL 放在程序目录（真正的“解压即用”）；没有 VC 工具链环境时退回 windeployqt 的 vc_redist 安装器
    redist = pathlib.Path(os.environ.get("VCToolsRedistDir", "")) / "x64"
    crt_dirs = sorted(redist.glob("Microsoft.VC14*.CRT")) if redist.exists() else []
    cmd = [a.windeployqt, "--release", "--no-translations", "--no-opengl-sw", "--no-system-d3d-compiler", "--no-system-dxc-compiler",
           "--no-network", "--no-compiler-runtime" if crt_dirs else "--compiler-runtime", "--dir", str(staging), str(staging / exe.name)]
    print(" ".join(cmd))
    subprocess.run(cmd, check=True)
    for dll in (crt_dirs[-1].glob("*.dll") if crt_dirs else []):
        shutil.copy2(dll, staging / dll.name)
    tr = pathlib.Path(a.translations)
    if tr.exists():
        shutil.copytree(tr, staging / "translations", dirs_exist_ok=True)
    for pdb in staging.rglob("*.pdb"):
        pdb.unlink()

    network = [p.name for p in staging.rglob("*.dll") if "network" in p.name.lower() or p.name.lower().startswith("qt6network")]
    if network:
        print("包里有网络组件：", network)
        return 1

    zip_path = out / f"ScreenShot-{a.version}-win64-portable.zip"
    if zip_path.exists():
        zip_path.unlink()
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for p in staging.rglob("*"):
            if p.is_file():
                z.write(p, p.relative_to(staging))
    size = zip_path.stat().st_size
    print(f"portable: {zip_path} {size / 1048576:.1f} MB")
    if size > LIMIT:
        print("绿色版超过 50 MB")
        return 1

    iscc = find_iscc()
    if not iscc:
        print("没有 Inno Setup，跳过安装包（绿色版已满足要求）")
        return 0
    cmd = [iscc, f"/DAppVersion={a.version}", f"/DSourceDir={staging}", f"/DOutDir={out}", str(pathlib.Path(a.iss).resolve())]
    print(" ".join(cmd))
    subprocess.run(cmd, check=True)
    installers = sorted(out.glob("ScreenShot-*-setup.exe"), key=lambda p: p.stat().st_mtime)
    if not installers:
        print("安装包没有生成")
        return 1
    size = installers[-1].stat().st_size
    print(f"installer: {installers[-1]} {size / 1048576:.1f} MB")
    return 0 if size <= LIMIT else 1


if __name__ == "__main__":
    sys.exit(main())
