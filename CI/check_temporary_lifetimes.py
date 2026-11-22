#!/usr/bin/env python3

"""Reject non-owning views, pointers and references bound to a temporary.

A function that returns std::string, std::vector or JsonNode by value returns a
temporary that dies at the end of the full expression. A std::string_view,
std::span, `.c_str()` pointer or a reference into it then dangles. GCC's library
often leaves the freed bytes in place, so such code can pass every Linux test
and still fail on Windows: Thunar's Stonecaller specialty was lost this way.

The checker learns which functions return an owning value from the tracked
declarations themselves. A name is trusted only when every declaration of it in
the tree agrees, so overloads that return references are never flagged. It
reports:

* a view or pointer declared, assigned, member-initialized or returned from a
  temporary string or container (a by-value call, a constructor call, a string
  concatenation, or `.c_str()`/`.data()` of one);
* a reference declared from an accessor called on a temporary owner, such as
  `const auto & text = makeNode()["name"].String();`;
* a C++20 range-for over an accessor of a temporary owner, such as
  `for(auto & entry : makeNode().Vector())`, which C++20 does not extend;
* a reference or range-for through `*` of a smart pointer returned by value,
  such as `for(auto & bonus : *unit->getUnstackedBonuses(selector))`; the
  pointee may be owned only by that temporary.

Mark a reviewed false positive with `// lifetime-checked: <reason>` on the line.
"""

from __future__ import annotations

from pathlib import Path
import re
import sys

from tree_sitter import Language, Parser, Query, QueryCursor
import tree_sitter_cpp

from normalize_relative_includes import tracked_source_files


CPP = Language(tree_sitter_cpp.language())
FUNCTIONS = Query(CPP, "(function_declarator) @function")
ALIASES = Query(CPP, "(alias_declaration) @alias (type_definition) @alias")
CHECK_SITES = Query(CPP, """
(init_declarator) @init
(field_initializer) @field_init
(assignment_expression) @assign
(return_statement) @return
(for_range_loop) @range_for
(field_declaration) @field
(parameter_declaration) @parameter
(optional_parameter_declaration) @parameter
""")

SUPPRESSION = b"lifetime-checked:"
# Export macros the parser cannot expand; blanked to equal-width spaces so positions stay exact.
LINKAGE_MACROS = re.compile(rb"\b(?:DLL_LINKAGE|DLL_EXPORT|DLL_IMPORT|ELF_VISIBILITY)\b")
SKIPPED_PREFIXES = ("dependencies/", "test/googletest/")
SKIPPED_PARTS = {"thirdparty", "third_party", "vendor"}

TEXT_TYPES = re.compile(rb"^(?:std::)?(?:basic_string|string|u8string|u16string|u32string|wstring)$"
    rb"|^(?:QString|QByteArray|(?:std::|boost::)?(?:filesystem::)?path)$")
CONTAINER_TYPES = re.compile(rb"^(?:std::|boost::container::|boost::)?"
    rb"(?:vector|small_vector|static_vector|array|deque|list|set|multiset|map|multimap|"
    rb"unordered_set|unordered_map|flat_set|flat_map|optional)$"
    rb"|^(?:JsonNode|JsonVector|JsonMap)$")
# Types that refer to storage owned elsewhere; a temporary of one of them is harmless.
VIEW_TYPES = re.compile(rb"(?:^|::)(?:basic_string_view|string_view|wstring_view|u8string_view|span|"
    rb"iterator_range|subrange|ref_view|\w*_view|\w*View|\w*Ref|\w*Span|\w*Range)$")
# Declared variables of these types are checked as views.
STD_VIEW_TYPES = re.compile(rb"(?:^|::)(?:basic_string_view|string_view|wstring_view|u8string_view|span)$")
# Smart pointers returned by value; the pointee may die with the temporary pointer.
OWNER_POINTERS = re.compile(rb"(?:^|::)(?:shared_ptr|unique_ptr)$")
STD_VIEW_NAMES = {b"string_view", b"wstring_view", b"span"}
POINTER_SOURCES = {b"c_str", b"data", b"constData"}

# Standard and Qt functions that return a new string or container.
LIBRARY_OWNERS = {
    b"str", b"to_string", b"to_wstring", b"string", b"generic_string", b"u8string", b"wstring",
    b"toStdString", b"toUtf8", b"toLatin1", b"toLocal8Bit", b"to_lower_copy", b"to_upper_copy",
    b"trim_copy", b"trim_left_copy", b"trim_right_copy", b"replace_all_copy", b"replace_first_copy",
    b"erase_all_copy", b"join", b"format", b"vformat",
}
# Accessors that return a reference into the object they are called on.
LIBRARY_ACCESSORS = {b"front", b"back", b"at", b"String", b"Vector", b"Struct"}
# Library names that also return references or views in types we do not parse.
LIBRARY_AMBIGUOUS = {b"value", b"get", b"first", b"second", b"begin", b"end", b"substr", b"operator*"}


