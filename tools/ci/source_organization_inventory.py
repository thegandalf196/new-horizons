#!/usr/bin/env python3
"""Physical source inventory. Syntax trees are measurements, not compiler validation."""
from __future__ import annotations

import ast
import hashlib
import importlib
import re
import subprocess
from pathlib import Path, PurePosixPath


LANGUAGES = {
    '.cpp': 'cpp', '.h': 'cpp', '.hpp': 'cpp', '.cc': 'cpp', '.cxx': 'cpp',
    '.hxx': 'cpp', '.ipp': 'cpp', '.tpp': 'cpp', '.inc': 'cpp', '.c': 'cpp',
    '.py': 'python', '.lua': 'lua', '.java': 'java', '.mjs': 'javascript',
    '.js': 'javascript', '.m': 'objc', '.mm': 'objc++', '.sh': 'bash',
    '.bash': 'bash', '.ps1': 'powershell', '.cmake': 'cmake', '.gradle': 'groovy',
    '.nix': 'nix', '.cmd': 'batch', '.bat': 'batch',
    '.iss': 'inno', '.nsi': 'nsis', '.nsh': 'nsis', '.html': 'html', '.css': 'css',
    '.spec': 'rpm', '.sql': 'sql', '.mk': 'make',
}
PARSED = {'cpp', 'lua', 'java', 'javascript', 'objc', 'objc++', 'bash', 'python', 'cmake'}
CALLABLES = {
    'function_definition', 'lambda_expression', 'function_declaration',
    'function_expression', 'arrow_function', 'generator_function',
    'generator_function_declaration', 'method_definition', 'method_declaration',
    'constructor_declaration', 'compact_constructor_declaration',
    'block_literal',
}
SCOPES = {'namespace_definition', 'class_specifier', 'struct_specifier',
          'class_declaration', 'interface_declaration', 'class_definition',
          'class_implementation'}


def git(root: Path, *args: str) -> bytes:
    return subprocess.check_output(['git', '-C', str(root), *args])


def source_paths(root: Path, revision: str | None = None, include_untracked: bool = False) -> list[str]:
    """Tracked membership by default; snapshots use their pinned Git tree."""
    args = ('ls-tree', '-r', '--name-only', '-z', revision) if revision else (
        ('ls-files', '-z', '--cached', '--others', '--exclude-standard') if include_untracked
        else ('ls-files', '-z', '--cached'))
    return sorted(set(git(root, *args).decode().split('\0')) - {''})


def language(path: str) -> str | None:
    name = PurePosixPath(path)
    if (path.startswith('.github/workflows/') or '/.github/workflows/' in path) and name.suffix in {'.yml', '.yaml'}:
        return 'workflow-yaml'
    if name.name == 'CMakeLists.txt':
        return 'cmake'
    if name.name in {'gradlew', 'vcmibuilder'}:
        return 'bash'
    if name.name in {'Makefile', 'GNUmakefile', 'makefile'} or (
            path.startswith('packaging/') and name.name == 'rules'):
        return 'make'
    if path == 'CI/NSIS.template.in':
        return 'nsis'
    if path == 'xcode/compiler-launcher.in':
        return 'bash'
    if name.suffix == '.in':
        return language(path[:-3])
    return LANGUAGES.get(name.suffix)


def classification(path: str) -> str:
    """Explicit path classes; no generated-name heuristic for handwritten code."""
    parts = PurePosixPath(path).parts
    if path.startswith(('dependencies/', 'client/lib/discord-presence/',
                        'launcher/lib/innoextract/', 'test/googletest/')):
        return 'vendor'
    # Upstream-authored build helpers, preserving their embedded license notices.
    if path in {'android/gradlew', 'android/gradlew.bat',
                'cmake_modules/GetGitRevisionDescription.cmake',
                'cmake_modules/GetGitRevisionDescription.cmake.in'}:
        return 'vendor'
    if 'test' in parts or 'tests' in parts or path in {
            'tools/ci/test_windows_ai_compile_flags.py',
            'assets/new-horizons/sorcery-art/template-compositor/test_compose.py'}:
        return 'test'
    if path.startswith('docs/modders/Lua_Reference/'):
        return 'documentation-example'
    return 'production'


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def span(symbol: str, start: int, end: int, data: bytes) -> dict:
    return {'symbol': symbol, 'start': start, 'end': end, 'lines': end - start + 1,
            'sha256': digest(b'\n'.join(data.split(b'\n')[start - 1:end]))}


def python_functions(data: bytes) -> tuple[list[dict], list[dict]]:
    try:
        tree = ast.parse(data)
    except SyntaxError as error:
        return [], [{'start': error.lineno or 1, 'end': error.end_lineno or 1,
                     'type': 'python-syntax-error'}]
    result = []

    def visit(node: ast.AST, scope: tuple[str, ...] = ()) -> None:
        nested = scope
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            nested = (*scope, node.name)
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            result.append(span('.'.join(nested), node.lineno, node.end_lineno, data))
        elif isinstance(node, ast.Lambda):
            result.append(span('.'.join((*scope, '<lambda>')), node.lineno, node.end_lineno, data))
        for child in ast.iter_child_nodes(node):
            visit(child, nested)

    visit(tree)
    return result, []


def descendants(node):
    stack = [node]
    while stack:
        current = stack.pop()
        yield current
        stack.extend(reversed(current.named_children))


def node_text(node, data: bytes) -> str:
    return re.sub(r'\s+', ' ', data[node.start_byte:node.end_byte].decode('utf-8', 'replace')).strip()


