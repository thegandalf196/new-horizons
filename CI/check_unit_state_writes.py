#!/usr/bin/env python3

"""Reject writes to a battle unit's plain state that bypass its revision.

CUnitState keeps a runtime-only plain-state revision (lib/battle/UnitStateRevision.h)
that caches can key on. That is only sound while every write of a covered field
changes the revision, so the fields are private and change through mutators:
setPosition, setGhost, setGhostPending, setHealth and the one-expression
editHealth()-> handle (and, inside CUnitState, setPhantomIntegrity and
editOriginalHealth()->). The compiler rejects writes from other classes. This
check covers the code the compiler cannot police:

* the covered fields must stay private, and CUnitState's friends must be known;
* CUnitState member functions and the friend classes' files must not assign the
  covered fields, swap them, bind a non-const reference to them or call a
  mutating CHealth method on a covered ledger, except inside the mutators;
* a member function that writes a battle-form field (battleForm*) or
  phantomInitialIntegrity must also change the revision in the same function;
* an editHealth()/editOriginalHealth() handle must be used at once with `->`,
  never kept in a variable, so each health change bumps before and after.
"""

from __future__ import annotations

from pathlib import Path
import re
import sys

from tree_sitter import Language, Parser
import tree_sitter_cpp

from normalize_relative_includes import tracked_source_files


CPP = Language(tree_sitter_cpp.language())
HEADER = "lib/battle/CUnitState.h"
HEALTH_HEADER = "lib/battle/CHealth.h"
STRICT_SCALARS = {"position", "ghost", "ghostPending", "phantomIntegrity"}
LEDGERS = {"health", "battleFormOriginalHealth"}
PRIVATE_FIELDS = STRICT_SCALARS | LEDGERS | {"stateRevision"}
FORM_FIELD = re.compile(r"^(battleForm\w+|phantomInitialIntegrity)$")
MUTATORS = {"setHealth", "setGhost", "setGhostPending", "setPosition", "setPhantomIntegrity",
            "editHealth", "editOriginalHealth"}
EDIT_HANDLES = {"editHealth", "editOriginalHealth"}
BUMPS = ("StateRevisionScope", "stateRevision.bump()", "onBattleFormChanged()",
         "editHealth()", "editOriginalHealth()", "setHealth(", "setGhost(", "setGhostPending(",
         "setPosition(", "setPhantomIntegrity(")
# Friend classes of CUnitState and the files that implement them.
FRIEND_FILES = {
    "UnitStateValueFields": ("lib/battle/UnitStateValueFields.h",),
    "PreparedStackStateTransfer": ("lib/battle/PreparedStackStateTransfer.h", "lib/battle/PreparedStackStateTransfer.cpp"),
    "PreparedBattleUnitRoots": ("lib/battle/PreparedBattleUnitRoots.h", "lib/battle/PreparedBattleUnitRoots.cpp",
                                "lib/battle/PreparedBattleUnitRootsState.h"),
}
LINKAGE_MACROS = re.compile(rb"\b(?:DLL_LINKAGE|DLL_EXPORT|DLL_IMPORT|ELF_VISIBILITY)\b")
# The JSON (de)serializer fills fields by reference; serializeJson ends with setPosition.
OWNER_SCOPES = {"CUnitState"}


def parse(source: bytes):
    blanked = LINKAGE_MACROS.sub(lambda match: b" " * len(match.group(0)), source)
    return Parser(CPP).parse(blanked)


def text(node) -> str:
    return node.text.decode("utf-8", "replace")


def walk(node):
    stack = [node]
    while stack:
        current = stack.pop()
        yield current
        stack.extend(reversed(current.children))


def find_class(root, name):
    for node in walk(root):
        if node.type in {"class_specifier", "struct_specifier"}:
            named = node.child_by_field_name("name")
            body = node.child_by_field_name("body")
            if named is not None and body is not None and text(named) == name:
                return body
    return None


def declarator_names(node):
    names = []
    for child in walk(node):
        if child.type in {"field_identifier", "identifier"} and child.parent is not None \
                and child.parent.type not in {"function_declarator", "parameter_declaration", "call_expression",
                                              "argument_list", "qualified_identifier"}:
            names.append(text(child))
    return names


