#!/usr/bin/env python3
"""Source guard for the New Horizons Necromancy result presentation.

The server owns eligibility, conversion, capacity and mana calculations.  This
small client contract only proves that the packet summary is rendered from
those authoritative fields, that final Skeleton/Zombie counts get creature
components, and that legacy Necromancy feedback remains the fallback for old
result packets.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HELPER = (ROOT / "client/UIHelper.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "client/UIHelper.h").read_text(encoding="utf-8")
VISITOR = (ROOT / "client/NetPacksClient.cpp").read_text(encoding="utf-8")


def require(source: str, needle: str, label: str) -> None:
    if needle not in source:
        raise AssertionError(f"missing {label}: {needle}")


def main() -> None:
    for field in (
        "eligibleCasualties",
        "skeletonsOffered",
        "skeletonsRaised",
        "zombiesRaised",
        "darkConversionChosen",
        "manaRecovered",
        "blockedByArmyCapacity",
    ):
        require(HELPER, f"result.{field}", f"authoritative summary field {field}")

    require(HELPER, '"Generated: "', "generated count")
    require(HELPER, '"Converted: "', "conversion count")
    require(HELPER, '"Delivered to army: "', "delivered count")
    require(HELPER, '"Black Harvest recovered +"', "mana recovery")
    require(HELPER, '"No creatures were delivered: the hero has no legal army slot',
            "blocked capacity reason")
    require(HELPER, '"The eligible casualty count was below the current Necromancy raising threshold.',
            "rounded-zero threshold reason")
    require(HELPER, 'ComponentType::CREATURE, skeleton', "Skeleton result component")
    require(HELPER, 'ComponentType::CREATURE, zombie', "Zombie result component")
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
