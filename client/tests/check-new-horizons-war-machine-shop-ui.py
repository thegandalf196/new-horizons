#!/usr/bin/env python3
"""Source and native-resolution geometry guard for the NH War Machine shop."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "client/windows/CCastleInterface.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "client/windows/CCastleInterface.h").read_text(encoding="utf-8")


def constructor_body(source: str) -> str:
	marker = "CBlacksmithDialog::CBlacksmithDialog(ObjectInstanceID townId, ObjectInstanceID hid, ArtifactID preferredArtifact)"
	start = source.index(marker)
	open_brace = source.index("{", start)
	depth = 0
	for offset in range(open_brace, len(source)):
		depth += (source[offset] == "{") - (source[offset] == "}")
		if depth == 0:
			return source[open_brace + 1 : offset]
	raise AssertionError("New Horizons shop constructor has unbalanced braces")


def require(condition: bool, message: str) -> None:
	if not condition:
		raise AssertionError(message)


def geometry_checks(body: str) -> None:
	constants = {
		name: int(value)
		for name, value in re.findall(r"constexpr int (\w+) = (\d+);", body)
	}
	shop_top = constants["shopTop"]
	shop_height = constants["shopHeight"]
	first_row_top = constants["firstRowTop"]
	for count in (1, 2, 3):
		row_height = (shop_height - 16) // count
		rows = []
		for index in range(count):
			row_top = first_row_top + index * row_height
			content_top = row_top + max(0, (row_height - 92) // 2)
			content_bottom = content_top + 92
			rows.append((row_top, row_top + row_height))
			require(row_top >= shop_top + 8, f"{count} offer rows begin outside the recessed shop area")
			require(content_top >= row_top, f"{count} offer row content starts above its row")
			require(content_bottom <= row_top + row_height, f"{count} offer row content exceeds its row")
		require(rows[-1][1] <= shop_top + shop_height - 8, f"{count} offer rows exceed the framed shop area")
		for previous, current in zip(rows, rows[1:]):
			require(previous[1] == current[0], f"{count} offer rows have a gap or overlap")

	# The help target occupies x=34..506 while the tactile buy control begins at x=535.
	require(constants["iconLeft"] + 472 < constants["buyLeft"], "machine help area intercepts the buy button")
	# The footer status bar ends before the close button's native position.
	require(8 + (640 - 120) < 555, "footer status bar overlaps the close button")
	require(433 + 64 <= 500 and 555 + 64 <= 640, "the close button exceeds the 640x500 panel bounds")
	require(78 + 352 < 443, "shop frame overlaps the footer instruction")


def check(source: str, header: str) -> None:
	body = constructor_body(source)
	require("CBlacksmithDialog(ObjectInstanceID townId, ObjectInstanceID hid, ArtifactID preferredArtifact);" in header,
		"the New Horizons shop constructor is not declared")
	require('capabilityRules["rulesetVersion"].Integer() >= 4' in source,
		"saved capability rules do not select the new shop")
	entry = source.split("void CCastleBuildings::enterBlacksmith", 1)[1].split("void CCastleBuildings::enterBuilding", 1)[0]
	require(entry.index("if(!hero)") < entry.index('capabilityRules["rulesetVersion"].Integer() >= 4'),
		"the visiting-hero feedback guard does not run before opening the shop")
	require(entry.index('capabilityRules["rulesetVersion"].Integer() >= 4') < entry.index("auto art = artifactID.toArtifact()"),
		"classic single-offer fallback runs before the New Horizons branch")
	require("const auto offers = town ? town->getWarMachineShopOffers()" in body,
		"the panel does not read authoritative town shop offers")
	require("const int visibleOfferCount = static_cast<int>(offers.size());" in body,
		"the panel does not iterate every town offer")
	require("getPrice()" not in body, "the New Horizons panel substitutes base artifact prices")
	require("std::to_string(offer.price)" in body, "the effective town price is not displayed")
	require("currentGold >= offer.price" in body, "affordability ignores the effective town price")
	require('"Already equipped"' in body and '"Not enough Gold"' in body,
		"the panel does not explain purchase availability")
	require("buyButton->block(true)" in body, "unavailable purchases leave their buy controls active")
	require("getReplacedWarMachine(offer.artifact)" in body, "same-machine/replacement state is not checked")
	require('"vcmi.townWindow.blacksmith.replaceWarMachine"' in body,
		"the existing replacement warning is missing")
	require("buyArtifact(GAME->interface()->cb->getHero(hid), artifactId)" in body,
		"the purchase does not use the existing authoritative artifact callback")
	require("artifact->getDescriptionTranslated()" in body,
		"machine help is missing")
	require('ImagePath::builtin("newHorizonsOrdersBackground.png")' in source,
		"the panel does not reuse the continuous New Horizons leather background")
	require("TransparentFilledRectangle>(Rect(shopLeft, shopTop, shopWidth, shopHeight)" in body,
		"the three rows are not joined inside one recessed framed surface")
	require('AnimationPath::builtin("IBUY30.DEF")' in body,
		"the native tactile buy control is missing")
	require("vstd::next(EShortcut::SELECT_INDEX_1, index)" in body,
		"non-default offers do not retain distinct numeric keyboard choices")
	require("offer.artifact == preferredArtifact ? EShortcut::GLOBAL_ACCEPT" in body,
		"Enter no longer accepts the machine associated with the clicked shop")
	require('AnimationPath::builtin("NH_cancel_button")' in body,
		"the New Horizons close control is missing")
	geometry_checks(body)


if __name__ == "__main__":
	check(SOURCE, HEADER)
	print("New Horizons War Machine shop source and 640x500 geometry guard passed")
