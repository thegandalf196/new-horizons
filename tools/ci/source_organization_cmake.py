"""Bounded CMake command lexer for physical function/macro spans, not evaluation.

Handles CMake quoted/bracket arguments, bracket/line comments and escapes before
balancing command parentheses. All conditional branches are inventoried.
"""
import re


def bracket_end(text: str, offset: int) -> int | None:
    match = re.match(r'\[(=*)\[', text[offset:])
    if match is None:
        return None
    close = ']' + match[1] + ']'
    end = text.find(close, offset + len(match[0]))
    if end < 0:
        raise ValueError('Unclosed CMake bracket argument/comment')
    return end + len(close)


def skip_token(text: str, offset: int) -> int | None:
    """Return end for a comment, quoted/bracket argument or escaped character."""
    char = text[offset]
    if char == '#':
        end = bracket_end(text, offset + 1)
        if end is not None:
            return end
        end = text.find('\n', offset)
        return len(text) if end < 0 else end
    if char == '[':
        return bracket_end(text, offset)
    if char == '\\':
        return min(offset + 2, len(text))
    if char == '"':
        end = offset + 1
        while end < len(text):
            if text[end] == '\\':
                end += 2
            elif text[end] == '"':
                return end + 1
            else:
                end += 1
        raise ValueError('Unclosed CMake quoted argument')
    return None


def commands(text: str):
    offset = 0
    while offset < len(text):
        skipped = skip_token(text, offset)
        if skipped is not None:
            offset = skipped
            continue
        match = re.match(r'([A-Za-z_][A-Za-z_0-9]*)\s*\(', text[offset:])
        if match is None:
            offset += 1
            continue
        start = offset
        offset += len(match[0])
        argument_start = offset
        depth = 1
        while offset < len(text) and depth:
            skipped = skip_token(text, offset)
            if skipped is not None:
                offset = skipped
                continue
            if text[offset] == '(':
                depth += 1
            elif text[offset] == ')':
                depth -= 1
            offset += 1
        if depth:
            raise ValueError('Unclosed CMake command')
        yield match[1].lower(), start, offset, text[argument_start:offset - 1]


def functions(data: bytes) -> tuple[list[tuple], list[dict]]:
    text = data.decode('utf-8')
    result, stack = [], []
    try:
        for name, start, end, arguments in commands(text):
            if name in {'function', 'macro'}:
                signature = re.sub(r'\s+', ' ', arguments).strip()
                if not signature:
                    raise ValueError('Missing CMake function/macro name')
                stack.append((name, name + '(' + signature + ')', start))
            elif name in {'endfunction', 'endmacro'}:
                if not stack or 'end' + stack[-1][0] != name:
                    raise ValueError('Unbalanced CMake function/macro terminator')
                _, signature, opening = stack.pop()
                scope = '::'.join([*(item[1] for item in stack), signature])
                result.append((scope, text.count('\n', 0, opening) + 1,
                               text.count('\n', 0, end) + 1))
        if stack:
            raise ValueError('Unclosed CMake function/macro')
    except ValueError:
        return [], [{'start': 1, 'end': len(data.splitlines()), 'type': 'cmake-lexical-error'}]
    return sorted(result, key=lambda item: (item[1], item[2], item[0])), []
