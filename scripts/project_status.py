#!/usr/bin/env python3
"""Read project state; optionally render the derived status board. Stdlib only."""
import argparse
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def git(*args):
    result = subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else "(尚无可读取的 Git 状态)"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true", help="render coordination/STATUS.md")
    args = parser.parse_args()
    state = json.loads((ROOT / "coordination/state.json").read_text())
    tasks = state["tasks"]
    ids = {task["id"] for task in tasks}
    if len(ids) != len(tasks):
        raise SystemExit("Duplicate task ID")
    for task in tasks:
        if (set(task["dependencies"]) | set(task.get("pass_gates", []))) - ids:
            raise SystemExit(f"Unknown dependency: {task['id']}")
        if task["status"] in {"ready", "in_progress", "passed"}:
            unmet = [dep for dep in task["dependencies"] if
                     next(item for item in tasks if item["id"] == dep)["status"] != "passed"]
            if unmet:
                raise SystemExit(f"{task['id']}: unmet dependencies {unmet}")
        if task["status"] == "passed":
            unmet_gates = [gate for gate in task.get("pass_gates", []) if
                           next(item for item in tasks if item["id"] == gate)["status"] != "passed"]
            if unmet_gates:
                raise SystemExit(f"{task['id']}: unmet acceptance gates {unmet_gates}")
    lines = ["# 当前项目状态（生成文件）", "", f"状态事件时间：{state['updated_at']}",
             f"阶段：{state['phase']}", f"总控：{state['coordinator'] or '未登记'}",
             "更新方式：按事件更新，没有后台自动刷新。", "",
             "| 任务 | 角色 | 状态 | 实际负责人 | 启动依赖 | 验收门禁 | 分支 |",
             "|---|---|---|---|---|---|---|"]
    for task in tasks:
        lines.append(f"| {task['id']} | {task['role']} | {task['status']} | "
                     f"{task['owner'] or '未领取'} | {', '.join(task['dependencies']) or '无'} | "
                     f"{', '.join(task.get('pass_gates', [])) or '无额外门禁'} | "
                     f"{task['branch'] or '未创建'} |")
    for title, key in [("独占写入", "active_writers"), ("阻塞", "blockers"),
                       ("已验证", "verified"), ("未验证", "unverified")]:
        lines += ["", f"## {title}", ""]
        lines += ["- " + (item if isinstance(item, str) else json.dumps(item, ensure_ascii=False))
                  for item in state[key]] or ["- 无"]
    lines += ["", "具体恢复命令与未完成修改见 HANDOFF.md。", ""]
    board = "\n".join(lines)
    if args.write:
        (ROOT / "coordination/STATUS.md").write_text(board)
    print(board)
    print("当前实际 Git 分支：", git("branch", "--show-current"))
    print("当前实际 HEAD：", git("rev-parse", "HEAD"))
    print("当前实际工作区：\n", git("status", "--short") or "clean")


if __name__ == "__main__":
    main()