def without_linkage(source):
    return LINKAGE_MACROS.sub(lambda match: b" " * len(match.group(0)), source)


def skipped(relative):
    return relative.startswith(SKIPPED_PREFIXES) or bool(SKIPPED_PARTS & set(Path(relative).parts))


def unwrapped(node):
    """The single expression inside parentheses, a braced initializer or argument list."""
    while node is not None and node.type in {"parenthesized_expression", "initializer_list", "argument_list",
        "condition_clause"}:
        children = [child for child in node.named_children if child.type != "comment"]
        if len(children) != 1:
            return None
        node = children[0]
    return node


def last_name(node):
    """The unqualified name of a called function, field or type."""
    if node is None:
        return None
    if node.type in {"identifier", "field_identifier", "type_identifier", "namespace_identifier",
        "primitive_type", "destructor_name", "operator_name"}:
        return node.text
    if node.type == "template_function" or node.type == "template_type" or node.type == "template_method":
        return last_name(node.child_by_field_name("name"))
    for field in ("name", "field"):
        child = node.child_by_field_name(field)
        if child is not None:
            return last_name(child)
    return None


def base_type(node):
    """A type node's text without template arguments, e.g. `std::vector` or `JsonNode`."""
    if node is None:
        return b""
    if node.type == "template_type":
        return base_type(node.child_by_field_name("name"))
    if node.type == "qualified_identifier":
        scope = node.child_by_field_name("scope")
        name = node.child_by_field_name("name")
        prefix = base_type(scope) if scope is not None and scope.type != "template_type" else (
            base_type(scope.child_by_field_name("name")) if scope is not None else b"")
        return (prefix + b"::" if prefix else b"") + base_type(name)
    return node.text


def return_kind(type_node, declarator, function, aliases):
    """Classify what a declared function returns: reference, pointer, view, owner, value or unknown."""
    node = declarator
    while node is not None and node.id != function.id:
        if node.type == "reference_declarator":
            return "reference"
        if node.type == "pointer_declarator":
            return "pointer"
        node = node.child_by_field_name("declarator") or (node.named_children[0] if node.named_children else None)
    if type_node is None or type_node.type == "decltype":
        return "unknown"
    name = base_type(type_node)
    if VIEW_TYPES.search(name) or aliases.get(name) == "view":
        return "view"
    if OWNER_POINTERS.search(name) or aliases.get(name) == "owner":
        return "owner"
    return "value"


def function_signatures(tree, aliases):
    """Yield (name, kind) for every function declarator in a parsed file."""
    for function in QueryCursor(FUNCTIONS).captures(tree.root_node).get("function", []):
        declarator = function.child_by_field_name("declarator")
        name = last_name(declarator)
        if name is None or declarator.type == "operator_name":
            continue
        owner = function.parent
        while owner is not None and owner.type in {"reference_declarator", "pointer_declarator",
            "parenthesized_declarator", "attributed_declarator"}:
            owner = owner.parent
        if owner is None or owner.type not in {"declaration", "field_declaration", "function_definition"}:
            continue
        if any(child.type == "trailing_return_type" for child in function.named_children):
            yield name, "unknown"
            continue
        yield name, return_kind(owner.child_by_field_name("type"), owner.child_by_field_name("declarator"),
            function, aliases)


def alias_kinds(tree):
    """Yield (name, kind) for `using`/`typedef` names of view, raw-pointer or owning-pointer types."""
    for node in QueryCursor(ALIASES).captures(tree.root_node).get("alias", []):
        target = node.child_by_field_name("type")
        if node.type == "alias_declaration":
            name = node.child_by_field_name("name")
            pointer = target is not None and any(child.type == "abstract_pointer_declarator"
                for child in target.named_children)
            target = target.child_by_field_name("type") if target is not None else None
            name = name.text if name is not None else None
        else:
            declarator = node.child_by_field_name("declarator")
            pointer = declarator is not None and declarator.type == "pointer_declarator"
            name = (last_name(declarator) or declarator.text) if declarator is not None else None
        if name is None or target is None:
            continue
        if pointer or VIEW_TYPES.search(base_type(target)):
            yield name, "view"
        elif OWNER_POINTERS.search(base_type(target)):
            yield name, "owner"


