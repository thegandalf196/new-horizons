/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSepulchralGrip.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "../CCreatureHandler.h"
#include "../entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include <stdexcept>
namespace newHorizonsSepulchralGrip
{
namespace { constexpr auto identity = "new-horizons:ordinary-creature-specialty:core:straker:sepulchralGrip"; }
bool isEffect(const Bonus * bonus)
{
	if(!bonus || bonus->stacking != identity)
		return false;
	if(bonus->source != BonusSource::HERO_SPECIAL
		|| bonus->sid != BonusSourceID(HeroTypeID(HeroTypeID::decode("core:straker")))
		|| bonus->type != BonusType::STACKS_MOVEMENT_RANGE || bonus->subtype != BonusSubtypeID()
		|| bonus->duration != BonusDuration::STACK_ACTIVATION
		|| bonus->valType != BonusValueType::ADDITIVE_VALUE || bonus->val >= 0 || bonus->val < -100
		|| bonus->targetSourceType != BonusSource::OTHER || bonus->turnsRemain != 0
		|| bonus->effectRange != BonusLimitEffect::NO_LIMIT || bonus->appliedByEnemy
		|| bonus->bonusOwner != PlayerColor::CANNOT_DETERMINE
		|| bonus->spellCasterOwner != PlayerColor::CANNOT_DETERMINE
		|| bonus->statusIdentity != "sepulchralGrip"
		|| bonus->statusTags.size() != 2
		|| bonus->statusTags[0] != BonusStatusTag::DEBUFF
		|| bonus->statusTags[1] != BonusStatusTag::NON_TRANSFERABLE
		|| bonus->parameters || bonus->limiter || bonus->propagator || bonus->updater || bonus->propagationUpdater
		|| !bonus->customIconPath.empty())
		throw std::runtime_error("Invalid Sepulchral Grip specialty provenance");
	return true;
}
std::shared_ptr<Bonus> primaryHit(const CBattleInfoCallback &, const BattleAttackInfo &)
{
	return nullptr;
}
}
