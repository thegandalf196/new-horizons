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


def cpp_prefix_has_type(declaration, start: int, end: int, data: bytes, parser=None) -> bool:
    """Confirm a prefix marker is followed by a separately parsed actual type."""
    if parser is None:
        from tree_sitter import Language, Parser
        import tree_sitter_cpp
        parser = Parser(Language(tree_sitter_cpp.language()))
    offset = declaration.start_byte
    fragment = bytearray(data[offset:declaration.end_byte])
    fragment[start - offset:end - offset] = b' ' * (end - start)
    tree = parser.parse(bytes(fragment))
    node = next(iter(tree.root_node.named_children), None)
    if node is None or node.type not in {'declaration', 'function_definition'}:
        return False
    actual_type = node.child_by_field_name('type')
    declarator = node.child_by_field_name('declarator')
    return (actual_type is not None and declarator is not None
            and end - offset <= actual_type.start_byte < actual_type.end_byte <= declarator.start_byte)


def cpp_annotation_position(marker: bytes, start: int, end: int, ancestors, data: bytes, parser=None) -> bool:
    """Require a distinct real type/name; a marker spelling can itself be a type."""
    if marker in {b'signals', b'slots'}:
        return True  # Access-label spelling is checked by the caller.
    if marker.startswith(b'VCMI_LIB_NAMESPACE_'):
        following = re.sub(rb'\s+|//[^\n]*|/\*[\s\S]*?\*/', b'', data[end:])
        if marker.endswith(b'BEGIN'):
            return re.match(rb'(?:class|struct|template|namespace)\b', re.sub(
                rb'//[^\n]*|/\*[\s\S]*?\*/', b'', data[end:]).lstrip()) is not None
        return not following
    if marker == b'PACKED_STRUCT_BEGIN':
        return re.match(rb'\s*struct\s+[A-Za-z_]\w*\s*\{', data[end:]) is not None
    linkage = {b'DLL_LINKAGE', b'DLL_EXPORT', b'VCMIQT_LINKAGE'}
    if marker in linkage and re.search(rb'\b(?:class|struct)[ \t]+$', data[:start]):
        following = re.match(rb'\s*([A-Za-z_]\w*)', data[end:])
        return following is not None and following[1] != b'final'
    if marker == b'Q_OBJECT' and re.match(
            rb'\s*(?:(?:public|protected|private)\s*(?:slots\s*)?|signals)\s*:', data[end:]):
        return True
    if marker == b'Q_OBJECT' and re.match(
            rb'\s*(?:class|struct|enum(?:\s+class)?)\s+[A-Za-z_]\w*\s*[:{]', data[end:]):
        return True
    if marker == b'Q_OBJECT' and re.match(rb'\s*#', data[end:]):
        fields = next((node for node in ancestors if node.type == 'field_declaration_list'), None)
        following = next((node for node in fields.named_children if node.start_byte >= end), None) if fields else None
        if following and following.type.startswith('preproc_'):
            field = next((node for node in descendants(following) if node.type == 'field_declaration'), None)
            if field and field.child_by_field_name('type'):
                return True  # The following branch has its own distinct field type.
    declaration = next((node for node in ancestors
                        if node.type in {'declaration', 'field_declaration', 'function_definition'}), None)
    if declaration is None:
        return False
    real_type = declaration.child_by_field_name('type')
    declarator = declaration.child_by_field_name('declarator')
    if real_type is None or declarator is None:
        return False
    prefix = linkage | {b'Q_OBJECT', b'Q_INVOKABLE'}
    if marker in prefix and (real_type.start_byte == start and real_type.end_byte == end
                             or end <= real_type.start_byte):
        return cpp_prefix_has_type(declaration, start, end, data, parser)
    name = declarator
    while True:
        child = name.child_by_field_name('declarator') or name.child_by_field_name('name')
        if child is None:
            break
        name = child
    if not any((node.type == 'ERROR' or node.is_missing)
               and start <= node.start_byte <= node.end_byte <= name.end_byte
               for node in descendants(declaration)):
        return False  # A valid type/qualified-name use needs no annotation recovery.
    if real_type.end_byte <= start and end <= declarator.start_byte:
        return True  # e.g. void SDLCALL callback(...), not SDLCALL as return type.
    return False


