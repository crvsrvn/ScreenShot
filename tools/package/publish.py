"""把发布构建发布到 <父目录>\\ScreenShot：取代已有程序，保留用户设置。

用法：python -I publish.py --source <dist/portable> [父目录] [--launch]
- 没给父目录时弹出目录选择框；程序总是放进父目录下的 ScreenShot 子目录，父目录里的其他内容不动。
- ScreenShot 目录里正在运行的 screenshot_app.exe 会先被结束。
- ScreenShot 目录已存在时，除 config（设置、日志、贴图恢复、录制恢复等用户数据）以外的内容全部删除，再复制新程序；
  若它非空却不是本程序的安装目录（没有 screenshot_app.exe），拒绝发布，不删任何东西。
"""
import argparse
import pathlib
import shutil
import subprocess
import sys
import time

KEEP = {"config"}  # 程序目录下保留的用户数据（settings.json 等都在 config 里）
EXE = "screenshot_app.exe"
APP_DIR = "ScreenShot"  # 在用户选择的父目录下新建的程序目录


def pick_folder():
    script = (
        "Add-Type -AssemblyName System.Windows.Forms;"
        "$d = New-Object System.Windows.Forms.FolderBrowserDialog;"
        "$d.Description = 'Choose the parent folder; ScreenShot will be published into its ScreenShot subfolder';"
        "$d.ShowNewFolderButton = $true;"
        "if ($d.ShowDialog() -eq 'OK') { Write-Output $d.SelectedPath }"
    )
    out = subprocess.run(["powershell", "-NoProfile", "-STA", "-Command", script], capture_output=True, text=True)
    return out.stdout.strip()


def stop_running(target: pathlib.Path):
    # 只结束从目标目录启动的实例，不影响其他位置的程序
    script = (
        f"Get-Process -Name '{EXE[:-4]}' -ErrorAction SilentlyContinue | "
        f"Where-Object {{ $_.Path -and $_.Path.StartsWith('{target}\\', [System.StringComparison]::OrdinalIgnoreCase) }} | "
        "ForEach-Object { Stop-Process -Id $_.Id -Force; Write-Output $_.Id }"
    )
    out = subprocess.run(["powershell", "-NoProfile", "-Command", script], capture_output=True, text=True)
    pids = out.stdout.split()
    if pids:
        print(f"已结束正在运行的实例：{', '.join(pids)}")
        time.sleep(1.0)  # 等文件句柄释放


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", required=True, help="windeployqt 产出的便携目录")
    ap.add_argument("target", nargs="?", help="父目录，程序发布到其下的 ScreenShot 子目录；省略时弹出选择框")
    ap.add_argument("--launch", action="store_true", help="发布后启动程序")
    a = ap.parse_args()

    source = pathlib.Path(a.source).resolve()
    if not (source / EXE).exists():
        print(f"没有找到发布构建：{source / EXE}\n请用 scripts\\publish.cmd 构建并发布")
        return 1
    target_text = a.target or pick_folder()
    if not target_text:
        print("没有选择目录，已取消")
        return 1
    target = pathlib.Path(target_text).resolve() / APP_DIR
    if target == source or source in target.parents:
        print("目标目录不能是发布构建自身")
        return 1
    # 只清理本程序自己的安装目录：非空却没有主程序，说明是别的内容，宁可不发布也不删
    if target.is_dir() and any(target.iterdir()) and not (target / EXE).exists():
        print(f"{target} 已存在且不是 ScreenShot 的安装目录（没有 {EXE}），为避免误删，已取消")
        return 1
    target.mkdir(parents=True, exist_ok=True)

    stop_running(target)
    kept = []
    for item in list(target.iterdir()):
        if item.name.lower() in KEEP:
            kept.append(item.name)
            continue
        if item.is_dir():
            shutil.rmtree(item)
        else:
            item.unlink()
    for item in source.iterdir():
        if item.is_dir():
            shutil.copytree(item, target / item.name)
        else:
            shutil.copy2(item, target / item.name)
    print(f"已发布到 {target}")
    print("保留的用户数据：" + (", ".join(kept) if kept else "无（首次发布，设置将写在 config\\settings.json）"))
    if a.launch:
        subprocess.Popen([str(target / EXE)], cwd=str(target))
        print("程序已启动")
    return 0


if __name__ == "__main__":
    sys.exit(main())