class Signatures:
    """Function names whose every tracked declaration agrees on how the value is returned."""

    def __init__(self):
        self.kinds = {}
        self.aliases = {}

    def add_aliases(self, tree):
        for name, kind in alias_kinds(tree):
            self.aliases[name] = kind if self.aliases.get(name, kind) == kind else "ambiguous"

    def add(self, tree):
        for name, kind in function_signatures(tree, self.aliases):
            self.kinds.setdefault(name, set()).add(kind)

    def finish(self):
        def agreed(kinds):
            return {name for name, seen in self.kinds.items() if seen <= kinds}

        self.owned = agreed({"value"}) - LIBRARY_AMBIGUOUS
        self.owners = agreed({"owner"}) - LIBRARY_AMBIGUOUS
        self.owned |= {name for name in LIBRARY_OWNERS if self.kinds.get(name, {"value"}) <= {"value"}}
        self.accessors = agreed({"reference"}) - LIBRARY_AMBIGUOUS
        self.accessors |= {name for name in LIBRARY_ACCESSORS if self.kinds.get(name, {"reference"}) <= {"reference"}}
        return self


def called(node):
    return last_name(node.child_by_field_name("function")) if node is not None and node.type == "call_expression" else None


def receiver(call):
    """The object a member call is made on, or None for a free call."""
    function = call.child_by_field_name("function")
    if function is not None and function.type == "field_expression" and \
        function.child_by_field_name("operator").type == ".":
        return function.child_by_field_name("argument")
    return None


def is_temporary_owner(node, signatures):
    """Whether `node` is a prvalue that owns its storage: a by-value call, constructor or concatenation."""
    node = unwrapped(node)
    if node is None:
        return False
    if node.type == "conditional_expression":
        return any(is_temporary_owner(node.child_by_field_name(field), signatures)
            for field in ("consequence", "alternative"))
    if node.type == "binary_expression" and node.child_by_field_name("operator").type == "+":
        return any(is_temporary_owner(node.child_by_field_name(side), signatures) for side in ("left", "right"))
    if node.type == "call_expression":
        function = node.child_by_field_name("function")
        name = called(node)
        if function is not None and function.type in {"qualified_identifier", "identifier", "template_function"} \
            and name is not None and TEXT_TYPES.match(base_type(function)):
            return True
        if name in STD_VIEW_NAMES:
            return is_temporary_owner(node.child_by_field_name("arguments"), signatures)
        return name in signatures.owned
    if node.type == "compound_literal_expression":
        name = base_type(node.child_by_field_name("type"))
        return bool(TEXT_TYPES.match(name) or CONTAINER_TYPES.match(name))
    return False


def views_temporary(node, signatures):
    """Whether a view initialized from `node` would view a temporary."""
    node = unwrapped(node)
    if node is None:
        return False
    if node.type == "call_expression" and called(node) in POINTER_SOURCES and receiver(node) is not None:
        return is_temporary_owner(receiver(node), signatures)
    return is_temporary_owner(node, signatures)


def points_into_temporary(node, signatures):
    """Whether a pointer initialized from `node` points into a temporary's storage."""
    node = unwrapped(node)
    if node is not None and node.type == "call_expression" and called(node) in POINTER_SOURCES:
        return receiver(node) is not None and is_temporary_owner(receiver(node), signatures)
    return False


def refers_into_temporary(node, signatures):
    """Whether `node` is an accessor result referring into a temporary owner."""
    node = unwrapped(node)
    if node is None:
        return False
    if node.type == "subscript_expression":
        return is_temporary_owner(node.child_by_field_name("argument"), signatures) or \
            refers_into_temporary(node.child_by_field_name("argument"), signatures)
    if node.type == "pointer_expression" and node.child_by_field_name("operator").type == "*":
        return is_temporary_pointer(node.child_by_field_name("argument"), signatures)
    if node.type == "call_expression" and called(node) in signatures.accessors and receiver(node) is not None:
        inner = receiver(node)
        return is_temporary_owner(inner, signatures) or refers_into_temporary(inner, signatures)
    return False


def is_temporary_pointer(node, signatures):
    """Whether `node` is a smart pointer returned by value, whose pointee may die with it."""
    node = unwrapped(node)
    return node is not None and node.type == "call_expression" and called(node) in signatures.owners


def declared_kind(type_node, declarator):
    """Classify a declared variable: view, pointer, ref or None."""
    node = declarator
    while node is not None and node.type in {"init_declarator", "attributed_declarator"}:
        node = node.child_by_field_name("declarator") or node.named_children[0]
    if node is not None and node.type == "reference_declarator":
        inner = node.named_children[0] if node.named_children else None
        if inner is not None and inner.type in {"pointer_declarator"}:
            return "ref"
        return "view" if type_node is not None and STD_VIEW_TYPES.search(base_type(type_node)) else "ref"
    if node is not None and node.type == "pointer_declarator":
        return "pointer"
    if type_node is not None and STD_VIEW_TYPES.search(base_type(type_node)):
        return "view"
    return None


