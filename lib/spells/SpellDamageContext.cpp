/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ISpellMechanics.h"
#include "SpellDamageContext.h"
#include "BattleSpellMechanics.h"
#include "CSpell.h"
#include "../battle/ConsecratedArms.h"
#include "../battle/Unit.h"
namespace spells
{
battle::DamageContext Mechanics::spellDamageContext(const battle::Unit &) const
{
	const auto descriptor = getCreatureAbilityDescriptor();
	const bool ability = descriptor && descriptor->family == creatureAbility::Family::CREATURE_ABILITY;
	return {isMagicalEffect() ? battle::DamageClass::MAGICAL : battle::DamageClass::PHYSICAL,
		ability ? battle::DamageOrigin::CREATURE_ABILITY : getUnitCaster()
			? battle::DamageOrigin::CREATURE_SPELL : battle::DamageOrigin::HERO_SPELL};
}

battle::DamageContext previewSpellDamageContext(const Mechanics & mechanics, const battle::Unit & recipient,
	std::optional<battle::VowCasualtyQuota> & storage)
{
	auto context = mechanics.Mechanics::spellDamageContext(recipient);
	if(!mechanics.getSpell()->isDamage()) return context;
	if(consecratedArms::activePrayer(recipient, consecratedArms::Prayer::UNBROKEN_VOW))
	{
		const auto values = consecratedArms::prayerValues(recipient, consecratedArms::Prayer::UNBROKEN_VOW);
		storage = battle::VowCasualtyQuota::capture(recipient.unitId(), recipient.getCount(), values.casualtyCeilingBP);
		context.vow = &*storage;
	}
	return context;
}

battle::DamageContext BattleSpellMechanics::spellDamageContext(const battle::Unit & recipient) const
{
	auto context = Mechanics::spellDamageContext(recipient);
	if(!getSpell()->isDamage()) return context;
	context.redemption = &spellRedemption;
	const auto prayer = consecratedArms::activePrayer(recipient, consecratedArms::Prayer::UNBROKEN_VOW);
	if(!prayer) return context;
	auto existing = spellCasualtyQuotas.find(recipient.unitId());
	if(existing == spellCasualtyQuotas.end())
	{
		const auto values = consecratedArms::prayerValues(recipient, consecratedArms::Prayer::UNBROKEN_VOW);
		existing = spellCasualtyQuotas.emplace(recipient.unitId(), battle::VowCasualtyQuota::capture(
			recipient.unitId(), recipient.getCount(), values.casualtyCeilingBP)).first;
	}
	context.vow = &existing->second;
	return context;
}
}