def cpp_measurement_source(data: bytes, root, parser=None) -> bytes:
    """Mask known nonexecutable annotations only in declaration/Qt-label context.

    The initial concrete tree supplies context even when an annotation caused
    recovery. Literals, comments, preprocessor definitions and expression uses
    remain byte-identical. This is not macro expansion or C++ semantic analysis.
    """
    markers = (rb'\b(?:DLL_LINKAGE|DLL_EXPORT|VCMI_LIB_NAMESPACE_BEGIN|VCMI_LIB_NAMESPACE_END|'
               rb'Q_OBJECT|Q_INVOKABLE|ZCALLBACK|CALLBACK|WINAPI|SDLCALL|VCMIQT_LINKAGE|'
               rb'MAIN_EXPORT|PACKED_STRUCT_BEGIN|PACKED_STRUCT_END|signals|slots)\b')
    opaque = {'comment', 'string_literal', 'raw_string_literal', 'char_literal',
              'preproc_def', 'preproc_function_def', 'preproc_include', 'preproc_call'}
    expressions = {'return_statement', 'expression_statement', 'call_expression',
                   'init_declarator', 'parameter_declaration', 'optional_parameter_declaration'}
    declarations = {'declaration', 'field_declaration', 'class_specifier', 'struct_specifier'}
    result = bytearray(data)
    for match in re.finditer(markers, data):
        start, end = match.span()
        node = root.named_descendant_for_byte_range(start, end)
        ancestors = []
        while node:
            ancestors.append(node)
            node = node.parent
        if any(node.type in opaque for node in ancestors):
            continue
        if any(node.type in {'identifier', 'field_identifier'} and node.parent
               and node.parent.child_by_field_name('declarator') == node for node in ancestors):
            continue
        line_start = data.rfind(b'\n', 0, start) + 1
        if data[line_start:start].lstrip().startswith(b'#'):
            continue
        declaration = False
        for node in ancestors:
            if node.type in expressions:
                break
            if node.type == 'function_definition':
                body = node.child_by_field_name('body')
                declaration = body is not None and end <= body.start_byte
                break
            if node.type in declarations:
                declaration = True
                break
        marker = match[0]
        namespace_end = (marker == b'VCMI_LIB_NAMESPACE_END'
                         and not any(node.type == 'compound_statement' for node in ancestors))
        if not declaration and not namespace_end:
            continue
        if not cpp_annotation_position(marker, start, end, ancestors, data, parser):
            continue
        if marker == b'slots':
            if not re.search(rb'\b(?:public|protected|private)[ \t]+$', data[line_start:start]):
                continue
            if not re.match(rb'[ \t]*:', data[end:]):
                continue
        if marker == b'signals':
            if not re.search(rb'(?:^|[;{}])[ \t]*$', data[line_start:start]):
                continue
            if not re.match(rb'[ \t]*:', data[end:]):
                continue
        result[start:end] = b'public ' if marker == b'signals' else b' ' * (end - start)
    return bytes(result)


def remember_cpp_function_starts(root, starts) -> None:
    for node in descendants(root):
        body = node.child_by_field_name('body')
        if node.type == 'function_definition' and body is not None:
            starts.setdefault((body.start_byte, body.end_byte), set()).add(
                (node.start_point.row + 1, node.start_byte))


def original_cpp_function_start(data: bytes, node, body, starts) -> int:
    start = node.start_point.row + 1
    annotation = (rb'(?:\s|DLL_LINKAGE\b|DLL_EXPORT\b|VCMIQT_LINKAGE\b|Q_INVOKABLE\b|'
                  rb'MAIN_EXPORT\b|CALLBACK\b|WINAPI\b|SDLCALL\b|ZCALLBACK\b|'
                  rb'//[^\n]*(?:\n|$)|/\*[\s\S]*?\*/)+')
    for line, offset in starts.get((body.start_byte, body.end_byte), ()):
        if offset < node.start_byte and re.fullmatch(annotation, data[offset:node.start_byte]):
            start = min(start, line)
    return start


def tree_functions(data: bytes, lang: str) -> tuple[list[dict], list[dict]]:
    from tree_sitter import Language, Parser
    choices = ('objc', 'cpp') if lang == 'objc++' or (lang == 'cpp' and b'@interface' in data) else (lang,)
    trees = []
    for choice in choices:
        module = importlib.import_module('tree_sitter_' + choice)
        parser = Parser(Language(module.language()))
        tree = parser.parse(data)
        raw_starts = {}
        if choice in {'cpp', 'objc'}:
            parsed_data = data
            # Recover an outer class annotation before examining its Qt labels.
            # A bounded fixed point cannot erase arbitrary unresolved syntax.
            for _ in range(3):
                if choice == 'cpp':
                    remember_cpp_function_starts(tree.root_node, raw_starts)
                normalized = cpp_measurement_source(parsed_data, tree.root_node, parser)
                if normalized == parsed_data:
                    break
                parsed_data = normalized
                tree = parser.parse(parsed_data)
        errors = [n for n in descendants(tree.root_node) if n.type == 'ERROR' or n.is_missing]
        trees.append((tree, errors, raw_starts))
    tree, errors, raw_starts = min(trees, key=lambda entry: sum(n.end_byte - n.start_byte + 1 for n in entry[1]))
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
        if node.type == 'function_definition':
            # An annotation on its own line still belongs to the signature.
            start = original_cpp_function_start(data, node, body, raw_starts)
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
    # Measure all first-party code; growth-policy units remain production-only.
    if result['classification'] not in {'production', 'test'}:
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
