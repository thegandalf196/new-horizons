"""Bounded, path-free evidence for two generated Windows AI compile commands."""
import argparse
import json
import os
from pathlib import Path
import re
import stat

SOURCES = ("AI/BattleAI/AttackPossibility.cpp", "AI/BattleAI/BattleExchangeVariant.cpp")
LIMIT = 16 * 1024 * 1024
FLAG = re.compile(r"[/-](?:O[12dxst]|Ob[0-3]|O[yi]-?|GL-?|LTCG(?::(?:OFF|INCREMENTAL))?|fp:(?:fast|precise|strict)|DNDEBUG|D_ITERATOR_DEBUG_LEVEL=[012]|M[DT]d?|Z[7iI])\Z", re.I)


def split_windows(text):
    # MS CRT quote/backslash rules, not POSIX shlex (which eats Windows slashes).
    if not isinstance(text, str) or len(text) > LIMIT or "\0" in text:
        raise ValueError("invalid command text")
    words, word, quoted, started, index = [], [], False, False, 0
    while index < len(text):
        char = text[index]
        if char.isspace() and not quoted:
            if started:
                words.append("".join(word))
                word, started = [], False
            index += 1
            continue
        started = True
        if char == "\\":
            end = index
            while end < len(text) and text[end] == "\\":
                end += 1
            count = end - index
            if end < len(text) and text[end] == '"':
                word.extend("\\" * (count // 2))
                if count % 2:
                    word.append('"')
                else:
                    quoted = not quoted
                index = end + 1
            else:
                word.extend("\\" * count)
                index = end
        elif char == '"':
            if quoted and index + 1 < len(text) and text[index + 1] == '"':
                word.append('"')
                index += 2
            else:
                quoted = not quoted
                index += 1
        else:
            word.append(char)
            index += 1
    if quoted:
        raise ValueError("unbalanced command quote")
    if started:
        words.append("".join(word))
    return words


def read_plain(path, limit):
    # No raw path/exception text is emitted, including refusal diagnostics.
    for ancestor in (path, *path.parents):
        info = ancestor.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError("reparse input refused")
    before = path.stat()
    if not stat.S_ISREG(before.st_mode) or before.st_size > limit:
        raise ValueError("bounded regular input required")
    with path.open("rb") as stream:
        content = stream.read(limit + 1)
        opened = os.fstat(stream.fileno())
    after = path.stat()
    if len(content) > limit or (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) != (opened.st_dev, opened.st_ino, opened.st_size, opened.st_mtime_ns) or (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
        raise ValueError("input changed or exceeded bound")
    return content


def expand(words, directory, root, seen=None, depth=0, budget=None, tokens=None):
    seen, budget = set() if seen is None else seen, [0] if budget is None else budget
    tokens = [0] if tokens is None else tokens
    if depth > 4 or len(words) > 32768:
        raise ValueError("response expansion bound")
    result = []
    for word in words:
        if not isinstance(word, str) or "\0" in word:
            raise ValueError("invalid argument")
        if not word.startswith("@"):
            tokens[0] += 1
            if tokens[0] > 32768:
                raise ValueError("response token bound")
            result.append(word)
            continue
        path = Path(word[1:])
        path = Path(os.path.abspath(path if path.is_absolute() else directory / path))
        if os.path.commonpath((root, path)) != str(root) or path in seen:
            raise ValueError("response path or cycle refused")
        data = read_plain(path, 1024 * 1024)
        budget[0] += len(data)
        if budget[0] > 4 * 1024 * 1024:
            raise ValueError("response byte bound")
        text = data.decode("utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig")
        result.extend(expand(split_windows(text), directory, root, seen | {path}, depth + 1, budget, tokens))
    return result


def is_link_boundary(word):
    return word[:1] in {"/", "-"} and word[1:].lower() == "link"


def flags(words):
    selected = []
    for word in words:
        if is_link_boundary(word):
            break # Linker tail cannot override compiler /Ob.
        if FLAG.fullmatch(word):
            selected.append(word)
            if len(selected) > 128:
                raise ValueError("flag output bound")
    return selected


def collect(root, environment):
    root = Path(os.path.abspath(root))
    entries = json.loads(read_plain(root / "compile_commands.json", LIMIT))
    if not isinstance(entries, list):
        raise ValueError("compile database required")
    cache = read_plain(root / "CMakeCache.txt", 1024 * 1024).decode("utf-8-sig")
    wanted = {"CMAKE_CXX_FLAGS", "CMAKE_CXX_FLAGS_RELWITHDEBINFO", "CMAKE_CXX_FLAGS_RELEASE"}
    cache_flags = {}
    for line in cache.splitlines():
        match = re.match(r"([^:#]+):[^=]+=(.*)$", line)
        if match and match[1] in wanted:
            cache_flags[match[1]] = flags(split_windows(match[2]))
    rows = []
    for source in SOURCES:
        matches = [entry for entry in entries if isinstance(entry, dict) and str(entry.get("file", "")).replace("\\", "/").lower().endswith(source.lower())]
        if len(matches) != 1:
            raise ValueError("exactly one command per source required")
        entry = matches[0]
        directory = Path(os.path.abspath(entry["directory"]))
        if os.path.commonpath((root, directory)) != str(root):
            raise ValueError("command directory refused")
        words = entry.get("arguments")
        if words is None:
            words = split_windows(entry["command"])
        if not isinstance(words, list) or not words or any(word in {"&&", "||", "|", ";"} for word in words):
            raise ValueError("direct compiler command required")
        expanded = expand(words, directory, root)
        prefix_words = expand(split_windows(environment.get("CL", "")), directory, root)
        suffix_words = expand(split_windows(environment.get("_CL_", "")), directory, root)
        if any(is_link_boundary(word) for word in prefix_words + suffix_words):
            raise ValueError("environment linker boundary refused")
        prefix, suffix = flags(prefix_words), flags(suffix_words)
        compiler = [index for index, word in enumerate(expanded) if word.replace("\\", "/").rsplit("/", 1)[-1].lower() in {"cl", "cl.exe"}]
        if len(compiler) != 1:
            raise ValueError("one MSVC compiler required")
        if any(word.replace("\\", "/").rsplit("/", 1)[-1].lower() not in {"sccache", "sccache.exe"} for word in expanded[:compiler[0]]):
            raise ValueError("unresolved compiler wrapper refused")
        command_flags = flags(expanded[compiler[0] + 1:])
        rows.append({"source": source, "CL_flags": prefix, "command_flags": command_flags, "_CL__flags": suffix, "ordered_explicit_flags": prefix + command_flags + suffix})
    return {"schema": 1, "scope": "generated command and bounded response expansion; no execution or optimizer-result proof", "CL_present": "CL" in environment, "_CL__present": "_CL_" in environment, "cache_flags": cache_flags, "translation_units": rows}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-root", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        report = collect(Path(args.build_root), os.environ)
        payload = json.dumps(report, indent=2) + "\n"
        if len(payload.encode("utf-8")) > 16384:
            raise ValueError("report output bound")
        with Path(args.output).open("x", encoding="utf-8") as stream:
            stream.write(payload)
    except Exception as error:
        # Never expose paths, environment values or decoder payloads in CI logs.
        raise SystemExit("Windows AI flag evidence refused: " + type(error).__name__) from None


if __name__ == "__main__":
    main()
