/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsStun.h"
#include "Unit.h"
#include "CBattleInfoCallback.h"
#include "PhysicalAffliction.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusParameters.h"
#include "../json/JsonNode.h"
namespace newHorizonsStun
{
namespace
{
constexpr auto IDENTITY = "new-horizons:yog:concussiveStun";
BonusSourceID source()
{
	static const auto id = BonusSourceID(HeroTypeID(HeroTypeID::decode("core:yog")));
	return id;
}
}
bool isMarker(const Bonus * bonus)
{
	if(!bonus || bonus->stacking != IDENTITY) return false;
	const auto metadata = physicalAfflictions::markerMetadata(*bonus);
	if(bonus->source != BonusSource::HERO_SPECIAL || bonus->sid != source()
		|| bonus->type != BonusType::PHYSICAL_AFFLICTION || !metadata || metadata->kind != "stun"
		|| bonus->subtype != BonusSubtypeID() || bonus->val != 0
		|| bonus->duration != BonusDuration::ONE_BATTLE || bonus->valType != BonusValueType::BASE_NUMBER
		|| bonus->turnsRemain != 0 || bonus->effectRange != BonusLimitEffect::NO_LIMIT
		|| bonus->targetSourceType != BonusSource::OTHER || bonus->appliedByEnemy
		|| bonus->bonusOwner != PlayerColor::CANNOT_DETERMINE || bonus->spellCasterOwner != PlayerColor::CANNOT_DETERMINE
		|| bonus->statusIdentity != IDENTITY || bonus->statusTags.size() != 2
		|| !vstd::contains(bonus->statusTags, BonusStatusTag::DEBUFF)
		|| !vstd::contains(bonus->statusTags, BonusStatusTag::NON_TRANSFERABLE)
		|| bonus->limiter || bonus->propagator || bonus->updater || bonus->propagationUpdater
		|| !bonus->customIconPath.empty()) throw std::runtime_error("Invalid Concussive Stun marker");
	return true;
}
bool isStunned(const battle::Unit & unit) { return unit.hasBonus(CSelector(isMarker)); }
bool forfeitsActivation(const CBattleInfoCallback & battle, const battle::Unit & unit, BattleUnitTurnReason reason)
{
	return unit.alive() && !unit.isGhost() && !unit.isTimeStopped() && isStunned(unit)
		&& battle.battleBeginsActivation(&unit, reason);
}
Bonus marker()
{
	Bonus result(BonusDuration::ONE_BATTLE, BonusType::PHYSICAL_AFFLICTION, BonusSource::HERO_SPECIAL,
		0, source(), BonusSubtypeID(), BonusValueType::BASE_NUMBER);
	result.stacking = IDENTITY;
	result.statusIdentity = IDENTITY;
	result.statusTags = {BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE};
	JsonNode metadata;
	metadata["kind"].String() = "stun";
	metadata["applicationOrder"].Integer() = 0;
	result.parameters = std::make_shared<BonusParameters>(metadata);
	result.description.appendRawString("Stunned: loses the next Creature Activation.");
	return result;
}
std::vector<Bonus> removalPlan(const battle::Unit & unit)
{
	std::vector<Bonus> result;
	const auto markers = unit.getAllBonuses(CSelector(isMarker));
	for(const auto & bonus : *markers) result.push_back(*bonus);
	return result;
}
}
