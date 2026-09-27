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
require(SPLIT_CPP, "Rect(5, 259, 130, 27)", "fallback/ambiguous owner labels must sit below the creature art")
require(SPLIT_CPP, "std::make_shared<CSplitOwnerLabel>", "ambiguous owner labels must use the bounded, interactive label")
require(SPLIT_CPP, "constexpr int maxWidth = 122;", "fallback text must leave a horizontal inset within its fixed label box")
require(SPLIT_CPP, "font->getStringWidth((displayText + ellipsis).c_str()) > maxWidth", "fallback names must be measured before truncation")
require(SPLIT_CPP, "TextOperations::trimRightUnicode(displayText);", "fallback truncation must preserve UTF-8 codepoints")
require(SPLIT_CPP, "addUsedEvents(HOVER | SHOW_POPUP);", "full owner names must be available through label hover and right-click")
require(SPLIT_CPP, "ENGINE->statusbar()->write(fullText);", "hovering an abbreviated label must expose its full name")
require(SPLIT_CPP, "CRClickPopup::createAndPush(fullText);", "right-clicking an abbreviated label must show its full name")
require(SPLIT_CPP, "Point(21, 275 + ownerLabelControlOffset)", "slider must sit below the owner indicator row")
require(SPLIT_CPP, "Rect(20, 302 + ownerLabelControlOffset, 100, 36)", "numeric amounts must remain below the slider")
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
button_layout = re.search(
    r"ok = std::make_shared<CButton>\(Point\(20, (\d+) \+ ownerLabelControlOffset\), AnimationPath::builtin\(\"IOK(\d{2})(\d{2})\"",
    SPLIT_CPP,
)
label_offset = re.search(r"const int ownerLabelControlOffset = .*\? (\d+) : (\d+);", SPLIT_CPP)
assert button_layout and label_offset, "button and fallback-label positions must remain tied to the split layout"
button_y, _button_width, button_height = (int(value) for value in button_layout.groups())
max_label_offset = max(int(value) for value in label_offset.groups())
controls_bottom = button_y + max_label_offset + button_height
footer_top = dialog_height - footer_height
assert controls_bottom + 10 <= footer_top, "the tallest split-dialog button must end above the lower frame with a margin"
require(GARRISON, 'translate("core.tcommand.5")', "a one-creature last-stack no-op must explain the constraint")
require(GARRISON, "if(amount <= 0)", "zero-amount last-stack transfers must be stopped before a split request")
require(
    GARRISON,
    "GAME->interface()->cb->mergeOrSwapStacks(selectedObj, owner->army(upg), selection->ID, ID);",
    "positive last-stack empty-slot transfers must use the server-clamped whole-stack intent",
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
