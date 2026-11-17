/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include "../constants/EntityIdentifiers.h"
#include "../serializer/Serializeable.h"
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

class JsonNode;
class CSpell;
struct Bonus;
enum class BonusType : uint16_t;
namespace battle { class Unit; }
namespace creatureAbility
{
enum class Family : uint8_t { UNKNOWN, CREATURE_ABILITY, HERO_SPELL, CREATURE_SPELL, BATTLEFIELD, SKILL };
enum Category : uint8_t
{
	DAMAGE = 1 << 0, HOSTILE_AFFLICTION = 1 << 1, BENEFICIAL = 1 << 2,
	RESTORATION = 1 << 3, MOVEMENT = 1 << 4, PASSIVE = 1 << 5
};
enum Interaction : uint8_t
{
	NORMAL_RESISTIBLE = 1 << 0, REMOVABLE = 1 << 1, DISPELLABLE = 1 << 2,
	PREVENTABLE = 1 << 3, IRRESISTIBLE = 1 << 4
};
enum class Resolution : uint8_t { SPELL_CAST, DIRECT_APPLICATION };
/// Only native producers lacking an existing SpellID/CombatScriptID use these
/// stable identities. They correspond to existing afflictions, not new abilities.
enum class Native : uint8_t
{
	PHYSICAL_POISON, CONCUSSIVE_STUN, FROZEN, SERPENTS_EMBRACE, PARALYZING_GAZE,
	CRIPPLE, GRAVE_DUST, SCORCHING_IMPACT, CRUEL_OVERSEER, ORDINARY_BURN, COORDINATED_MARK
};
}

/// Immutable once published through a shared const handle. Family records the
/// effect's producer; Bonus.source/sid separately retain who granted the ability.
struct DLL_LINKAGE CreatureAbilityDescriptor : public Serializeable
{
	creatureAbility::Family family = creatureAbility::Family::UNKNOWN;
	uint8_t categories = 0;
	uint8_t interactions = 0;
	creatureAbility::Resolution resolution = creatureAbility::Resolution::DIRECT_APPLICATION;
	PlayerColor sourceOwner = PlayerColor::CANNOT_DETERMINE;
	std::variant<SpellID, ScriptID, BonusType, creatureAbility::Native> identity = SpellID::NONE;
	void validate() const;
	bool operator==(const CreatureAbilityDescriptor & other) const;
	JsonNode toJson() const;
	std::string logicalIdentity() const;
	static std::shared_ptr<const CreatureAbilityDescriptor> fromJson(const JsonNode & json);
	template<typename Handler> void serialize(Handler & h)
	{
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_CONSECRATED_ARMS))
			throw std::runtime_error("Creature Ability provenance requires format1164");
		if(h.saving) validate();
		h & family;
		h & categories;
		h & interactions;
		h & resolution;
		h & sourceOwner;
		h & identity;
		if(!h.saving) validate();
	}
};

namespace creatureAbility
{
using Descriptor = std::shared_ptr<const CreatureAbilityDescriptor>;
DLL_LINKAGE Descriptor nativeAffliction(Native identity, Family family = Family::CREATURE_ABILITY,
	PlayerColor sourceOwner = PlayerColor::CANNOT_DETERMINE);
DLL_LINKAGE Descriptor spellEffect(SpellID spell, Family family, uint8_t categories, uint8_t interactions,
	PlayerColor sourceOwner = PlayerColor::CANNOT_DETERMINE);
DLL_LINKAGE Descriptor fromInnateBonus(const Bonus & bonus);
/// The audited on-hit producer is a creature ability wrapper even when its
/// backing SpellID also has an ordinary hero-cast form.
DLL_LINKAGE Descriptor onHitSpell(const CSpell & spell, PlayerColor sourceOwner);
DLL_LINKAGE bool sameDescriptor(const Descriptor & left, const Descriptor & right);
/// Extra resistance only. Existing ordinarily resistible effects add this to
/// their one native check; direct physical afflictions use one prevention check.
/// A direct delayed-damage status is checked only on application, independently
/// of its causing strike and later ticks. A mixed whole-spell cast must first
/// split its damage from its affliction; this query never cancels both together.
DLL_LINKAGE int unshakableResistanceBP(const CreatureAbilityDescriptor & effect, const battle::Unit & target);
}
