/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClient.h"

void HypotheticBattle::restoreProjectedNormalMana(const SetMana & packet)
{
	packet.validateOperationPayload();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto * hero = battleGetFightingHero(side);
		if(!hero || hero->id != packet.hid)
			continue;
		auto & mana = projectedMana.at(side);
		if(!mana)
			mana = ProjectedMana{*getProjectedMana(side), getTemporaryBufferRemaining(side)};
		mana->pools.restoreNormal(static_cast<int32_t>(std::min<int64_t>(packet.amount,
			std::numeric_limits<int32_t>::max())), hero->manaLimit());
		return;
	}
	throw std::invalid_argument("Projected Mana recovery requires a fighting hero");
}