def variable_name(declarator):
    node = declarator
    while node is not None and node.type not in {"identifier", "field_identifier"}:
        node = node.child_by_field_name("declarator") or (node.named_children[0] if node.named_children else None)
    return node.text if node is not None else None


def enclosing_function(node):
    while node is not None and node.type not in {"function_definition", "lambda_expression"}:
        node = node.parent
    return node


def dangles(kind, value, signatures):
    if kind == "view":
        return views_temporary(value, signatures)
    if kind == "pointer":
        return points_into_temporary(value, signatures)
    if kind == "ref":
        return refers_into_temporary(value, signatures)
    return False


def dangling_lines(source, parser, signatures):
    """Line numbers where a view, pointer or reference is bound to a temporary."""
    tree = parser.parse(without_linkage(source))
    captures = QueryCursor(CHECK_SITES).captures(tree.root_node)
    declared = {}

    def remember(node, kind, name):
        if kind in {"view", "pointer"} and name is not None:
            function = enclosing_function(node)
            declared[(function.id if function is not None else None, name)] = kind

    def lookup(node, name):
        function = enclosing_function(node)
        while function is not None:
            if (function.id, name) in declared:
                return declared[(function.id, name)]
            function = enclosing_function(function.parent)
        return declared.get((None, name))

    for node in captures.get("field", []) + captures.get("parameter", []):
        if node.type != "field_declaration" and enclosing_function(node) is None:
            continue
        remember(node, declared_kind(node.child_by_field_name("type"), node.child_by_field_name("declarator")),
            variable_name(node.child_by_field_name("declarator")))
    matches = set()
    for init in captures.get("init", []):
        owner = init.parent
        kind = declared_kind(owner.child_by_field_name("type"), init)
        value = init.child_by_field_name("value")
        explicit = unwrapped(value)
        if kind is None and explicit is not None and called(explicit) in STD_VIEW_NAMES:
            kind = "view"
            value = explicit.child_by_field_name("arguments")
        remember(init, kind, variable_name(init))
        if dangles(kind, value, signatures):
            matches.add(init.start_point.row + 1)
    for init in captures.get("field_init", []):
        name = last_name(init.named_children[0]) if init.named_children else None
        if declared.get((None, name)) == "view" and views_temporary(init.named_children[-1], signatures):
            matches.add(init.start_point.row + 1)
    for assign in captures.get("assign", []):
        left = assign.child_by_field_name("left")
        name = last_name(left) if left is not None and left.type in {"identifier", "field_expression"} else None
        if assign.child_by_field_name("operator").type == "=" and \
            dangles(lookup(assign, name), assign.child_by_field_name("right"), signatures):
            matches.add(assign.start_point.row + 1)
    for statement in captures.get("return", []):
        function = enclosing_function(statement)
        if function is None or function.type != "function_definition":
            continue
        kind = declared_kind(function.child_by_field_name("type"), function.child_by_field_name("declarator"))
        value = statement.named_children[0] if statement.named_children else None
        if kind in {"view", "pointer"} and dangles(kind, value, signatures):
            matches.add(statement.start_point.row + 1)
    for loop in captures.get("range_for", []):
        if refers_into_temporary(loop.child_by_field_name("right"), signatures):
            matches.add(loop.start_point.row + 1)
    lines = source.split(b"\n")
    return sorted(line for line in matches if SUPPRESSION not in lines[line - 1])


def tracked_signatures(paths, parser):
    signatures = Signatures()
    trees = [parser.parse(without_linkage(path.read_bytes())) for path in paths]
    for tree in trees:
        signatures.add_aliases(tree)
    for tree in trees:
        signatures.add(tree)
    return signatures.finish()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = Parser(CPP)
    paths = tracked_source_files(repo)
    signatures = tracked_signatures(paths, parser)
    issues = []
    for path in paths:
        relative = path.relative_to(repo).as_posix()
        if skipped(relative):
            continue
        for line in dangling_lines(path.read_bytes(), parser, signatures):
            issues.append((relative, line))
    for relative, line in issues:
        print(f"{relative}:{line}: a view, pointer or reference outlives the temporary it is bound to")
    print(f"Temporary lifetime violations: {len(issues)}")
    return 1 if issues else 0


if __name__ == "__main__":
    sys.exit(main())
