#!/usr/bin/env python3
"""Source guard for the New Horizons Necromancy result presentation.

The server owns eligibility, conversion, capacity and mana calculations.  This
small client contract only proves that the packet summary is rendered from
those authoritative fields, that final Skeleton-form/Zombie counts get creature
components, and that legacy Necromancy feedback remains the fallback for old
result packets.
"""

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HELPER = (ROOT / "client/UIHelper.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "client/UIHelper.h").read_text(encoding="utf-8")
VISITOR = (ROOT / "client/NetPacksClient.cpp").read_text(encoding="utf-8")


def require(source: str, needle: str, label: str) -> None:
    if needle not in source:
        raise AssertionError(f"missing {label}: {needle}")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


def main() -> None:
    for field in (
        "eligibleCasualties",
        "skeletonsOffered",
        "skeletonsRaised",
        "zombiesRaised",
        "wightsRaised",
        "darkConversionChosen",
        "manaRecovered",
        "blockedByArmyCapacity",
    ):
        require(HELPER, f"result.{field}", f"authoritative summary field {field}")

    require(HELPER, '"Generated: "', "generated count")
    require(HELPER, '"Converted: "', "conversion count")
    component_builder = function_body(
        HELPER,
        "UIHelper::getNewHorizonsNecromancyComponents(",
    )
    summary_builder = function_body(
        HELPER,
        "UIHelper::getNewHorizonsNecromancyInfoWindowText(",
    )
    selected_skeleton = (
        "result.skeletonCreature.hasValue()\n"
        "\t\t? result.skeletonCreature\n"
        "\t\t: CreatureID(CreatureID::decode(\"core:skeleton\"))"
    )
    require(component_builder, selected_skeleton,
            "authoritative Skeleton creature with legacy fallback for result components")
    require(summary_builder, selected_skeleton,
            "authoritative Skeleton creature with legacy fallback for result text")
    if HELPER.count('CreatureID::decode("core:skeleton")') != 3:
        raise AssertionError(
            "core:skeleton may appear only as the missing-field fallback "
            "and base Skeleton-equivalent binding"
        )
    require(component_builder,
            "ComponentType::CREATURE, skeleton, result.skeletonsRaised",
            "selected Skeleton result component")
    require(summary_builder,
            "text.appendName(baseSkeleton, result.skeletonsOffered)",
            "base Skeleton-equivalent generated count")
    require(summary_builder,
            "text.appendName(skeleton, result.skeletonsRaised)",
            "selected Skeleton delivered count")
    require(summary_builder,
            "const auto baseSkeleton = CreatureID(CreatureID::decode(\"core:skeleton\"));",
            "canonical base Skeleton-equivalent binding")
    require(summary_builder,
            'text.appendRawString(" from Core casualties (three ");\n'
            "\t\ttext.appendName(baseSkeleton, 3);\n"
            '\t\ttext.appendRawString(" each)\\n");',
            "Core-only conversion explanation using base Skeleton equivalents")
    require(summary_builder,
            'text.appendRawString(" from Elite casualties (six ");\n'
            "\t\ttext.appendName(baseSkeleton, 6);\n"
            '\t\ttext.appendRawString(" per Wight)\\n");',
            "Elite-casualty Wight explanation using base Skeleton equivalents")
    hardcoded_skeleton_labels = [
        value for value in re.findall(r'"((?:\\.|[^"\\])*)"', HELPER, re.IGNORECASE)
        if "skeleton" in value.lower() and value != "core:skeleton"
    ]
    if hardcoded_skeleton_labels:
        raise AssertionError(
            "Necromancy result text must name the selected Skeleton creature, "
            f"not hardcode a base-only label: {hardcoded_skeleton_labels}"
        )
    require(HELPER, '"Delivered to army: "', "delivered count")
    require(HELPER, '"Black Harvest recovered +"', "mana recovery")
    require(HELPER, '"No creatures were delivered: the hero has no legal army slot',
            "blocked capacity reason")
    require(HELPER, '"The eligible casualty count was below the current Necromancy raising threshold.',
            "rounded-zero threshold reason")
    require(HELPER, 'ComponentType::CREATURE, zombie', "Zombie result component")
    require(HELPER, 'CreatureID::decode("core:wight")', "Wight creature binding")
    require(HELPER, 'ComponentType::CREATURE, wight', "Wight result component")
    require(HEADER, "getNewHorizonsNecromancyInfoWindowText", "summary text API")
    require(HEADER, "getNewHorizonsNecromancyComponents", "summary component API")

    summary = VISITOR.split("void ApplyClientNetPackVisitor::visitBattleResultsApplied", 1)[1]
    require(summary, "if(pack.necromancy.active)", "New Horizons summary gate")
    require(summary, "getNewHorizonsNecromancyInfoWindowText(pack.necromancy)",
            "summary text call")
    require(summary, "getNewHorizonsNecromancyComponents(pack.necromancy)",
            "summary component call")
    require(summary, "else if(pack.raisedStack.getCreature())", "legacy fallback")
    if summary.index("getNewHorizonsNecromancyInfoWindowText") > summary.index("else if(pack.raisedStack.getCreature())"):
        raise AssertionError("legacy raisedStack popup must not replace an active New Horizons summary")

    # Presentation must not derive the result from a client-side hero/army
    # snapshot or call the legacy health-weighted resolver.
    if "calculateNecromancy" in HELPER or "getSlotFor" in HELPER:
        raise AssertionError("client summary must consume the packet, not infer army capacity")

    print("New Horizons Necromancy summary UI source checks passed")


if __name__ == "__main__":
    main()
