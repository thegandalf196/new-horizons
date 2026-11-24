#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Report file-private definitions that collide inside one CMake Unity batch.

Reads the `Unity/unity_*_cxx.cxx` files of an already configured
`ENABLE_NH_UNITY_BUILD=ON` build directory (configure only; nothing is
compiled) and lexically scans the sources each batch includes. Two sources in
one batch become one translation unit, so these become redefinitions there:

* functions with the same name and parameter types defined in the same
  anonymous namespace, or `static`/`inline`/`constexpr` at the same scope;
* same-named variables, aliases or class/enum definitions at the same
  namespace scope;
* the same macro defined in two sources without a matching `#undef`.

Overloads with different parameter types are legal and not reported. The scan
is a quick lexical check, not a compiler: compile the Unity targets for proof.
"""
import argparse
from collections import defaultdict
from pathlib import Path
import re
import sys

INCLUDE = re.compile(r'^#include\s+"([^"]+\.(?:cpp|cc|cxx))"', re.M)
IDENT = re.compile(r'[A-Za-z_]\w*')
NOT_NAMES = {'static_assert', 'operator', 'decltype', 'sizeof', 'alignas', 'noexcept', 'requires',
             'if', 'for', 'while', 'switch', 'return', 'template', 'typename', 'extern'}
SPECIFIERS = {'static', 'inline', 'constexpr', 'consteval', 'constinit', 'thread_local',
              'const', 'volatile', 'unsigned', 'signed', 'short', 'long', 'int', 'char', 'bool',
              'float', 'double', 'void', 'auto', 'struct', 'class', 'enum', 'typename'}


def strip_code(text):
    """Blank comments and literals, keeping preprocessor lines and line count."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append('\n' * text.count('\n', i, j))
            i = j
        elif c == 'R' and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            k = text.find('(', i + 2)
            delimiter = ')' + text[i + 2:k] + '"'
            j = text.find(delimiter, k)
            j = n if j < 0 else j + len(delimiter)
            out.append('""' + '\n' * text.count('\n', i, j))
            i = j
        elif c in '"\'':
            if c == "'" and i > 0 and text[i - 1].isalnum():  # digit separator 1'000
                i += 1
                continue
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def split_top(text, separator=','):
    parts, depth, current = [], 0, []
    for c in text:
        if c in '(<[{':
            depth += 1
        elif c in ')>]}':
            depth -= 1
        if c == separator and depth == 0:
            parts.append(''.join(current))
            current = []
        else:
            current.append(c)
    parts.append(''.join(current))
    return [part.strip() for part in parts if part.strip()]


def parameter_types(parameters):
    """Normalized parameter types: names and default arguments dropped."""
    types = []
    for parameter in split_top(parameters):
        parameter = split_top(parameter, '=')[0] if '=' in parameter else parameter
        tokens = re.findall(r'[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*|::|<[^()]*>|[&*]+|\.\.\.|\S', parameter)
        if len(tokens) > 1 and IDENT.fullmatch(tokens[-1]) and tokens[-1] not in SPECIFIERS:
            tokens.pop()
        types.append(' '.join(tokens))
    return tuple(t for t in types if t != 'void')


def classify(head, has_body):
    """Return (kind, name, signature) for a namespace-scope head, or None."""
    head = re.sub(r'\[\[.*?\]\]', ' ', head).strip()
    head = re.sub(r'^template\s*<.*?>\s*', '', head, flags=re.S) if head.startswith('template') else head
    if not head or head.startswith(('using namespace', 'namespace', 'friend', 'extern ')):
        return None
    words = head.split()
    if words[0] in ('struct', 'class', 'union', 'enum'):
        if not has_body:
            return None
        names = [w for w in IDENT.findall(head) if w not in ('struct', 'class', 'union', 'enum', 'alignas')]
        return ('type', names[0], None) if names else None
    alias = re.match(r'using\s+([A-Za-z_]\w*)\s*=(.*)$', head)
    if alias:  # repeating an alias of the same type is legal
        return ('alias', alias.group(1), ' '.join(alias.group(2).split()))
    if words[0] == 'using':
        return None  # a using-declaration may be repeated
    if words[0] == 'typedef':
        names = IDENT.findall(head)
        return ('alias', names[-1], ' '.join(head[len('typedef'):].replace(names[-1], '').split()))
    paren, equals = head.find('('), head.find('=')
    if paren >= 0 and (equals < 0 or paren < equals):
        before = head[:paren].rstrip()
        match = re.search(r'(~?[A-Za-z_]\w*)$', before)
        if not match or match.group(1) in NOT_NAMES or '::' in before[-len(match.group(1)) - 2:]:
            return None
        if not has_body:
            return None  # a declaration; only definitions collide
        depth, end = 0, paren
        for end in range(paren, len(head)):
            depth += {'(': 1, ')': -1}.get(head[end], 0)
            if depth == 0:
                break
        return ('function', match.group(1), parameter_types(head[paren + 1:end]))
    target = head[:equals] if equals >= 0 else head
    target = re.sub(r'\[[^\]]*\]\s*$', '', target.rstrip())
    names = IDENT.findall(target)
    if not names or names[-1] in SPECIFIERS or names[-1] in NOT_NAMES:
        return None
    return ('variable', names[-1], None)