def class_fields(body):
    """Yield (access, field names, friend name) for the direct members of a class body."""
    access = "private"
    for member in body.named_children:
        if member.type == "access_specifier":
            access = text(member).rstrip(":").strip()
        elif member.type == "field_declaration":
            if any(child.type == "function_declarator" for child in walk(member)):
                continue
            names = [text(d) if d.type == "field_identifier" else declarator_names(d)[-1]
                     for d in member.children_by_field_name("declarator")]
            yield access, names, None
        elif member.type == "friend_declaration":
            named = [text(child) for child in walk(member) if child.type == "type_identifier"]
            yield access, [], named[-1] if named else None


def health_mutators(source: bytes) -> set[str]:
    body = find_class(parse(source).root_node, "CHealth")
    found = set()
    if body is None:
        return found
    for member in walk(body):
        if member.type != "function_declarator":
            continue
        name = member.child_by_field_name("declarator")
        qualifiers = [text(child) for child in member.children if child.type == "type_qualifier"]
        if name is None or "const" in qualifiers or name.type not in {"field_identifier", "identifier"}:
            continue
        if text(name) not in {"CHealth", "detached"}:
            found.add(text(name))
    return found


def header_issues(source: bytes) -> list[tuple[int, str]]:
    body = find_class(parse(source).root_node, "CUnitState")
    if body is None:
        return [(1, "class CUnitState not found")]
    issues, seen, friends = [], set(), set()
    for access, names, friend in class_fields(body):
        if friend:
            friends.add(friend)
        for name in names:
            if name in PRIVATE_FIELDS:
                seen.add(name)
                if access != "private":
                    issues.append((body.start_point.row + 1, f"CUnitState::{name} must stay private"))
    for missing in sorted(PRIVATE_FIELDS - seen):
        issues.append((body.start_point.row + 1, f"CUnitState::{missing} declaration not found"))
    for unknown in sorted(friends - FRIEND_FILES.keys()):
        issues.append((body.start_point.row + 1,
                       f"new CUnitState friend {unknown}: add its files to FRIEND_FILES in this check"))
    return issues


def function_name(definition):
    declarator = definition.child_by_field_name("declarator")
    while declarator is not None and declarator.type != "function_declarator":
        declarator = declarator.child_by_field_name("declarator")
    if declarator is None:
        return None, None
    name = declarator.child_by_field_name("declarator")
    if name is None:
        return None, None
    if name.type == "qualified_identifier":
        scope = name.child_by_field_name("scope")
        inner = name.child_by_field_name("name")
        return (text(scope).split("<")[0] if scope is not None else None), (text(inner) if inner is not None else None)
    return None, text(name)


def inside_class(node, name):
    parent = node.parent
    while parent is not None:
        if parent.type in {"class_specifier", "struct_specifier"}:
            named = parent.child_by_field_name("name")
            return named is not None and text(named) == name
        parent = parent.parent
    return False


def covered_name(node):
    """The covered field an lvalue expression names, if any."""
    while node is not None and node.type in {"parenthesized_expression"}:
        node = node.named_children[0] if node.named_children else None
    if node is None:
        return None
    if node.type == "identifier":
        return text(node)
    if node.type == "field_expression":
        field = node.child_by_field_name("field")
        return text(field) if field is not None else None
    return None


