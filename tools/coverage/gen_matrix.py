#!/usr/bin/env python3
"""需求覆盖矩阵（TST-01）。
# REQ: TST-01 NFR-15 AC-14 —— 本工具就是“需求 → 用例”矩阵与无空项检查，两个平台共用同一套用例与范围文件

扫描需求文档中的全部编号（形如 `CAP-01`），扫描测试源码中的 `REQ:` 标注，
生成 Markdown 矩阵；已交付范围（scope 文件）内任何一条没有用例即失败（退出码 1）。
"""
import argparse
import pathlib
import re
import sys

ID_RE = re.compile(r"`([A-Z]{2,4}-\d{2,3})`")
REQ_LINE_RE = re.compile(r"REQ:\s*([A-Z0-9\- ]+)")
PRIORITY_RE = re.compile(r":(danger|warn|muted)\[(P[012])\]")


def scan_requirements(doc: pathlib.Path):
    """返回 {编号: 优先级或空}，按文档出现顺序。"""
    reqs = {}
    for line in doc.read_text(encoding="utf-8").splitlines():
        ids = ID_RE.findall(line)
        if not ids:
            continue
        pri = PRIORITY_RE.search(line)
        for i in ids:
            reqs.setdefault(i, pri.group(2) if pri else "")
    return reqs


def scan_tests(roots):
    """返回 {编号: [用例文件名]}。"""
    covered = {}
    for root in roots:
        for path in pathlib.Path(root).rglob("*"):
            if path.suffix not in (".cpp", ".h", ".py", ".txt") or not path.is_file():
                continue
            try:
                text = path.read_text(encoding="utf-8")
            except UnicodeDecodeError:
                continue
            for m in REQ_LINE_RE.finditer(text):
                for i in m.group(1).split():
                    if re.fullmatch(r"[A-Z]{2,4}-\d{2,3}", i):
                        covered.setdefault(i, set()).add(path.name)
    return covered


def read_scope(path: pathlib.Path):
    scope = []
    if not path.exists():
        return scope
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            scope.append(line)
    return scope


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--docs", required=True, type=pathlib.Path)
    ap.add_argument("--tests", nargs="+", required=True)
    ap.add_argument("--scope", required=True, type=pathlib.Path)
    ap.add_argument("--out", required=True, type=pathlib.Path)
    args = ap.parse_args()

    reqs = scan_requirements(args.docs)
    covered = scan_tests(args.tests)
    scope = read_scope(args.scope)

    unknown = [i for i in scope if i not in reqs]
    missing = [i for i in scope if i not in covered]
    rows = ["| 编号 | 优先级 | 已交付 | 用例 |", "|---|---|---|---|"]
    for rid, pri in reqs.items():
        cases = ", ".join(sorted(covered.get(rid, ()))) or "—"
        rows.append(f"| `{rid}` | {pri} | {'是' if rid in scope else ''} | {cases} |")
    total = len(reqs)
    done = sum(1 for r in reqs if r in covered)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "# 需求覆盖矩阵\n\n"
        f"需求总数 {total}，有用例 {done}，已交付范围 {len(scope)}，范围内缺用例 {len(missing)}\n\n"
        + "\n".join(rows) + "\n", encoding="utf-8")
    print(f"需求 {total}，有用例 {done}，已交付范围 {len(scope)}")
    if unknown:
        print("范围文件里有文档中不存在的编号：", ", ".join(unknown))
    if missing:
        print("已交付范围内缺少用例：", ", ".join(missing))
        return 1
    if unknown:
        return 1
    print("覆盖矩阵写入", args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