def scan_source(path):
    """Return (scope, kind, name, signature, line) for file-private namespace-scope definitions."""
    text = strip_code(Path(path).read_text(encoding='utf-8', errors='replace'))
    lines = text.split('\n')
    code = '\n'.join('' if line.lstrip().startswith('#') else line for line in lines)
    # Only namespace bodies are entered; every other brace body is skipped whole,
    # so each statement head seen here is at namespace scope.
    scopes, results, head_start, i = [], [], 0, 0
    while i < len(code):
        c = code[i]
        if c == '{':
            head = code[head_start:i]
            match = re.search(r'(?:^|[;{}\s])(inline\s+)?namespace\s*([\w:]*)\s*$', head)
            if match:
                scopes.append(match.group(2).split('::') if match.group(2) else ['(anonymous)'])
                head_start = i = i + 1
                continue
            record(results, scopes, head, True, code.count('\n', 0, i) + 1)
            # skip the whole body
            body_depth, j = 0, i
            while j < len(code):
                body_depth += {'{': 1, '}': -1}.get(code[j], 0)
                if body_depth == 0:
                    break
                j += 1
            i = j + 1
            # `struct X {...} x;` or `= {...};` continue the same statement
            rest = code[i:i + 80].lstrip()
            if rest.startswith((';', ',')) or re.match(r'[A-Za-z_]\w*\s*[;=,\[]', rest):
                j = code.find(';', i)
                i = len(code) if j < 0 else j + 1
            head_start = i
            continue
        if c == '}':
            if scopes:
                scopes.pop()
            head_start = i + 1
        elif c == ';':
            record(results, scopes, code[head_start:i], False, code.count('\n', 0, i) + 1)
            head_start = i + 1
        i += 1
    for macro in re.finditer(r'^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)((?:\\\n|.)*)', text, re.M):
        name = macro.group(1)
        if not re.search(r'^\s*#\s*undef\s+' + name + r'\b', text[macro.end():], re.M):
            body = ' '.join(macro.group(2).replace('\\\n', ' ').split())  # identical redefinition is legal
            results.append(('#', 'macro', name, body, text.count('\n', 0, macro.start()) + 1))
    return results


def record(results, scopes, head, has_body, line):
    head = ' '.join(head.split())
    found = classify(head, has_body)
    if not found:
        return
    kind, name, signature = found
    flat = [part for scope in scopes for part in scope]
    anonymous = '(anonymous)' in flat
    # Internal or inline linkage: anonymous namespace, static/inline/constexpr, or
    # a namespace-scope const variable. Class and alias names clash in any scope.
    declarator = head.split('=')[0] if kind == 'variable' else head
    specifiers = r'\b(static|inline|constexpr|const)\b' if kind == 'variable' else r'^(template\s*<.*?>\s*)?(static|inline|constexpr)\b'
    private = anonymous or kind in ('type', 'alias') or re.search(specifiers, re.sub(r'\[\[.*?\]\]\s*', '', declarator)) is not None
    if private:
        results.append(('::'.join(flat), kind, name, signature, line))


def conflict(first, second):
    """Whether two same-named definitions from different sources clash in one unit."""
    (_, _, kind_a, sig_a), (_, _, kind_b, sig_b) = first, second
    if kind_a == 'function' and kind_b == 'function':
        return sig_a == sig_b  # different parameter types are overloads
    if kind_a == kind_b and kind_a in ('alias', 'macro'):
        return sig_a != sig_b
    return True


def collisions(batch_sources):
    seen = defaultdict(list)
    for source in batch_sources:
        for scope, kind, name, signature, line in scan_source(source):
            seen[(scope, name, kind == 'macro')].append((source, line, kind, signature))
    for (scope, name, _), places in seen.items():
        clashing = {}
        for index, first in enumerate(places):
            for second in places[index + 1:]:
                if first[0] != second[0] and conflict(first, second):
                    clashing[first[:2]] = first
                    clashing[second[:2]] = second
        if clashing:
            yield scope, name, list(clashing.values())


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('build_dir', type=Path, help='configured build directory with ENABLE_NH_UNITY_BUILD=ON')
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args(argv)
    batches = sorted(args.build_dir.glob('**/CMakeFiles/*.dir/Unity/unity_*_cxx.cxx'))
    if not batches:
        print(f'no Unity batches under {args.build_dir}; configure it with -DENABLE_NH_UNITY_BUILD=ON', file=sys.stderr)
        return 2
    root, found = args.root.resolve(), 0
    for batch in batches:
        # Generated sources (such as Version.cpp) may not exist before a build.
        sources = [source for source in INCLUDE.findall(batch.read_text()) if Path(source).is_file()]
        for scope, name, places in collisions(sources):
            found += 1
            print(f'{batch.relative_to(args.build_dir)}: {scope or "(global)"}::{name}')
            for source, line, kind, signature in places:
                shown = Path(source).resolve()
                shown = shown.relative_to(root) if shown.is_relative_to(root) else shown
                detail = f'({", ".join(signature)})' if kind == 'function' else ''
                print(f'  {kind}{detail} {shown}:{line}')
    print(f'{len(batches)} Unity batches scanned, {found} file-private collision(s)')
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main())
