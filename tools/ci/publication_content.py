#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Narrow R1/R2 prevention over explicit Git text objects, not a privacy guarantee."""
import argparse
import json
from pathlib import Path
import re
import sys
import tomllib

from git_publication_blobs import InspectionError, read_publication_blobs

MAX_CANDIDATES = 4096
MAX_STRUCTURED_NODES = 100000
ROLES = frozenset({'user', 'assistant', 'system', 'developer', 'tool'})
SESSION_KEYS = frozenset({'session_id', 'conversation_id', 'thread_id'})
START = re.compile(r'^<\|im_start\|>(user|assistant|system|developer|tool)[ \t]*\r?$', re.M)
END = re.compile(r'^<\|im_end\|>[ \t]*\r?$', re.M)
CONVERSATION = re.compile(r'^<<< BEGIN OF CONVERSATION >>>[ \t]*\r?$', re.M)
CONVERSATION_END = re.compile(r'^<<< END OF CONVERSATION >>>[ \t]*\r?$', re.M)
CONVERSATION_ROLE = re.compile(r'^<<< (user|assistant|system|developer|tool) >>>[ \t]*\r?$', re.M)


def frame_lines(text):
    if text.count('<|im_start|>') + text.count('<<< BEGIN OF CONVERSATION >>>') > MAX_CANDIDATES:
        raise InspectionError('frame-marker-limit-exceeded')
    messages = []
    cursor = 0
    for opening in START.finditer(text):
        if opening.start() < cursor:
            continue
        closing = END.search(text, opening.end())
        nested = START.search(text, opening.end())
        if closing and (not nested or closing.start() < nested.start()):
            body = text[opening.end():closing.start()].strip()
            if body:
                messages.append((opening.group(1), opening.start(), closing.end()))
                cursor = closing.end()
    for left, right in zip(messages, messages[1:]):
        if {left[0], right[0]} == {'user', 'assistant'} and not text[left[2]:right[1]].strip():
            return [text.count('\n', 0, left[1]) + 1]
    for opening in CONVERSATION.finditer(text):
        closing = CONVERSATION_END.search(text, opening.end())
        if not closing:
            continue
        segment = text[opening.end():closing.start()]
        roles = list(CONVERSATION_ROLE.finditer(segment))
        populated = set()
        for index, role in enumerate(roles):
            end = roles[index + 1].start() if index + 1 < len(roles) else len(segment)
            if segment[role.end():end].strip():
                populated.add(role.group(1))
        if {'user', 'assistant'} <= populated:
            return [text.count('\n', 0, opening.start()) + 1]
    return []


def session_message(node):
    if not isinstance(node, dict):
        return False
    active_session = any(isinstance(node.get(key), str) and node[key].strip() for key in SESSION_KEYS)
    messages = node.get('messages')
    if not active_session or not isinstance(messages, list):
        return False
    return any(isinstance(message, dict) and isinstance(message.get('role'), str) and message['role'] in ROLES
               and isinstance(message.get('content'), str) and message['content'].strip()
               for message in messages)


def structured_session(value):
    stack, visited = [value], 0
    while stack:
        current = stack.pop()
        visited += 1
        if visited > MAX_STRUCTURED_NODES:
            raise InspectionError('structured-node-limit-exceeded')
        if session_message(current):
            return True
        if isinstance(current, dict):
            stack.extend(current.values())
        elif isinstance(current, list):
            stack.extend(current)
    return False


def structured_line(text):
    try:
        value = json.loads(text)
    except RecursionError:
        raise InspectionError('structured-nesting-unsupported') from None
    except ValueError:
        value = None
    if value is not None:
        return 1 if structured_session(value) else None
    try:
        value = tomllib.loads(text)
    except RecursionError:
        raise InspectionError('structured-nesting-unsupported') from None
    except ValueError:
        value = None
    if value is not None and structured_session(value):
        return 1
    decoder = json.JSONDecoder()
    position, candidates = 0, 0
    while True:
        opening = text.find('{', position)
        if opening < 0:
            return None
        candidates += 1
        if candidates > MAX_CANDIDATES:
            raise InspectionError('structured-candidate-limit-exceeded')
        try:
            value, end = decoder.raw_decode(text, opening)
        except RecursionError:
            raise InspectionError('structured-nesting-unsupported') from None
        except ValueError:
            position = opening + 1
            continue
        if structured_session(value):
            return text.count('\n', 0, opening) + 1
        position = end


def find_content_frames(text):
    findings = [{'rule': 'raw-role-envelope', 'line': line} for line in frame_lines(text)]
    line = structured_line(text)
    if line is not None:
        findings.append({'rule': 'session-role-messages', 'line': line})
    return findings


def inspect(root, *, staged=False, revision=None):
    selected = read_publication_blobs(root, staged=staged, revision=revision)
    findings, issues = [], list(selected['issues'])
    for blob in selected['blobs']:
        try:
            findings.extend({'path': blob['path'], **finding} for finding in find_content_frames(blob['text']))
        except InspectionError as error:
            issues.append({'path': blob['path'], 'code': str(error)})
        except RecursionError:
            issues.append({'path': blob['path'], 'code': 'structured-nesting-unsupported'})
    return {key: value for key, value in selected.items() if key != 'blobs'} | {
        'issues': issues, 'findings': findings, 'implemented_rules': ['R1', 'R2'],
        'unimplemented': ['R3-authoring-request', 'credentials-use-separate-credential-content-entrypoint'],
        'limits': 'Finite text suffix/name scope and framing dialects; not an exhaustive privacy verdict.'}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--staged', action='store_true')
    mode.add_argument('--revision')
    args = parser.parse_args(argv)
    try:
        result = inspect(args.root, staged=args.staged, revision=args.revision)
    except (InspectionError, OSError, ValueError, UnicodeError):
        result = {'issues': [{'code': 'publication-inspection-failed'}], 'findings': []}
    print(json.dumps(result, sort_keys=True, ensure_ascii=True))
    return 2 if result['issues'] else int(bool(result['findings']))


if __name__ == '__main__':
    raise SystemExit(main())