def tree_functions(data: bytes, lang: str) -> tuple[list[dict], list[dict]]:
    from tree_sitter import Language, Parser
    choices = ('objc', 'cpp') if lang == 'objc++' or (lang == 'cpp' and b'@interface' in data) else (lang,)
    # These linkage/Qt markers have no executable body. Preserve every byte offset
    # and newline; never preprocess conditional branches or expand gameplay macros.
    markers = rb'\b(?:DLL_LINKAGE|DLL_EXPORT|VCMI_LIB_NAMESPACE_BEGIN|VCMI_LIB_NAMESPACE_END|Q_OBJECT|Q_INVOKABLE|ZCALLBACK)\b'
    parsed_data = re.sub(markers, lambda match: b' ' * len(match[0]), data)
    parsed_data = re.sub(rb'\bsignals(?=\s*:)', b'public ', parsed_data)
    parsed_data = re.sub(rb'\bslots(?=\s*:)', b'     ', parsed_data)
    trees = []
    for choice in choices:
        module = importlib.import_module('tree_sitter_' + choice)
        tree = Parser(Language(module.language())).parse(parsed_data)
        errors = [n for n in descendants(tree.root_node) if n.type == 'ERROR' or n.is_missing]
        trees.append((tree, errors))
    tree, errors = min(trees, key=lambda entry: sum(n.end_byte - n.start_byte + 1 for n in entry[1]))
    functions = []
    for node in descendants(tree.root_node):
        if node.type == 'preproc_function_def':
            name = node.child_by_field_name('name')
            end = node.end_point.row + (1 if node.end_point.column else 0)
            functions.append(span('#define ' + node_text(name, data), node.start_point.row + 1, end, data))
            continue
        if node.type not in CALLABLES:
            continue
        body = node.child_by_field_name('body')
        if body is None and node.type in {'method_definition', 'block_literal'}:
            body = next((child for child in node.named_children if child.type == 'compound_statement'), None)
        # Declarations without a body are not functions with measured implementations.
        if body is None:
            continue
        scope = []
        parent = node.parent
        while parent:
            if parent.type in SCOPES:
                name = parent.child_by_field_name('name')
                if name is None and parent.type == 'class_implementation':
                    name = next((child for child in parent.named_children if child.type == 'identifier'), None)
                if name:
                    scope.append(node_text(name, data))
            elif parent.type in CALLABLES:
                name = parent.child_by_field_name('name') or parent.child_by_field_name('declarator')
                scope.append(node_text(name, data) if name else '<anonymous>')
            parent = parent.parent
        signature = re.sub(r'\s+', ' ', data[node.start_byte:body.start_byte].decode('utf-8', 'replace')).strip()
        symbol = '::'.join([*reversed(scope), signature])
        start = node.start_point.row + 1
        if node.parent and node.parent.type == 'template_declaration':
            start = node.parent.start_point.row + 1
        end = node.end_point.row + (1 if node.end_point.column else 0)
        functions.append(span(symbol, start, end, data))
    diagnostics = [{'start': n.start_point.row + 1, 'end': n.end_point.row + 1,
                    'type': n.type if not n.is_missing else 'missing:' + n.type} for n in errors]
    return functions, diagnostics


def measure(path: str, data: bytes) -> dict:
    lang = language(path)
    result = {'path': path, 'language': lang, 'classification': classification(path),
              'lines': data.count(b'\n') + int(bool(data) and not data.endswith(b'\n')),
              'sha256': digest(data), 'functions': [],
              'diagnostics': [], 'measurement': 'parser' if lang in PARSED else 'manual-required'}
    if result['classification'] != 'production':
        result['measurement'] = 'excluded'
        return result
    if lang in PARSED:
        if lang == 'cmake':
            from source_organization_cmake import functions
            found, result['diagnostics'] = functions(data)
            result['functions'] = [span(symbol, start, end, data) for symbol, start, end in found]
            result['measurement'] = 'balanced-cmake-commands'
        else:
            result['functions'], result['diagnostics'] = (
                python_functions(data) if lang == 'python' else tree_functions(data, lang))
    # Ordinals disambiguate anonymous callbacks / same-signature conditional definitions.
    counts = {}
    for function in result['functions']:
        symbol = function['symbol']
        counts[symbol] = counts.get(symbol, 0) + 1
        function['symbol'] += '#' + str(counts[symbol])
    return result


def inventory(root: Path, revision: str | None = None, include_untracked: bool = False) -> dict:
    files = []
    tracked = set(source_paths(root, revision))
    untracked = [] if revision else [path for path in source_paths(root, include_untracked=True)
                                     if path not in tracked and language(path) is not None]
    tree_modes = {} if not revision else {
        entry.split(b'\t', 1)[1].decode(): entry.split(b' ', 1)[0]
        for entry in git(root, 'ls-tree', '-r', '-z', revision).split(b'\0') if entry}
    for path in source_paths(root, revision, include_untracked):
        if language(path) is None:
            continue
        if revision:
            if tree_modes[path] == b'120000':
                raise ValueError('Source symlink requires explicit review: ' + path)
            data = git(root, 'show', revision + ':' + path)
        else:
            target = root / path
            if target.is_symlink():
                raise ValueError('Source symlink requires explicit review: ' + path)
            if not target.exists():
                continue
            data = target.read_bytes()
        file = measure(path, data)
        file['provenance'] = 'git-tree' if revision else ('tracked' if path in tracked else 'untracked')
        files.append(file)
    return {'schema': 1, 'selection': 'git-tree' if revision else (
        'tracked-and-untracked' if include_untracked else 'tracked-working-tree'),
        'untracked_source_paths': untracked, 'files': files}
