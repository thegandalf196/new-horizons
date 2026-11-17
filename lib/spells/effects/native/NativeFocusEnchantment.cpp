/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "UnitEffect.h"
#include "StatusHelpers.h"
#include "../../NewHorizonsSorcerySpells.h"
#include "../../NewHorizonsSorceryRevision.h"
#include "../../ISpellMechanics.h"
#include "../../../battle/NewHorizonsFocusMagic.h"
#include "../../../networkPacks/SetStackEffect.h"
#include "../../../mapObjects/CGHeroInstance.h"

namespace spells::effects::native
{
namespace
{
class FocusEnchantment final : public UnitEffect
{
	bool isValidTarget(const Mechanics * mechanics, const battle::Unit * unit) const override
	{
		return unit && unit->alive() && unit->isValidTarget(false) && mechanics->ownerMatches(unit)
			&& !unit->hasBonusOfType(BonusType::SIEGE_WEAPON);
	}
public:
	Target transformTarget(const Mechanics * mechanics, const Target & aim, const Target & target) const override
	{
		auto result = UnitEffect::transformTarget(mechanics, aim, target);
		if(result.size() > 1) result.resize(1);
		return result;
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		if(target.size() != 1 || !isValidTarget(mechanics, target.front().unitValue)
			|| !isReceptive(mechanics, target.front().unitValue)) return;
		const auto & unit = *target.front().unitValue;
		SetStackEffect pack;
		pack.battleID = mechanics->getBattleID();
		std::vector<Bonus> previous;
		const auto bonuses = unit.getBonusesFrom(BonusSource::SPELL_EFFECT);
		for(const auto & bonus : *bonuses)
			if(bonus->sid == BonusSourceID(mechanics->getSpellId())) previous.push_back(*bonus);
		if(!previous.empty()) pack.toRemove.emplace_back(unit.unitId(), std::move(previous));
		pack.toAdd.emplace_back(unit.unitId(), newHorizonsFocusMagic::enchantment(unit, mechanics->getSpellId(),
			newHorizonsSorceryRevision::focusAdditionalRangedPercent(mechanics->getHeroCaster(), mechanics->getSpellId())));
		server->apply(pack);
		server->recordResolvedSpellRecipient(unit.unitId());
	}
};
}
}

namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createFocusEnchantmentEffect(const std::string & path)
{
	return path == "spells/focusMagicEnchantment" ? std::make_shared<spells::effects::native::FocusEnchantment>() : nullptr;
}
}
