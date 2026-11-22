#!/usr/bin/env python3

"""Reject range-for loops that dereference a temporary bonus-list owner.

The same repository check step also runs the general temporary-lifetime guard in
check_temporary_lifetimes.py.
"""

from pathlib import Path
import re

from tree_sitter import Language, Parser, Query, QueryCursor
import tree_sitter_cpp

from normalize_relative_includes import tracked_source_files
import check_temporary_lifetimes


BONUS_QUERIES = {b"getBonuses", b"getAllBonuses", b"getBonusesOfType"}
CANDIDATE = re.compile(rb"\bfor\s*\(")
CPP = Language(tree_sitter_cpp.language())
RANGE_FOR = Query(CPP, "(for_range_loop right: (_) @range)")


def unparenthesized(node):
    while node is not None and node.type == "parenthesized_expression":
        children = [child for child in node.named_children if child.type != "comment"]
        if len(children) != 1:
            return None
        node = children[0]
    return node


def called_name(node):
    if node is None:
        return None
    if node.type in {"identifier", "field_identifier"}:
        return node.text
    for field in ("field", "name"):
        name = node.child_by_field_name(field)
        if name is not None:
            return called_name(name)
    return None


def unsafe_loop_lines(source, parser):
    if not any(query in source for query in BONUS_QUERIES) or not CANDIDATE.search(source):
        return []
    tree = parser.parse(source)
    matches = []
    for initializer in QueryCursor(RANGE_FOR).captures(tree.root_node).get("range", []):
        pointer = unparenthesized(initializer)
        if pointer is None or pointer.type != "pointer_expression" or pointer.children[0].type != "*":
            continue
        argument = unparenthesized(pointer.child_by_field_name("argument"))
        if argument is not None and argument.type == "call_expression":
            if called_name(argument.child_by_field_name("function")) in BONUS_QUERIES:
                matches.append(initializer.parent.start_point.row + 1)
    return sorted(matches)


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = Parser(CPP)
    issues = [
        (path.relative_to(repo), line)
        for path in tracked_source_files(repo)
        for line in unsafe_loop_lines(path.read_bytes(), parser)
    ]
    for path, line in issues:
        print(f"{path}:{line}: keep the bonus-list owner alive with a range-for initializer")
    print(f"Bonus-list lifetime violations: {len(issues)}")
    temporaries = check_temporary_lifetimes.main()
    return 1 if issues or temporaries else 0


if __name__ == "__main__":
    raise SystemExit(main())
