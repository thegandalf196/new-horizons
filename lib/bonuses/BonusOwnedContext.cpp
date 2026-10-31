/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BonusEvaluationContext.h"
#include "CBonusSystemNode.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/army/CArmedInstance.h"
#include "../mapObjects/army/CStackInstance.h"
#include <stdexcept>
BonusEvaluationContext::BonusEvaluationContext(const CBonusSystemNode & shell, const OwnedBonusEvaluationFacts & facts)
	: original(shell), owned(&facts)
{
	facts.validate();
	if(shell.getNodeType() != facts.nodeType)
		throw std::invalid_argument("Owned bonus shell and facts disagree");
}
BonusEvaluationContext::BonusEvaluationContext(const CBonusSystemNode & shell,
	const OwnedBonusEvaluationFacts & facts, const battle::Unit & unit)
	: BonusEvaluationContext(shell, facts)
{
	if(facts.nodeType != BonusNodeType::STACK_BATTLE)
		throw std::invalid_argument("Owned current unit requires a battle recipient shell");
	projected = &unit; // Borrowed only through canonical collection/fixedpoint completion.
}
std::optional<int> BonusEvaluationContext::heroLevel() const
{
	if(getNodeType() != BonusNodeType::HERO) return std::nullopt;
	return owned ? owned->heroLevel : std::optional<int>(dynamic_cast<const CGHeroInstance &>(original).level);
}
namespace
{
struct ArmySlotView
{
	const CStackInstance * native = nullptr;
	const OwnedBonusEvaluationFacts::ArmySlot * owned = nullptr;
	CreatureID creature() const { return native ? native->getCreatureID() : owned->creature; }
	FactionID faction() const { return native ? native->getFactionID() : owned->faction; }
	int level() const { return native ? native->getLevel() : owned->level; }
	int count() const { return native ? native->getCount() : owned->count; }
};
}
std::optional<int> BonusEvaluationContext::filteredArmySize(CreatureID creature, FactionID faction, int level) const
{
	if(getNodeType() != BonusNodeType::HERO && getNodeType() != BonusNodeType::ARMY
		&& getNodeType() != BonusNodeType::TOWN) return std::nullopt;
	int totalSize = 0;
	const auto append = [&](const ArmySlotView & unit)
	{
		if(creature.hasValue() && creature != unit.creature()) return;
		if(faction.hasValue() && faction != unit.faction()) return;
		if(level != -1 && level != unit.level()) return;
		totalSize += unit.count();
	};
	if(owned)
		for(const auto & unit : *owned->armySlots) append({nullptr, &unit});
	else
		for(const auto & unit : dynamic_cast<const CArmedInstance &>(original).Slots()) append({unit.second.get(), nullptr});
	return totalSize;
}
