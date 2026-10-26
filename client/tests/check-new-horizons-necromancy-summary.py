#!/usr/bin/env python3
"""Source guard for the New Horizons Necromancy result presentation.

The server owns eligibility, conversion, capacity and mana calculations.  This
small client contract only proves that the packet summary is rendered from
those authoritative fields, that final Skeleton-form/Zombie counts get creature
components, Lord of the Dead's Bone Dragon conversion and component use packet
counts, Death Lord and Grave Knowledge pools are explained from packet counts,
Ossuary delivery names its destination when available, and legacy Necromancy
feedback remains the fallback for old result packets.
"""

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HELPER = (ROOT / "client/UIHelper.cpp").read_text(encoding="utf-8")
CONVERSIONS = (ROOT / "client/NewHorizonsNecromancyText.cpp").read_text(encoding="utf-8")
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
    conversion_builder = function_body(CONVERSIONS, "void appendConversions(")
    conversion_call = "necromancyPresentation::appendConversions(text, result);"
    require(HELPER, conversion_call, "shared conversion text receives the packet result")
    for field in (
        "percentage",
        "eligibleCasualties",
        "deathLordCasualties",
        "graveKnowledgeCasualties",
        "deathLordSkeletons",
        "graveKnowledgeSkeletons",
        "lordOfDeadSkeletonsConsumed",
        "boneDragonsRaised",
        "skeletonsOffered",
        "skeletonsRaised",
        "zombiesRaised",
        "wightsRaised",
        "darkConversionChosen",
        "manaRecovered",
        "blockedByArmyCapacity",
        "ossuaryTown",
    ):
        require(HELPER + conversion_builder, f"result.{field}", f"authoritative summary field {field}")

    require(HELPER, '"Generated: "', "generated count")
    require(conversion_builder, '"Converted: "', "conversion count")
    if "#if defined(NEW_HORIZONS_NECROMANCY_LORD_OF_DEAD)" in HELPER:
        raise AssertionError(
            "NEW_HORIZONS_NECROMANCY_LORD_OF_DEAD is an enum, not a preprocessor symbol"
        )
    component_builder = function_body(
        HELPER,
        "UIHelper::getNewHorizonsNecromancyComponents(",
    )
    summary_builder = function_body(
        HELPER,
        "UIHelper::getNewHorizonsNecromancyInfoWindowText(",
    )
    require(summary_builder, conversion_call, "summary delegates its conversion text")
    summary_builder = summary_builder.replace(conversion_call, conversion_builder, 1)
    fresh_builder = function_body(CONVERSIONS, "void appendFreshDelivery(")
    require(summary_builder, "necromancyPresentation::appendFreshDelivery(text, result,",
            "fresh delivery delegates the authoritative packet")
    require(summary_builder, "necromancyPresentation::appendRecovery(text, result);",
            "own recovery is separate from fresh/Ossuary output")
    require(component_builder, "necromancyPresentation::recoveryOutputs(result)",
            "recovered components use the same original-form readback")
    require(component_builder, "ComponentType::CREATURE, recovered.creature, recovered.count",
            "recovery components use actual restored counts")
    recovery_builder = function_body(CONVERSIONS, "std::vector<RecoveryOutput> recoveryOutputs(")
    for field, creature in (("recoveredSkeletons", "core:skeleton"),
                            ("recoveredSkeletonWarriors", "core:skeletonWarrior")):
        require(recovery_builder, f"if(result.{field} > 0)", "positive actual recovery count")
        require(recovery_builder, f'CreatureID::decode("{creature}")', "original recovered form")
        require(recovery_builder, f", result.{field}}}", "no fresh-output count substitution")
    if any(field in fresh_builder for field in ("recoveredSkeletons", "recoveredSkeletonWarriors")):
        raise AssertionError("fresh delivery/Ossuary counts must not include own recovery")
    require(CONVERSIONS, '"Grave Knowledge restored to army: "', "separate own recovery row")
    # Recovery is independent of eligible enemy casualties and fresh delivery.
    # In particular, a capacity-blocked fresh batch must not hide restored own troops.
    recovery_call = "necromancyPresentation::appendRecovery(text, result);"
    prefix = summary_builder.split(recovery_call, 1)[0]
    if prefix.count("{") != prefix.count("}"):
        raise AssertionError("own recovery must not be nested under a fresh-output gate")
    if summary_builder.index(recovery_call) > summary_builder.index("if(result.blockedByArmyCapacity)"):
        raise AssertionError("own recovery must remain visible before fresh-capacity failure text")
    recovery_text = function_body(CONVERSIONS, "void appendRecovery(")
    require(recovery_text, "const auto outputs = recoveryOutputs(result);", "shared original-form recovery text")
    require(recovery_text, "if(outputs.empty())", "historical zero recovery remains silent")
    if any(field in recovery_text + recovery_builder for field in (
            "eligibleCasualties", "blockedByArmyCapacity", "ossuaryTown", "manaRecovered")):
        raise AssertionError("own recovery readback must not depend on fresh-output or Mana fields")
    if "Black Harvest recovered" in HELPER + CONVERSIONS:
        raise AssertionError("generic Mana result must not advertise the retired Black Harvest name")
    summary_builder += fresh_builder
    selected_skeleton = (
        "result.skeletonCreature.hasValue()\n"
        "\t\t? result.skeletonCreature\n"
        "\t\t: CreatureID(CreatureID::decode(\"core:skeleton\"))"
    )
    require(component_builder, selected_skeleton,
            "authoritative Skeleton creature with legacy fallback for result components")
    require(summary_builder, selected_skeleton,
            "authoritative Skeleton creature with legacy fallback for result text")
    if HELPER.count('CreatureID::decode("core:skeleton")') != 2:
        raise AssertionError(
            "core:skeleton may appear only as the missing-field fallback "
            "and base Skeleton-equivalent binding"
        )
    require(component_builder,
            "ComponentType::CREATURE, skeleton, result.skeletonsRaised",
            "selected Skeleton result component")
    require(component_builder,
            "CreatureID::decode(\"core:boneDragon\")",
            "canonical Bone Dragon component binding")
    require(component_builder,
            "if(result.boneDragonsRaised > 0)",
            "Bone Dragon component gate")
    require(component_builder,
            "ComponentType::CREATURE, boneDragon, result.boneDragonsRaised",
            "authoritative Bone Dragon result component")
    require(summary_builder,
            "text.appendName(baseSkeleton, result.skeletonsOffered)",
            "base Skeleton-equivalent generated count")
    require(summary_builder, '"Ordinary living casualties: "',
            "ordinary living casualty pool label")
    require(summary_builder, '" (normal rate "',
            "ordinary living rate scope")
    require(summary_builder, "text.appendNumber(result.percentage)",
            "authoritative ordinary living rate")
    require(summary_builder, "if(result.deathLordCasualties > 0)",
            "Death Lord pool eligibility gate")
    require(summary_builder, '" Construct/Elemental casualties -> "',
            "Death Lord casualty pool description")
    require(summary_builder, "text.appendNumber(result.deathLordCasualties)",
            "authoritative Death Lord casualty count")
    require(summary_builder, "text.appendNumber(result.deathLordSkeletons)",
            "authoritative Death Lord generated equivalent count")
    require(summary_builder,
            "text.appendName(baseSkeleton, result.deathLordSkeletons)",
            "Death Lord base Skeleton-equivalent label")
    require(summary_builder,
            '" (base equivalent) at 25% of the normal rate.\\n"',
            "Death Lord normal-rate fraction")
    require(summary_builder,
            '" (base equivalents) at 25% of the normal rate.\\n"',
            "Death Lord plural normal-rate fraction")
    require(summary_builder, "if(result.graveKnowledgeCasualties > 0)",
            "Grave Knowledge pool eligibility gate")
    require(summary_builder, '" Undead casualties -> "',
            "Grave Knowledge casualty pool description")
    require(summary_builder,
            "text.appendNumber(result.graveKnowledgeCasualties)",
            "authoritative Grave Knowledge casualty count")
    require(summary_builder,
            "text.appendNumber(result.graveKnowledgeSkeletons)",
            "authoritative Grave Knowledge generated equivalent count")
    require(summary_builder,
            "text.appendName(baseSkeleton, result.graveKnowledgeSkeletons)",
            "Grave Knowledge base Skeleton-equivalent label")
    require(summary_builder,
            '" (base equivalent) at the fixed 20% rate.\\n"',
            "Grave Knowledge fixed rate")
    require(summary_builder,
            '" (base equivalents) at the fixed 20% rate.\\n"',
            "Grave Knowledge plural fixed rate")
    require(summary_builder,
            "text.appendName(skeleton, result.skeletonsRaised)",
            "selected Skeleton delivered count")
    require(summary_builder,
            "result.ossuaryTown != ObjectInstanceID::NONE",
            "packet-provided Ossuary destination gate")
    require(summary_builder,
            "GAME->interface()->cb->getTown(result.ossuaryTown)",
            "existing callback lookup for the delivered-to town")
    require(summary_builder,
            "ossuaryTown->getObjectName().toString(&GAME->translator())",
            "actual delivered-to town name")
    require(summary_builder, '"Delivered to Ossuary: "',
            "Ossuary destination label")
    require(summary_builder, '"a Necropolis town"',
            "safe generic Ossuary destination fallback")
    require(summary_builder, '" instead of the hero: "',
            "Ossuary delivery instead of hero wording")
    if "getTownsInfo" in summary_builder or "hasActivePerk" in summary_builder:
        raise AssertionError("Ossuary summary must not select destinations or recalculate perk eligibility")
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
    require(summary_builder,
            "CreatureID::decode(\"core:boneDragon\")",
            "canonical Bone Dragon summary binding")
    require(summary_builder,
            "if(result.lordOfDeadSkeletonsConsumed > 0 || result.boneDragonsRaised > 0)",
            "Lord of the Dead conversion result gate")
    require(summary_builder, 'text.appendRawString("Lord of the Dead: ");',
            "Lord of the Dead conversion label")
    require(summary_builder,
            "text.appendNumber(result.lordOfDeadSkeletonsConsumed)",
            "authoritative consumed Skeleton-equivalent count")
    require(summary_builder, "text.appendName(baseSkeleton, 1)",
            "localized base Skeleton-equivalent name")
    require(summary_builder, 'text.appendRawString(" equivalents -> ");',
            "Lord of the Dead conversion relationship")
    require(summary_builder,
            "text.appendNumber(result.boneDragonsRaised)",
            "authoritative Bone Dragon output count")
    require(summary_builder,
            "text.appendName(boneDragon, result.boneDragonsRaised)",
            "localized Bone Dragon output name")
    hardcoded_skeleton_labels = [
        value for value in re.findall(r'"((?:\\.|[^"\\])*)"', HELPER, re.IGNORECASE)
        if "skeleton" in value.lower() and value != "core:skeleton"
    ]
    if hardcoded_skeleton_labels:
        raise AssertionError(
            "Necromancy result text must name the selected Skeleton creature, "
            f"not hardcode a base-only label: {hardcoded_skeleton_labels}"
        )
    require(CONVERSIONS, '"Delivered to army: "', "delivered count")
    require(HELPER, '"Necromancy recovered +"', "mana recovery")
    require(HELPER, '"No newly raised creatures were delivered: no legal army capacity or Necropolis destination was available',
            "blocked capacity reason")
    require(HELPER, '"The eligible casualty count was below the current Necromancy raising threshold.',
            "rounded-zero threshold reason")
    require(HELPER, 'ComponentType::CREATURE, zombie', "Zombie result component")
    require(HELPER, 'CreatureID::decode("core:wight")', "Wight creature binding")
    require(HELPER, 'ComponentType::CREATURE, wight', "Wight result component")
    require(summary_builder,
            "const bool hasNecromancyOutput = result.skeletonsRaised > 0\n"
            "\t\t|| result.zombiesRaised > 0\n"
            "\t\t|| result.wightsRaised > 0\n"
            "\t\t|| result.boneDragonsRaised > 0;\n"
            "\tif(hasNecromancyOutput)",
            "all four creature outputs in the delivery summary gate")
    require(component_builder,
            "ComponentType::CREATURE, boneDragon, result.boneDragonsRaised",
            "fourth creature output component")
    require(summary_builder,
            'if(result.boneDragonsRaised > 0)\n'
            '\t\t{\n'
            '\t\t\tif(hasOutput)\n'
            '\t\t\t\ttext.appendRawString(" and ");\n'
            '\t\t\ttext.appendNumber(result.boneDragonsRaised);\n'
            '\t\t\ttext.appendRawString(" ");\n'
            '\t\t\ttext.appendName(boneDragon, result.boneDragonsRaised);\n'
            '\t\t}',
            "Bone Dragon delivery name")
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
    if summary_builder.index("if(result.deathLordCasualties > 0)") > summary_builder.index("if(result.blockedByArmyCapacity)"):
        raise AssertionError("Death Lord pool summary must remain visible when army capacity blocks delivery")
    if summary_builder.index("if(result.graveKnowledgeCasualties > 0)") > summary_builder.index("if(result.blockedByArmyCapacity)"):
        raise AssertionError("Grave Knowledge pool summary must remain visible when army capacity blocks delivery")

    print("New Horizons Necromancy summary UI source checks passed")


if __name__ == "__main__":
    main()
