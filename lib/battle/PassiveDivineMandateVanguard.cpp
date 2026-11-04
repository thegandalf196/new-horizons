/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PassiveDivineMandateVanguard.h"
#include "HeroCommand.h"
#include "Unit.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/BonusList.h"
#include <stdexcept>

namespace newHorizonsPassiveDivineMandate
{
bool isVanguardRiderEntitlement(const Bonus & bonus)
{
	return bonus.type == BonusType::NONE && bonus.source == BonusSource::HERO_COMMAND
		&& bonus.valType == BonusValueType::BASE_NUMBER && bonus.stacking == "vanguard";
}

void validateVanguardRiderEntitlement(const Bonus & bonus)
{
	if(!isVanguardRiderEntitlement(bonus) || bonus.duration != BonusDuration::N_TURNS
		|| bonus.val <= 0 || bonus.val > 0x3ff || bonus.turnsRemain < 0 || bonus.turnsRemain > 3
		|| (bonus.subtype != BonusSubtypeID(BonusCustomSubtype(1)) && bonus.subtype != BonusSubtypeID(BonusCustomSubtype(2))))
		throw std::runtime_error("Invalid Vanguard Divine Mandate rider entitlement");
}

uint16_t vanguardRiderPerks(const battle::Unit * unit, BattleSide currentSide)
{
	if(!unit || !unit->alive() || unit->isGhost() || unit->isTurret()
		|| unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| unit->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
		|| (currentSide != BattleSide::ATTACKER && currentSide != BattleSide::DEFENDER)
		|| !unit->hasBonusOfType(BonusType::NONE) || !heroCommands::activeVanguardBonuses(unit))
		return 0;
	uint16_t flags = 0;
	const auto markers = unit->getBonuses(CSelector([](const Bonus * bonus)
	{
		return bonus && isVanguardRiderEntitlement(*bonus);
	}));
	for(const auto & marker : *markers)
	{
		validateVanguardRiderEntitlement(*marker);
		if(marker->subtype == BonusSubtypeID(BonusCustomSubtype(static_cast<int>(currentSide) + 1)) && marker->turnsRemain > 0)
			flags |= static_cast<uint16_t>(marker->val);
	}
	return flags;
}
}
