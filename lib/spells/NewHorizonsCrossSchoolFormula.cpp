/* NewHorizonsCrossSchoolFormula.cpp, part of VCMI; GPL v2.0 or later. */
#include "StdInc.h"
#include "NewHorizonsCrossSchoolFormula.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "CSpellHandler.h"
#include "../GameLibrary.h"
#include "../battle/IBattleState.h"
#include "../mapObjects/CGHeroInstance.h"
#include <algorithm>

namespace newHorizonsCrossSchoolFormula
{
namespace
{
bool eligible(const IBattleInfo & battle, BattleSide side, SpellID spell)
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER) || battle.getRound() < 0
		|| !newHorizonsMagic::rulesActive(battle.getMagicRules())
		|| spell.num < 0 || static_cast<size_t>(spell.num) >= LIBRARY->spellh->objects.size()
		|| !newHorizonsMagic::spellAllowedByHeroRoster(battle.getMagicRules(), spell))
		return false;
	const auto * hero = battle.getSideHero(side);
	return hero && hero->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL), std::string(PERK));
}
}

bool differentSchools(const std::vector<SpellSchool> & prior, const std::vector<SpellSchool> & next)
{
	return !prior.empty() && !next.empty() && std::none_of(next.begin(), next.end(), [&prior](SpellSchool school)
		{ return std::find(prior.begin(), prior.end(), school) != prior.end(); });
}

void validateState(const IBattleInfo & battle, const State & state, bool supported)
{
	state.validate();
	if(!state.hasState())
		return;
	if(!supported || state.round > battle.getRound() || state.spell.num < 0
		|| static_cast<size_t>(state.spell.num) >= LIBRARY->spellh->objects.size()
		|| !newHorizonsMagic::rulesActive(battle.getMagicRules())
		|| !newHorizonsMagic::spellAllowedByHeroRoster(battle.getMagicRules(), state.spell)
		|| newHorizonsMagic::spellSchools(battle.getMagicRules(), state.spell).empty())
		throw std::runtime_error("Invalid or unsupported saved Cross-School Formula history");
}

std::optional<Receipt> acceptedReceipt(const IBattleInfo & battle, BattleSide side, SpellID spell)
{
	if(!eligible(battle, side, spell)
		|| newHorizonsMagic::spellSchools(battle.getMagicRules(), spell).empty())
		return std::nullopt;
	const auto & previous = battle.getCrossSchoolFormulaState(side);
	validateState(battle, previous);
	return Receipt{previous, State{spell, battle.getRound()}};
}

int bonusPercent(const IBattleInfo & battle, BattleSide side, SpellID spell)
{
	if(!eligible(battle, side, spell))
		return 0;
	const auto & previous = battle.getCrossSchoolFormulaState(side);
	validateState(battle, previous);
	if(!previous.hasState() || static_cast<int64_t>(battle.getRound()) - previous.round > 1)
		return 0;
	const auto priorSchools = newHorizonsMagic::spellSchools(battle.getMagicRules(), previous.spell);
	const auto nextSchools = newHorizonsMagic::spellSchools(battle.getMagicRules(), spell);
	if(!differentSchools(priorSchools, nextSchools))
		return 0;
	return BONUS_PERCENT;
}
}
