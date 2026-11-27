/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleSpellMechanics.h"
#include "NewHorizonsInfernoSpecialties.h"
#include "TargetCondition.h"
#include "CSpell.h"
#include "NewHorizonsMetamagicCore.h"
#include "NewHorizonsMagicalMemory.h"
#include "NewHorizonsSorcerySpells.h"
#include "NewHorizonsRealityWarp.h"
#include "NewHorizonsPandemonium.h"
#include "NewHorizonsNaturesWrath.h"
#include "../battle/NewHorizonsPuppetMaster.h"
#include "NewHorizonsBlink.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
namespace spells
{
bool BattleSpellMechanics::isReceptive(const battle::Unit * target) const
{
	if(target && target->alive() && !target->isInvincible() && infernoSpellSpecialties::bloodfireProtects(*this, *target)) return true;
	if(owner->getJsonKey() == newHorizonsSorcerySpells::FORCE_LANCE)
		return target && target->alive() && target->isValidTarget(false) && !target->isInvincible() && ownerMatches(target);
	if(target && (newHorizonsMetamagic::empoweredDamage(*this)
		|| newHorizonsMetamagic::explicitlyIrresistible(*this)))
		if(const auto * conditions = dynamic_cast<const TargetCondition *>(targetCondition.get()))
			return conditions->isReceptiveIgnoringMagicResistance(this, target);
	// Memory prevents an exact enchantment family, never a damage component of a
	// mixed spell. Packet filtering independently guards the enchantment records.
	if(target && !owner->isDamage() && battle() && battle()->getBattle()
		&& newHorizonsMetamagic::blocksEnchantment(*target, *battle()->getBattle(),
			owner->getId(), owner->isPositive(), owner->isNegative()))
		return false;
	if(target && owner->getJsonKey() == newHorizonsRealityWarp::SPELL_KEY && usesNewHorizonsMagicV3())
	{
		// The paired cast rolls hostile resistance once during beforeCast;
		// captured-effect recipient checks themselves are deterministic.
		if(const auto * conditions = dynamic_cast<const TargetCondition *>(targetCondition.get()))
			return conditions->isReceptiveIgnoringMagicResistance(this, target);
	}
	if(target && newHorizonsPandemonium::enabled(*this))
	{
		// Preview and damage adjustment retain all immunity conditions, while
		// actual casts consume the existing cached resistance roll exactly once.
		if(const auto * conditions = dynamic_cast<const TargetCondition *>(targetCondition.get()))
			return conditions->isReceptiveIgnoringMagicResistance(this, target);
	}
	if(target && newHorizonsNaturesWrath::enabled(*this))
	{
		// The ordered hostile-recipient consumer handles resistance, not the
		// routing or heal immunity check. All other immunity conditions remain.
		if(const auto * conditions = dynamic_cast<const TargetCondition *>(targetCondition.get()))
			return conditions->isReceptiveIgnoringMagicResistance(this, target);
	}
	if(target && newHorizonsPuppetMaster::isMentalControlSpell(owner->getJsonKey())
		&& newHorizonsPuppetMaster::hasLucidity(target))
		return false;

	if(target && owner->getJsonKey() == newHorizonsBlink::SPELL_ID && usesNewHorizonsMagicV3()
		&& target->unitSide() == casterSide)
	{

		if(const auto * conditions = dynamic_cast<const TargetCondition *>(targetCondition.get()))
			return conditions->isReceptiveIgnoringMagicResistance(this, target);
	}

	return target
		&& targetCondition->isReceptive(this, target);
}

}
