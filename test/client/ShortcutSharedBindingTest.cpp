/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../client/gui/Shortcut.h"
#include "../../client/gui/ShortcutHandler.h"

// The battle opening captures the first shortcut of a pressed key; Q must still reach
// "end with autocombat" although its alphabetically earlier bindings are delivered first.
TEST(ShortcutSharedBinding, AutocombatEndKeySharesItsBindingsOnly)
{
	const ShortcutHandler shortcuts;
	const auto onQ = shortcuts.translateKeycode("Q");
	ASSERT_NE(std::find(onQ.begin(), onQ.end(), EShortcut::BATTLE_END_WITH_AUTOCOMBAT), onQ.end());
	EXPECT_NE(onQ.front(), EShortcut::BATTLE_END_WITH_AUTOCOMBAT);

	EXPECT_TRUE(shortcuts.sharesBinding(EShortcut::ADVENTURE_QUEST_LOG, EShortcut::BATTLE_END_WITH_AUTOCOMBAT));
	EXPECT_TRUE(shortcuts.sharesBinding(EShortcut::MAIN_MENU_QUIT, EShortcut::BATTLE_END_WITH_AUTOCOMBAT));
	EXPECT_TRUE(shortcuts.sharesBinding(EShortcut::BATTLE_END_WITH_AUTOCOMBAT, EShortcut::BATTLE_END_WITH_AUTOCOMBAT));
	EXPECT_FALSE(shortcuts.sharesBinding(EShortcut::BATTLE_WAIT, EShortcut::BATTLE_END_WITH_AUTOCOMBAT));
	EXPECT_FALSE(shortcuts.sharesBinding(EShortcut::GLOBAL_CANCEL, EShortcut::BATTLE_END_WITH_AUTOCOMBAT));
}