def write_issues(body, mutating: set[str]) -> tuple[list[tuple[int, str]], bool]:
    issues, writes_form = [], False
    for node in walk(body):
        line = node.start_point.row + 1
        if node.type in {"assignment_expression", "update_expression"}:
            target = node.child_by_field_name("left") or node.child_by_field_name("argument")
            name = covered_name(target)
            if name in STRICT_SCALARS or name in LEDGERS:
                issues.append((line, f"direct write of {name}; use its CUnitState mutator"))
            elif name and FORM_FIELD.match(name):
                writes_form = True
        elif node.type == "call_expression":
            function = node.child_by_field_name("function")
            arguments = node.child_by_field_name("arguments")
            if function is not None and function.type == "field_expression":
                method = function.child_by_field_name("field")
                ledger = covered_name(function.child_by_field_name("argument"))
                if ledger in LEDGERS and method is not None and text(method) in mutating:
                    issues.append((line, f"{ledger}.{text(method)}() bypasses the revision; use editHealth()->"))
            called = text(function).split("::")[-1] if function is not None else ""
            if called == "swap" and arguments is not None:
                swapped = [covered_name(argument) for argument in arguments.named_children]
                for name in [name for name in swapped if name in STRICT_SCALARS | LEDGERS][:1]:
                    issues.append((line, f"swap of {name} bypasses the revision"))
        elif node.type == "init_declarator":
            declarator = node.child_by_field_name("declarator")
            value = node.child_by_field_name("value")
            declaration = node.parent
            is_const = declaration is not None and any(
                child.type == "type_qualifier" and text(child) == "const" for child in declaration.children)
            if declarator is not None and declarator.type == "reference_declarator" and not is_const \
                    and covered_name(value) in STRICT_SCALARS | LEDGERS:
                issues.append((line, f"non-const reference to {covered_name(value)} bypasses the revision"))
    return issues, writes_form


def handle_issues(root) -> list[tuple[int, str]]:
    issues = []
    for node in walk(root):
        if node.type != "call_expression":
            continue
        function = node.child_by_field_name("function")
        if function is None:
            continue
        name = function.child_by_field_name("field") if function.type == "field_expression" else function
        if name is None or text(name) not in EDIT_HANDLES:
            continue
        parent = node.parent
        operator = parent.child_by_field_name("operator") if parent is not None else None
        if parent is None or parent.type != "field_expression" or operator is None or text(operator) != "->" \
                or parent.child_by_field_name("argument") != node:
            if function_definition_named(node, EDIT_HANDLES):
                continue
            issues.append((node.start_point.row + 1, f"use {text(name)}() at once with ->; never keep the handle"))
    return issues


def function_definition_named(node, names):
    parent = node.parent
    while parent is not None:
        if parent.type == "function_definition":
            return function_name(parent)[1] in names
        parent = parent.parent
    return False


def file_issues(relative: str, source: bytes, mutating: set[str]) -> list[tuple[int, str]]:
    if b"health" not in source and b"position" not in source and b"ghost" not in source \
            and b"battleForm" not in source and b"phantom" not in source and b"editHealth" not in source \
            and b"editOriginalHealth" not in source:
        return []
    root = parse(source).root_node
    issues = handle_issues(root) if b"editHealth" in source or b"editOriginalHealth" in source else []
    friend_file = any(relative in files for files in FRIEND_FILES.values())
    for node in walk(root):
        if node.type != "function_definition":
            continue
        scope, name = function_name(node)
        member = scope in OWNER_SCOPES or (scope is None and inside_class(node, "CUnitState"))
        if not (member or friend_file) or name in MUTATORS:
            continue
        body = node.child_by_field_name("body")
        if body is None:
            continue
        found, writes_form = write_issues(body, mutating)
        issues += found
        if writes_form and not any(bump in text(body) for bump in BUMPS):
            issues.append((node.start_point.row + 1,
                           f"{name} writes battle-form or Phantom state without changing the revision"))
    return issues


def main(repo: Path | None = None) -> int:
    repo = repo or Path(__file__).resolve().parents[1]
    mutating = health_mutators((repo / HEALTH_HEADER).read_bytes())
    issues = [(HEADER, line, message) for line, message in header_issues((repo / HEADER).read_bytes())]
    if not mutating:
        issues.append((HEALTH_HEADER, 1, "no mutating CHealth methods found"))
    for path in tracked_source_files(repo):
        relative = path.relative_to(repo).as_posix()
        if relative.startswith(("dependencies/", "test/googletest/")):
            continue
        issues += [(relative, line, message) for line, message in file_issues(relative, path.read_bytes(), mutating)]
    for path, line, message in issues:
        print(f"{path}:{line}: {message}")
    print(f"Unit plain-state write violations: {len(issues)}")
    return 1 if issues else 0


if __name__ == "__main__":
    sys.exit(main())
