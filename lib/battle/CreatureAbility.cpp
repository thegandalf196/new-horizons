/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CreatureAbility.h"
#include "ConsecratedArms.h"
#include "Unit.h"
#include "../bonuses/BonusEnum.h"
#include "../bonuses/Bonus.h"
#include "../json/JsonNode.h"
#include "../spells/CSpell.h"
#include <array>
#include <stdexcept>

void CreatureAbilityDescriptor::validate() const
{
	using namespace creatureAbility;
	if(family > Family::SKILL || (categories & ~uint8_t{0x3f}) || (interactions & ~uint8_t{0x1f})
		|| resolution > Resolution::DIRECT_APPLICATION || categories == 0)
		throw std::runtime_error("Invalid Creature Ability effect descriptor");
	if(!sourceOwner.isValidPlayer() && sourceOwner != PlayerColor::CANNOT_DETERMINE
		&& sourceOwner != PlayerColor::NEUTRAL && sourceOwner != PlayerColor::UNFLAGGABLE)
		throw std::runtime_error("Invalid Creature Ability source owner");
	std::visit([](const auto & id)
	{
		using T = std::decay_t<decltype(id)>;
		if constexpr(std::is_same_v<T, SpellID> || std::is_same_v<T, ScriptID>)
		{
			if(!id.hasValue()) throw std::runtime_error("Missing ability backing identity");
		}
		else if constexpr(std::is_same_v<T, Native>)
		{
			if(id > Native::COORDINATED_MARK) throw std::runtime_error("Unknown native ability identity");
		}
		else if(id == BonusType::NONE || id >= BonusType::BUILTIN_BONUSES_COUNT)
			throw std::runtime_error("Invalid ability trait identity");
	}, identity);
}

bool CreatureAbilityDescriptor::operator==(const CreatureAbilityDescriptor & other) const
{
	return family == other.family && categories == other.categories && interactions == other.interactions
		&& resolution == other.resolution && sourceOwner == other.sourceOwner && identity == other.identity;
}

namespace creatureAbility
{
Descriptor nativeAffliction(Native identity, Family family, PlayerColor owner)
{
	auto effect = std::make_shared<CreatureAbilityDescriptor>();
	effect->family = family;
	effect->categories = HOSTILE_AFFLICTION;
	effect->interactions = PREVENTABLE;
	if(identity == Native::PHYSICAL_POISON || identity == Native::CONCUSSIVE_STUN || identity == Native::FROZEN
		|| identity == Native::CRIPPLE || identity == Native::GRAVE_DUST)
		effect->interactions |= REMOVABLE;
	if(identity == Native::ORDINARY_BURN) effect->categories |= DAMAGE;
	effect->identity = identity;
	effect->sourceOwner = owner;
	effect->validate();
	return effect;
}

Descriptor spellEffect(SpellID spell, Family family, uint8_t categories, uint8_t interactions, PlayerColor owner)
{
	auto effect = std::make_shared<CreatureAbilityDescriptor>();
	effect->family = family;
	effect->categories = categories;
	effect->interactions = interactions;
	effect->resolution = Resolution::SPELL_CAST;
	effect->identity = spell;
	effect->sourceOwner = owner;
	effect->validate();
	return effect;
}

Descriptor fromInnateBonus(const Bonus & bonus)
{
	if(bonus.source != BonusSource::CREATURE_ABILITY || bonus.type == BonusType::NONE)
		return {};
	auto effect = std::make_shared<CreatureAbilityDescriptor>();
	effect->family = Family::CREATURE_ABILITY;
	effect->categories = PASSIVE;
	effect->identity = bonus.type;
	effect->validate();
	return effect;
}

Descriptor onHitSpell(const CSpell & spell, PlayerColor owner)
{
	const auto id = spell.getId();
	const bool affliction = !spell.isDamage() && (spell.isNegative()
		|| id == SpellID::STONE_GAZE || id == SpellID::BIND || id == SpellID::ACID_BREATH_DEFENSE);
	const uint8_t categories = spell.isDamage() ? DAMAGE
		: affliction ? HOSTILE_AFFLICTION : spell.isPositive() ? BENEFICIAL : PASSIVE;
	uint8_t interactions = affliction ? PREVENTABLE : 0;
	if(spell.isMagical() && spell.isNegative()) interactions |= NORMAL_RESISTIBLE;
	if(spell.isMagical() && !spell.isDamage()) interactions |= DISPELLABLE | REMOVABLE;
	if(id == SpellID::POISON || id == SpellID::DISEASE) interactions |= REMOVABLE;
	return spellEffect(id, Family::CREATURE_ABILITY, categories, interactions, owner);
}

bool sameDescriptor(const Descriptor & left, const Descriptor & right)
{
	return left == right || (left && right && *left == *right);
}

int unshakableResistanceBP(const CreatureAbilityDescriptor & effect, const battle::Unit & target)
{
	effect.validate();
	if(effect.family != Family::CREATURE_ABILITY || !(effect.categories & HOSTILE_AFFLICTION)
		|| ((effect.categories & DAMAGE) && effect.resolution == Resolution::SPELL_CAST)
		|| !(effect.interactions & PREVENTABLE) || (effect.interactions & IRRESISTIBLE))
		return 0;
	const auto prayer = consecratedArms::activePrayer(target, consecratedArms::Prayer::STEADFAST_SOUL);
	if(!prayer || !prayer->policy.has(consecratedArms::Perk::UNSHAKABLE_FAITH))
		return 0;
	return consecratedArms::prayerValues(target, consecratedArms::Prayer::STEADFAST_SOUL).resistanceBP;
}
}
