#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CE6ML 配置编辑工具（命令行版）

编辑游戏目录下的 CE6ML.ini，保留原有注释。
ini 由游戏首次启动时自动生成，也可用本工具直接创建。

用法示例:
    python ce6ml_config.py show
    python ce6ml_config.py set-debug on
    python ce6ml_config.py set-mod-dir "D:\\Mods\\DL"
    python ce6ml_config.py list
    python ce6ml_config.py add cool_mod.pak
    python ce6ml_config.py add cool_mod.pak --index 1
    python ce6ml_config.py remove cool_mod.pak
    python ce6ml_config.py move cool_mod.pak 2

默认操作当前目录的 CE6ML.ini，可用 -c/--config 指定其他路径，
例如:  python ce6ml_config.py -c "D:\\Games\\Dying Light\\CE6ML.ini" show
"""
from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

DEFAULT_INI = """; CE6ML 配置文件
; 以 ; 或 # 开头的行为注释

[general]
; 是否显示调试控制台 (true/false)
debug = false

[paths]
; mod 目录：绝对路径，或相对于游戏目录的路径
mod_dir = mods

[load_order]
; pak 加载顺序：数字越小越先加载
; 1 = example.pak
"""


@dataclass
class Line:
    type: str  # 'section' | 'kv' | 'raw'
    raw: str = ""
    section: str = ""
    key: str = ""
    value: str = ""


def _strip_inline_comment(value: str) -> str:
    """去掉 " ; 注释" / " # 注释" 形式的行内注释（标记前需有空白）。"""
    for i in range(1, len(value)):
        if value[i] in ";#" and value[i - 1] in " \t":
            return value[:i].rstrip()
    return value


def parse(text: str) -> list[Line]:
    lines: list[Line] = []
    section = ""
    for raw in text.splitlines():
        t = raw.strip()
        if t.startswith("[") and t.endswith("]") and len(t) >= 2:
            section = t[1:-1].strip()
            lines.append(Line("section", raw=raw, section=section))
        elif "=" in t and t and not t.startswith((";", "#")):
            key, _, value = t.partition("=")
            lines.append(Line("kv", section=section, key=key.strip(),
                              value=_strip_inline_comment(value.strip())))
        else:
            lines.append(Line("raw", raw=raw))
    return lines


def serialize(lines: list[Line]) -> str:
    out = []
    for l in lines:
        if l.type == "kv":
            out.append(f"{l.key} = {l.value}")
        else:
            out.append(l.raw)
    return "\n".join(out) + "\n"


def load_lines(path: Path) -> list[Line]:
    if not path.exists():
        print(f"未找到 {path}，已创建默认配置")
        path.write_text(DEFAULT_INI, encoding="utf-8")
    return parse(path.read_text(encoding="utf-8-sig"))


def save_lines(path: Path, lines: list[Line]) -> None:
    path.write_text(serialize(lines), encoding="utf-8")


def get_key(lines: list[Line], section: str, key: str) -> str | None:
    for l in lines:
        if l.type == "kv" and l.section == section and l.key == key:
            return l.value
    return None


def set_key(lines: list[Line], section: str, key: str, value: str) -> None:
    for l in lines:
        if l.type == "kv" and l.section == section and l.key == key:
            l.value = value
            return
    # 不存在则插入到该 section 最后一个 kv 之后（或 section 头之后）
    insert_at = None
    in_sec = False
    for i, l in enumerate(lines):
        if l.type == "section":
            in_sec = l.section == section
            if in_sec:
                insert_at = i + 1
        elif l.type == "kv" and in_sec:
            insert_at = i + 1
    new_line = Line("kv", section=section, key=key, value=value)
    if insert_at is not None:
        lines.insert(insert_at, new_line)
    else:
        lines.append(Line("section", raw=f"[{section}]", section=section))
        lines.append(new_line)


def get_load_order(lines: list[Line]) -> list[str]:
    entries: list[tuple[int, int, str]] = []  # (排序键, 原顺序, 名称)
    for seq, l in enumerate(lines):
        if l.type == "kv" and l.section == "load_order" and l.value:
            try:
                idx = int(l.key)
            except ValueError:
                idx = 10**9 + seq
            entries.append((idx, seq, l.value))
    entries.sort(key=lambda e: (e[0], e[1]))
    return [v for _, _, v in entries]


def rewrite_load_order(lines: list[Line], entries: list[str]) -> None:
    """用新的 pak 列表重写 [load_order]，键重新编号为 1..n，注释保留。"""
    kv_idx = [i for i, l in enumerate(lines)
              if l.type == "kv" and l.section == "load_order"]
    sec_idx = next((i for i, l in enumerate(lines)
                    if l.type == "section" and l.section == "load_order"), None)

    # 1) 覆盖现有 kv 行
    for n, i in enumerate(kv_idx[: len(entries)]):
        lines[i].key = str(n + 1)
        lines[i].value = entries[n]

    if len(entries) < len(kv_idx):
        # 2a) 多余的 kv 行从后往前删
        for i in reversed(kv_idx[len(entries):]):
            del lines[i]
    elif len(entries) > len(kv_idx):
        # 2b) 不足则插到最后一个 kv 之后；若还没有 kv，则插到 section 头的注释块之后
        extra = entries[len(kv_idx):]
        new_lines = [Line("kv", section="load_order", key=str(len(kv_idx) + 1 + j), value=v)
                     for j, v in enumerate(extra)]
        if kv_idx:
            lines[kv_idx[-1] + 1: kv_idx[-1] + 1] = new_lines
        elif sec_idx is not None:
            pos = sec_idx + 1
            while pos < len(lines) and lines[pos].type == "raw":
                pos += 1
            lines[pos:pos] = new_lines
        else:
            lines.append(Line("section", raw="[load_order]", section="load_order"))
            lines.extend(new_lines)


def _print_order(entries: list[str]) -> None:
    if not entries:
        print("  (空)")
    for i, p in enumerate(entries, 1):
        print(f"  {i}. {p}")


# ---------------- 子命令 ----------------

def cmd_show(args, path: Path) -> int:
    lines = load_lines(path)
    debug = get_key(lines, "general", "debug") or "false"
    mod_dir = get_key(lines, "paths", "mod_dir") or "mods"
    print(f"debug   = {debug}")
    print(f"mod_dir = {mod_dir}")
    print("加载顺序:")
    _print_order(get_load_order(lines))
    return 0


def cmd_set_debug(args, path: Path) -> int:
    lines = load_lines(path)
    value = "true" if args.state == "on" else "false"
    set_key(lines, "general", "debug", value)
    save_lines(path, lines)
    print(f"debug = {value}")
    return 0


def cmd_set_mod_dir(args, path: Path) -> int:
    lines = load_lines(path)
    set_key(lines, "paths", "mod_dir", args.mod_dir)
    save_lines(path, lines)
    print(f"mod_dir = {args.mod_dir}")
    return 0


def cmd_list(args, path: Path) -> int:
    _print_order(get_load_order(load_lines(path)))
    return 0


def cmd_add(args, path: Path) -> int:
    lines = load_lines(path)
    entries = get_load_order(lines)
    if args.pak in entries:
        print(f"已存在: {args.pak}", file=sys.stderr)
        return 1
    pos = len(entries) if args.index is None else max(0, min(args.index - 1, len(entries)))
    entries.insert(pos, args.pak)
    rewrite_load_order(lines, entries)
    save_lines(path, lines)
    print(f"已添加 {args.pak}，当前顺序:")
    _print_order(entries)
    return 0


def cmd_remove(args, path: Path) -> int:
    lines = load_lines(path)
    entries = get_load_order(lines)
    if args.pak not in entries:
        print(f"未找到: {args.pak}", file=sys.stderr)
        return 1
    entries.remove(args.pak)
    rewrite_load_order(lines, entries)
    save_lines(path, lines)
    print(f"已移除 {args.pak}，当前顺序:")
    _print_order(entries)
    return 0


def cmd_move(args, path: Path) -> int:
    lines = load_lines(path)
    entries = get_load_order(lines)
    if args.pak not in entries:
        print(f"未找到: {args.pak}", file=sys.stderr)
        return 1
    entries.remove(args.pak)
    pos = max(0, min(args.index - 1, len(entries)))
    entries.insert(pos, args.pak)
    rewrite_load_order(lines, entries)
    save_lines(path, lines)
    print(f"已移动 {args.pak} 到第 {pos + 1} 位，当前顺序:")
    _print_order(entries)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="CE6ML 配置编辑工具（编辑 CE6ML.ini，保留注释）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("-c", "--config", default="CE6ML.ini",
                        help="CE6ML.ini 路径（默认当前目录）")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("show", help="显示当前配置")

    p = sub.add_parser("set-debug", help="开关调试控制台")
    p.add_argument("state", choices=["on", "off"])

    p = sub.add_parser("set-mod-dir", help="设置 mod 目录（绝对路径或相对游戏目录）")
    p.add_argument("mod_dir")

    sub.add_parser("list", help="列出 pak 加载顺序")

    p = sub.add_parser("add", help="添加 pak 到加载顺序")
    p.add_argument("pak")
    p.add_argument("--index", type=int, default=None, help="插入位置（从 1 开始，默认末尾）")

    p = sub.add_parser("remove", help="从加载顺序移除 pak")
    p.add_argument("pak")

    p = sub.add_parser("move", help="调整 pak 顺序")
    p.add_argument("pak")
    p.add_argument("index", type=int, help="目标位置（从 1 开始）")

    args = parser.parse_args()
    path = Path(args.config)
    handler = {
        "show": cmd_show,
        "set-debug": cmd_set_debug,
        "set-mod-dir": cmd_set_mod_dir,
        "list": cmd_list,
        "add": cmd_add,
        "remove": cmd_remove,
        "move": cmd_move,
    }[args.command]
    return handler(args, path)


if __name__ == "__main__":
    sys.exit(main())
