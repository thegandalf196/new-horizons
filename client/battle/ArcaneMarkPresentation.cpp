/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ArcaneMarkPresentation.h"
#include "../GameInstance.h"
#include "../CPlayerInterface.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/NewHorizonsMetamagicMarks.h"

namespace arcaneMarkPresentation
{
namespace
{
std::shared_ptr<CBattleInfoCallback> presentationBattle(const CStack & stack)
{
	const auto * state = stack.getBattle();
	return state && GAME->interface() && GAME->interface()->cb
		? GAME->interface()->cb->getBattle(state->getBattleID()) : nullptr;
}
}

std::array<uint8_t, 2> counts(const CStack & stack)
{
	std::array<uint8_t, 2> result{};
	const auto battle = presentationBattle(stack);
	if(!battle)
		return result;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(battle->battleGetFightingHero(side))
			result[static_cast<size_t>(side)] = stack.metamagicMarks(side);
	return result;
}

std::vector<Readout> forStack(const CStack & stack)
{
	std::vector<Readout> result;
	const auto battle = presentationBattle(stack);
	if(!battle)
		return result;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto * hero = battle->battleGetFightingHero(side);
		const auto marks = stack.metamagicMarks(side);
		if(!hero || !newHorizonsMetamagic::usesArcaneMarks(hero->getPerkState().rules))
			continue;
		const auto facts = newHorizonsMetamagic::profile(hero);
		if(facts.rank <= 0)
			continue;
		const auto name = GAME->translator().translate(hero->getNameTextID());
		result.push_back({"Arcane Marks (" + name + "): " + std::to_string(marks) + "/5",
			name + "'s next spell on this stack has +"
			+ std::to_string(newHorizonsMetamagic::effectivenessBonusPercent(facts, marks))
			+ "% effectiveness on scalable effects."
			"\n\nArcane Marks: " + std::to_string(marks) + "/5. Marks apply after a hero spell resolves,"
			" last for combat and cannot be dispelled or cleansed. Other heroes use their own marks."});
	}
	return result;
}

std::string combinedTooltip(const std::vector<Readout> & readouts)
{
	std::string result = "Arcane Marks";
	for(const auto & entry : readouts)
		result += "\n\n" + entry.compact + "\n" + entry.details;
	return result;
}

std::string compactCounts(const std::array<uint8_t, 2> & marks)
{
	if(marks[0] && marks[1])
		return "A" + std::to_string(marks[0]) + " D" + std::to_string(marks[1]);
	return std::to_string(marks[0] ? marks[0] : marks[1]);
}

std::string targetIndependentEffectNote(const CGHeroInstance & hero)
{
	return newHorizonsMetamagic::usesArcaneMarks(hero.getPerkState().rules)
		&& newHorizonsMetamagic::profile(&hero).rank > 0
		? "\nArcane Marks can increase these values on individual targets." : "";
}

std::string manaUpperBoundText(int ordinaryCost)
{
	return "Mana: up to " + std::to_string(ordinaryCost);
}
}
