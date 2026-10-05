#!/usr/bin/env python3
"""Guard the Leadership empty-slot repair and split-dialog owner indicators."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
SERVER = (ROOT / "server/CGameHandler.cpp").read_text(encoding="utf-8")
GARRISON = (ROOT / "client/widgets/CGarrisonInt.cpp").read_text(encoding="utf-8")
SPLIT_CPP = (ROOT / "client/windows/GUIClasses.cpp").read_text(encoding="utf-8")
SPLIT_H = (ROOT / "client/windows/GUIClasses.h").read_text(encoding="utf-8")
ASSET_GENERATOR = (ROOT / "client/render/AssetGenerator.cpp").read_text(encoding="utf-8")
SERVER_TEST = (ROOT / "test/server/NewHorizonsLeadershipAdmissionTest.cpp").read_text(encoding="utf-8")


def require(source: str, fragment: str, message: str) -> None:
    assert fragment in source, message


require(
    SERVER,
    "Whole-stack drags are move intents: reserve a required last creature",
    "server must reserve the final creature before calculating a whole-stack move",
)
require(
    SERVER,
    "transferCount = std::min<int64_t>(transferCount, capacity->maximum);",
    "server must authoritatively cap the move at current Leadership capacity",
)
require(
    SERVER,
    "return moveStack(sourceLocation, destinationLocation, static_cast<TQuantity>(transferCount));",
    "server must send the calculated amount through validated stack movement",
)
require(
    GARRISON,
    "The server treats a whole-stack drag into an empty hero slot",
    "client fast path must allow the server-authored partial transfer",
)
require(GARRISON, 'owner.label = "Hero: " + name;', "split dialog must keep a hero-name fallback")
require(
    GARRISON,
    '"Garrison: " + name',
    "split dialog must keep a garrison-name fallback",
)
split_window_declaration = SPLIT_H.split("class CSplitWindow", 1)[1].split("class CLevelWindow", 1)[0]
require(split_window_declaration, "CSplitWindowOwner leftOwner", "split sides must carry army identity for markers")
require(split_window_declaration, "std::shared_ptr<CAnimImage> leftOwnerMarker;", "split window must retain its left owner marker")
require(split_window_declaration, "std::shared_ptr<CAnimImage> rightOwnerMarker;", "split window must retain its right owner marker")
require(SPLIT_CPP, 'AnimationPath::builtin("PortraitsLarge")', "hero sides must use their built-in portrait")
require(SPLIT_CPP, 'AnimationPath::builtin("CREST58")', "owned garrison sides must use their player crest")
require(SPLIT_CPP, "Rect(5, 261, 130, 15)", "fallback/ambiguous owner labels must sit below the creature art")
require(SPLIT_CPP, "std::make_shared<CSplitOwnerLabel>", "ambiguous owner labels must use the bounded, interactive label")
require(SPLIT_CPP, "constexpr int maxWidth = 122;", "fallback text must leave a horizontal inset within its fixed label box")
require(SPLIT_CPP, "font->getStringWidth((displayText + ellipsis).c_str()) > maxWidth", "fallback names must be measured before truncation")
require(SPLIT_CPP, "TextOperations::trimRightUnicode(displayText);", "fallback truncation must preserve UTF-8 codepoints")
require(SPLIT_CPP, "addUsedEvents(HOVER | SHOW_POPUP);", "full owner names must be available through label hover and right-click")
require(SPLIT_CPP, "ENGINE->statusbar()->write(fullText);", "hovering an abbreviated label must expose its full name")
require(SPLIT_CPP, "CRClickPopup::createAndPush(fullText);", "right-clicking an abbreviated label must show its full name")
require(SPLIT_CPP, "Point(21, 284)", "slider must sit below the owner indicator row")
require(SPLIT_CPP, "Rect(20, 313, 100, 26)", "numeric amounts must remain below the slider")
require(SPLIT_CPP, "CWindowObject(0, splitDialogBackgroundImage())", "the split dialog must use its taller composed frame")
require(ASSET_GENERATOR, '"newHorizonsSplitBackground-" + color.toString() + ".png"', "the split frame must retain per-player coloring")
split_background = ASSET_GENERATOR.split("AssetGenerator::CanvasPtr AssetGenerator::createSplitDialogBackground", 1)[1].split(
    "AssetGenerator::CanvasPtr AssetGenerator::createNewHorizonsHeroBackground", 1)[0]
dialog_height = int(re.search(r"constexpr int dialogHeight = (\d+);", split_background).group(1))
footer_height = int(re.search(r"constexpr int footerHeight = (\d+);", split_background).group(1))
require(split_background, 'ImagePath::builtin("GPUCRDIV")', "the extended frame must reuse the installed split-dialog artwork")
require(split_background, "constexpr int footerTop = dialogHeight - footerHeight;", "the lower frame must sit at the bottom of the expanded window")
require(split_background, "canvas.draw(original, Point(0, 0), Rect(0, 0, dialogWidth, upperContentHeight));",
        "the original title and creature panels must remain intact")
require(split_background, "canvas.draw(original, Point(0, footerTop), Rect(0, footerSourceTop, dialogWidth, footerHeight));",
        "the expanded dialog must restore the original ornate lower frame")
require(split_background, "auto image = createDialogBackground(Point(dialogWidth, dialogHeight));",
        "the split dialog must keep one continuous leather field")
require(split_background, "frame(Rect(17, 52, 108, 135));",
		"the left creature backdrop must sit in its own native-style bevel")
require(split_background, "frame(Rect(174, 52, 108, 135));",
		"the right creature backdrop must sit in its own native-style bevel")
require(split_background, "inset(Rect(31, 189, 78, 72));",
		"the left owner marker must sit within a recessed plaque")
require(split_background, "inset(Rect(188, 189, 78, 72));",
		"the right owner marker must sit within a recessed plaque")
require(split_background, "inset(Rect(18, 279, 262, 31));",
		"the slider must sit within a recessed plate")
require(split_background, "inset(Rect(18, 312, 104, 28));",
		"the left amount field must sit within its own recessed plate")
require(split_background, "inset(Rect(174, 312, 104, 28));",
		"the right amount field must sit within its own recessed plate")
require(split_background, "inset(Rect(18, 351, 106, 44));",
		"the confirmation button must sit within its own recessed plate")
require(split_background, "inset(Rect(174, 351, 106, 44));",
		"the cancel button must sit within its own recessed plate")
assert "textureTileHeight" not in split_background, "decorated source strips must not repeat across the lower dialog"
button_layout = re.search(
	r"ok = std::make_shared<CButton>\(Point\((\d+), (\d+)\), AnimationPath::builtin\(\"IOK(\d{2})(\d{2})\"",
	SPLIT_CPP,
)
assert button_layout, "the confirmation button must retain its native-size layout"
button_x, button_y, _button_width, button_height = (int(value) for value in button_layout.groups())
cancel_layout = re.search(
	r"cancel = std::make_shared<CButton>\(Point\((\d+), (\d+)\), AnimationPath::builtin\(\"ICN(\d{2})(\d{2})\"",
	SPLIT_CPP,
)
assert cancel_layout, "the cancel button must retain its native-size layout"
cancel_x, cancel_y, _cancel_width, cancel_height = (int(value) for value in cancel_layout.groups())
assert button_x == 38 and cancel_x == 195 and button_y == cancel_y == 355, "footer buttons must align within their matching side wells"
assert 312 + 28 + 8 <= 351, "amount wells must leave a visible leather gap above the button wells"
controls_bottom = button_y + button_height
footer_top = dialog_height - footer_height
assert controls_bottom + 10 <= footer_top, "the tallest split-dialog button must end above the lower frame with a margin"
require(GARRISON, 'translate("core.tcommand.5")', "a one-creature last-stack no-op must explain the constraint")
garrison_click = GARRISON.split("void CGarrisonSlot::clickPressed(", 1)[1].split("void CGarrisonSlot::gesture(", 1)[0]
exact_one_click = "else if(lastHeroStackSelected && selection->myStack->getCount() <= 1"
require(
    garrison_click,
    exact_one_click,
    "cross-army exact-one attempts must be checked before either same-creature merges or empty-slot moves",
)
assert garrison_click.index(exact_one_click) < garrison_click.index("cb->mergeOrSwapStacks(selectedObj"), \
    "exact-one empty-slot intent must show the gameplay explanation before sending a request"
assert garrison_click.index(exact_one_click) < garrison_click.index("cb->mergeStacks(selectedObj"), \
    "exact-one occupied same-creature merge must show the gameplay explanation before sending a request"
radial_move = GARRISON.split("void CGarrisonInt::moveStackToAnotherArmy(", 1)[1].split("void CGarrisonInt::bulkMoveArmy(", 1)[0]
radial_exact_one = "if(isLastStack && selected->myStack->getCount() <= 1)"
require(radial_move, radial_exact_one, "radial/Alt+Ctrl exact-one last-stack attempts must explain the constraint")
assert radial_move.index(radial_exact_one) < radial_move.index("cb->mergeStacks(srcArmy"), \
    "radial occupied merges must preflight exact-one sources before sending a request"
assert radial_move.index(radial_exact_one) < radial_move.index("cb->mergeOrSwapStacks(srcArmy"), \
    "radial empty-slot moves must preflight exact-one sources before sending a request"
require(
    GARRISON,
    "GAME->interface()->cb->mergeOrSwapStacks(selectedObj, owner->army(upg), selection->ID, ID);",
    "positive last-stack empty-slot transfers must use the server-clamped whole-stack intent",
)
require(
    radial_move,
    "GAME->interface()->cb->mergeOrSwapStacks(srcArmy, destArmy, srcSlot, destSlot);",
    "positive radial last-stack empty-slot transfers must use the server-clamped whole-stack intent",
)
require(
    SERVER_TEST,
    "WholeStackDragIntoEmptyHeroSlotFillsLeadershipCapacityAndLeavesRemainder",
    "authoritative regression must cover the reported empty-slot transfer",
)
require(
    SERVER_TEST,
    "LastHeroCreatureDragToEmptyGarrisonReportsGameplayReason",
    "authoritative regression must reject a one-creature last-stack move precisely",
)
require(
    SERVER_TEST,
    "LastHeroStackDragToEmptyGarrisonTransfersAllButOne",
    "authoritative regression must preserve one creature during a garrison transfer",
)
require(
    SERVER_TEST,
    "LastHeroStackMoveToEmptyHeroSlotTransfersMaximumLegalAmountAndKeepsOne",
    "authoritative regression must Leadership-cap a partial last-stack move",
)
require(
    SERVER_TEST,
    "StaleLastStackMoveToEmptyGarrisonDoesNotMutate",
    "authoritative regression must reject a stale last-stack drag without mutation",
)
require(
    SERVER_TEST,
    "garrison->setVisitingHero(hero);",
    "cross-army regression must establish a legal exchange relationship",
)
require(
    SERVER_TEST,
    "ArrangeStacks drag(1, SlotID(0), SlotID(0), hero->id, garrison->id, 0);",
    "regression must use the UI's empty-destination-first swap orientation",
)
require(
    SERVER_TEST,
    "ArrangeStacks emptyDrag(1, SlotID(0), SlotID(0), hero->id, garrison->id, 0);",
    "empty-source regression must exercise the guarded hero-destination branch",
)

print("Leadership empty-slot partial transfer and split owner/layout guards passed.")
