#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reject literal personal home paths in tracked project instructions/docs.

Default: inspect tracked Markdown/text files. --base REF or --staged inspects
only added lines, leaving unrelated historical findings for a private audit.
Diagnostics deliberately omit matched paths and source snippets.
"""

import argparse
from dataclasses import dataclass
import html
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import unquote


EXTERNAL_DIRECTORIES = {"vendor", "third_party", "thirdparty", "node_modules"}
HOME_PATH = re.compile(
    r"(?<![\w])(?:/(?:home|Users)/|[A-Za-z]:[\\/]Users[\\/])"
    r"(?P<account>[^/\\\r\n\"'`]+)",
    re.IGNORECASE,
)
ESCAPE = re.compile(r"\\(?:u([0-9a-fA-F]{4})|x([0-9a-fA-F]{2}))")
VARIABLE = re.compile(r"(?:\$\{[A-Za-z_][A-Za-z0-9_]*\}|\$[A-Za-z_][A-Za-z0-9_]*|%[A-Za-z_][A-Za-z0-9_]*%)")


@dataclass(frozen=True)
class Finding:
    path: str
    line: int


def project_document(path):
    """Limit the policy to authored instructions/docs, not code/assets/vendors."""
    parts = Path(path).parts
    if any(part.lower() in EXTERNAL_DIRECTORIES for part in parts):
        return False
    return Path(path).suffix.lower() in {".md", ".txt"}


def normalized_forms(text):
    """Handle common URL, HTML, JSON and Markdown escape representations."""
    forms = [text]
    for _ in range(3):
        decoded = html.unescape(unquote(forms[-1]))
        decoded = ESCAPE.sub(lambda match: chr(int(match.group(1) or match.group(2), 16)), decoded)
        decoded = decoded.replace(r"\/", "/").replace(r"\\", "\\")
        if decoded == forms[-1]:
            break
        forms.append(decoded)
    return forms


def contains_literal_home_path(text):
    for form in normalized_forms(text):
        for match in HOME_PATH.finditer(form):
            account = match.group("account").strip()
            # Angle-bracket placeholders can contain spaces; literal account
            # names cannot. Strip prose after a literal component, not inside
            # a portable placeholder expression.
            if account.startswith("<") and ">" in account:
                continue
            if VARIABLE.fullmatch(account) or account == "~":
                continue
            if account and not account.startswith(("$", "%", "{")):
                return True
    return False


def added_lines(patch):
    """Yield destination line numbers/text from zero-context Git hunks."""
    destination_line = None
    for line in patch.splitlines():
        if line.startswith("@@"):
            match = re.match(r"@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@", line)
            destination_line = int(match.group(1)) if match else None
        elif destination_line is not None and line.startswith("+"):
            yield destination_line, line[1:]
            destination_line += 1
        elif destination_line is not None and line.startswith(" "):
            destination_line += 1


def git(repo, *arguments):
    result = subprocess.run(
        ["git", "-C", str(repo), *arguments], stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False,
    )
    if result.returncode:
        raise ValueError("Git inspection failed; check the repository and reference")
    return result.stdout


def inspect(repo, *, base=None, staged=False):
    if base is not None or staged:
        options = ["diff", "--no-ext-diff", "--no-textconv"]
        if staged:
            options.append("--cached")
        if base is not None:
            # Resolve as an object, never interpret a supplied REF as an option.
            revision = git(repo, "rev-parse", "--verify", "--end-of-options", base + "^{commit}").decode().strip()
            options.append(revision)
        paths = git(repo, *options, "--name-only", "-z", "--diff-filter=ACMR", "--").split(b"\0")
    else:
        options = None
        paths = git(repo, "ls-files", "-z", "--").split(b"\0")

    findings = []
    for encoded_path in paths:
        if not encoded_path:
            continue
        path = encoded_path.decode("utf-8", errors="surrogateescape")
        if not project_document(path):
            continue
        if options is None:
            source = Path(repo) / path
            if source.is_symlink() or not source.is_file():
                continue
            lines = enumerate(source.read_text(encoding="utf-8", errors="replace").splitlines(), 1)
        else:
            patch = git(repo, *options, "--unified=0", "--", path).decode("utf-8", errors="replace")
            lines = added_lines(patch)
        findings.extend(Finding(path, number) for number, text in lines if contains_literal_home_path(text))
    return findings


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--base", metavar="REF", help="check added lines relative to a commit")
    mode.add_argument("--staged", action="store_true", help="check only staged additions")
    args = parser.parse_args(argv)
    try:
        findings = inspect(args.repo, base=args.base, staged=args.staged)
    except (OSError, ValueError):
        # OS errors may contain an absolute filename and account identifier.
        parser.exit(2, "instruction-home-paths: cannot inspect repository/reference or tracked documents\n")
    for finding in findings:
        print(f"{finding.path}:{finding.line}: literal home path; use a portable variable/placeholder", file=sys.stderr)
    if findings:
        print("Keep detailed historical findings private; this checker does not rewrite files.", file=sys.stderr)
    return int(bool(findings))


if __name__ == "__main__":
    raise SystemExit(main())
