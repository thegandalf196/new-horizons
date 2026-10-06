#!/usr/bin/env python3
"""Source guard for read-only Resource Broker feedback in resource trades."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
UI = (ROOT / "client/widgets/markets/CMarketResources.cpp").read_text(encoding="utf-8")
MODULE = (ROOT / "Mods/new-horizons/mod.json").read_text(encoding="utf-8")


def require(source: str, text: str, label: str) -> None:
    if text not in source:
        raise AssertionError(f"missing {label}: {text}")


def method(source: str, signature: str, next_signature: str) -> str:
    return source.split(signature, 1)[1].split(next_signature, 1)[0]


def main() -> None:
    helper = method(UI, "void appendResourceBrokerHelp(", "\n}\n}")
    for guard in (
        "sellingResource == buyingResource",
        "newHorizonsMagic::rulesActive(tradeInterface.cb->getMagicRules())",
        "dynamic_cast<const CGTownInstance *>(market)",
        "town->getOwner() != tradeInterface.playerID",
        "town->IMarket::getResourceExchangeEffectiveness(sellingResource, buyingResource)",
        "market->getResourceExchangeEffectiveness(sellingResource, buyingResource)",
        "!std::isfinite(ordinaryEffectiveness) || ordinaryEffectiveness <= 0.0",
        "!std::isfinite(actualEffectiveness) || actualEffectiveness <= ordinaryEffectiveness",
        "std::lround(percentIncrease)",
        'MetaString::createFromTextID("new-horizons.economy.resourceBroker.modifier")',
        'brokerHelp.replaceTokenNumber("%PERCENT", roundedPercent)',
    ):
        require(helper, guard, "saved eligibility and shared-rate contribution")
    for prohibited in (
        "hasActivePerk",
        "1.2",
        "sendAndApply",
        "cb->trade",
    ):
        if prohibited in helper:
            raise AssertionError(f"tooltip helper must not duplicate rules or mutate trade state: {prohibited}")

    refresh = method(UI, "void CMarketResources::highlightingChanged()", "void CMarketResources::updateSubtitles()")
    base_help = 'LIBRARY->generaltexth->zelp[595]'
    require(refresh, f"auto dealHelp = {base_help};", "ordinary Deal help reset on every refresh")
    require(refresh, "bidTradePanel->isHighlighted() && offerTradePanel->isHighlighted()",
            "selected resource-pair gate")
    require(refresh, "sellingResource != buyingResource", "distinct-pair gate")
    require(refresh, "appendResourceBrokerHelp(dealHelp, market, *getTradeInterface()",
            "tooltip annotation from the current market")
    require(refresh, "deal->setHelp(dealHelp)", "native Deal control help refresh")
    require(refresh, "market->getOffer(", "existing actual quote path unchanged")

    deselect = method(UI, "void CMarketResources::deselect()", "void CMarketResources::makeDeal()")
    require(deselect, f"deal->setHelp({base_help})", "ordinary Deal help restored on deselection")
    deal = method(UI, "void CMarketResources::makeDeal()", "CMarketBase::MarketShowcasesParams")
    require(deal, "getTradeInterface()->cb->trade(", "existing authoritative trade request retained")

    require(MODULE, '"new-horizons.economy.resourceBroker.modifier"', "localized contribution text registration")
    print("PASS: selected eligible town pairs get a localized rate contribution; other states restore ordinary help")
    print("Source wiring only; native tooltip fit and rendered acceptance remain unverified")


if __name__ == "__main__":
    main()
