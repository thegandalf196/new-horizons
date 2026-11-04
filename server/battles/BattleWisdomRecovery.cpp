/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleWisdomRecovery.h"
#include "../CGameHandler.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/NewHorizonsWisdom.h"
void applyWisdomBattleRecovery(CGameHandler & game, BattleID battle, const CGHeroInstance * hero,
	int64_t spent, bool retained, bool victorious, bool completedCast)
{
	if(!hero || !retained) return;
	const auto recover = [&](int32_t requested, const char * label)
	{
		if(requested <= 0) return;
		const auto before = hero->getNormalSpellPoints();
		game.restoreSpellPoints(hero->id, requested);
		const auto restored = hero->getNormalSpellPoints() - before;
		if(restored <= 0) return;
		MetaString line = MetaString::createFromTextID(hero->getNameTextID());
		line.appendRawString(std::string(": ") + label + " restores ");
		line.appendNumber(restored);
		line.appendRawString(" Normal Spell Points after combat.");
		BattleLogMessage message;
		message.battleID = battle;
		message.lines.push_back(std::move(line));
		game.sendAndApply(message);
	};
	recover(newHorizonsWisdom::conservationRecovery(*hero, spent), "Mana Conservation");
	recover(newHorizonsWisdom::fastingRecovery(*hero, victorious, completedCast), "Mana Fasting");
}
