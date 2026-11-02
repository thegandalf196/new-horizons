/*
 * BattleBonusAcceptance.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "BattleBonusAcceptance.h"
#include "NewHorizonsSylvanTactics.h"
#include "BattleInfo.h"
#include "../CStack.h"
#include "../mapObjects/CGHeroInstance.h"
#include <optional>
#include <stdexcept>

namespace
{
struct LiveState
{
	const battle::CUnitState & value;
	bool hasBattleForm() const { return value.hasBattleForm(); }
	CreatureID battleFormCreature() const { return value.battleFormCreature(); }
	CreatureID battleFormOriginalCreature() const { return value.battleFormOriginalCreature(); }
	bool isGhost() const { return value.isGhost(); }
	bool isSummoned() const { return value.summoned; }
	bool isNatureSummoned() const { return value.natureSummoned; }
};
struct CopiedState
{
	const battle::BonusAcceptanceFacts::State & value;
	bool hasBattleForm() const { return value.hasForm; }
	CreatureID battleFormCreature() const { return value.currentForm; }
	CreatureID battleFormOriginalCreature() const { return value.originalForm; }
	bool isGhost() const { return value.ghost; }
	bool isSummoned() const { return value.summoned; }
	bool isNatureSummoned() const { return value.natureSummoned; }
};
struct LiveMetadata
{
	const CStack & original;
	std::optional<const CGHeroInstance *> supplied;
	mutable bool resolved = false;
	mutable const CGHeroInstance * cachedHero = nullptr;
	const CGHeroInstance * hero() const
	{
		if(!resolved) { cachedHero = supplied ? *supplied : original.getMyHero(); resolved = true; }
		return cachedHero;
	}
	SecondarySkill sylvanSkill() const { return SecondarySkill(SecondarySkill::decode("new-horizons:sylvanLuck")); }
	bool tacticsEnabled() const { return newHorizonsSylvanTactics::enabled(hero()); }
	bool heroPresent() const { return hero() != nullptr; }
	bool wildChance() const
	{
		return hero() && hero()->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.wildChance");
	}
	bool isTurret() const { return original.isTurret(); }
	SlotID unitSlot() const { return original.unitSlot(); }
};
struct CopiedMetadata
{
	const battle::BonusAcceptanceFacts::Static & value;
	SecondarySkill sylvanSkill() const { return value.sylvanSkill; }
	bool tacticsEnabled() const { return value.tacticsEnabled; }
	bool heroPresent() const { return value.heroPresent; }
	bool wildChance() const { return value.wildChance; }
	bool isTurret() const { return value.turret; }
	SlotID unitSlot() const { return value.slot; }
};
template<typename State, typename Metadata>
bool accepts(const State & state, const Bonus & bonus, const Metadata & metadata)
{
	if(state.hasBattleForm() && state.battleFormCreature() != state.battleFormOriginalCreature()
		&& bonus.sid == BonusSourceID(state.battleFormOriginalCreature())
		&& (bonus.source == BonusSource::CREATURE_ABILITY || bonus.source == BonusSource::STACK_EXPERIENCE))
		return false;

	// Temporary summons do not inherit Sylvan Luck rank effects unless they
	// carry Nature-spell provenance and their hero owns Wild Chance.  Filtering
	// by source skill preserves native creature Luck and unrelated bonuses.
	if(bonus.source != BonusSource::SECONDARY_SKILL)
		return true;

	// Identifier resolution depends on VLC and therefore must never run during
	// namespace-scope initialization (notably in the standalone test binary).
	const auto sylvanLuckSkill = metadata.sylvanSkill();
	if(bonus.sid.as<SecondarySkill>() != sylvanLuckSkill)
		return true;

	if(metadata.tacticsEnabled())
		return !state.isGhost() && !metadata.isTurret()
			&& metadata.unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
			&& metadata.unitSlot() != SlotID::WAR_MACHINES_SLOT;
	if(!state.isSummoned()) return true;
	return state.isNatureSummoned() && metadata.heroPresent() && metadata.wildChance();
}
}

namespace battle
{
// The same canonical accepts() below consumes this immutable private metadata.
// No native source, Hero, callback, library or callable is retained.
class OwnedBonusAcceptancePolicy final
{
	const BonusAcceptanceFacts::Static metadata;
	const BonusAcceptanceFacts::Subject subject;
	OwnedBonusAcceptancePolicy(BonusAcceptanceFacts::Static values, BonusAcceptanceFacts::Subject scope)
		: metadata(std::move(values)), subject(scope) {}
	friend BonusAcceptanceFacts captureOwnedBonusAcceptance(const CStack &);
	friend bool acceptsBattleBonus(const Bonus &, const BonusAcceptanceFacts::Static &, const BonusAcceptanceFacts::State &);
public:
	~OwnedBonusAcceptancePolicy() = default;
};
BonusAcceptanceFacts captureOwnedBonusAcceptance(const CStack & original)
{
	const auto * battle = original.getBattle();
	if(!battle) throw std::invalid_argument("Owned acceptance requires its captured native battle occurrence");
	LiveMetadata native{original, std::nullopt};
	BonusAcceptanceFacts::Static copied;
	copied.sylvanSkill = native.sylvanSkill();
	copied.tacticsEnabled = native.tacticsEnabled();
	// These dependencies are mutually exclusive in the canonical formula.
	// Typed current ghost/summon/form transitions remain evaluated after capture.
	if(!copied.tacticsEnabled)
	{
		copied.heroPresent = native.heroPresent();
		if(copied.heroPresent) copied.wildChance = native.wildChance();
	}
	copied.turret = native.isTurret(); copied.slot = native.unitSlot();
	const BonusAcceptanceFacts::Subject subject{battle->getBattleID(), original.unitId(), battle->getActivationSerial()};
	BonusAcceptanceFacts result;
	result.metadata.sylvanSkill = copied.sylvanSkill; result.metadata.slot = copied.slot;
	result.metadata.enginePolicy = std::shared_ptr<const OwnedBonusAcceptancePolicy>(
		new OwnedBonusAcceptancePolicy(std::move(copied), subject));
	result.state = {original.hasBattleForm(), original.battleFormCreature(), original.battleFormOriginalCreature(),
		original.isGhost(), original.summoned, original.natureSummoned, subject};
	return result;
}
}

bool battle::acceptsBattleBonus(const CStack & original, const CUnitState & state, const Bonus & bonus)
{
	return accepts(LiveState{state}, bonus, LiveMetadata{original, std::nullopt});
}

bool battle::acceptsBattleBonus(const CStack & original, const CUnitState & state, const Bonus & bonus,
	const CGHeroInstance * hero)
{
	return accepts(LiveState{state}, bonus, LiveMetadata{original, hero});
}

bool battle::acceptsBattleBonus(const Bonus & bonus,
	const BonusAcceptanceFacts::Static & metadata, const BonusAcceptanceFacts::State & state)
{
	if(const auto & policy = metadata.enginePolicy)
	{
		if(!state.capturedSubject || *state.capturedSubject != policy->subject)
			throw std::invalid_argument("Owned bonus acceptance source-domain mismatch");
		return accepts(CopiedState{state}, bonus, CopiedMetadata{policy->metadata});
	}
	return accepts(CopiedState{state}, bonus, CopiedMetadata{metadata});
}
