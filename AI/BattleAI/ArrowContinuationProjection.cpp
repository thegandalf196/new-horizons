/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ArrowContinuationReuse.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/battle/PassiveDivineMandateVanguard.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/effects/Effect.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"
#include <vcmi/ServerCallback.h>

namespace arrowContinuation
{
bool canonicalDamageEffect(const spells::effects::Effect & effect, const spells::Mechanics & mechanics) noexcept
{
	try
	{
		return !effect.indirect && !effect.optional
			&& effect.forecastImplementation(&mechanics) == spells::effects::ForecastImplementation::CANONICAL_DAMAGE;
	}
	catch(...) { return false; }
}
namespace
{
template<class T> std::vector<std::byte> bytes(const T & packet)
{
	CMemorySerializer memory;
	memory.oser & packet;
	return memory.extractBuffer();
}
bool canonical(const spells::Mechanics & mechanics)
{
	try
	{
		bool supported = true;
		size_t count = 0;
		mechanics.forEachEffect([&](const spells::effects::Effect & effect)
		{
			++count;
			supported &= canonicalDamageEffect(effect, mechanics);
			return false; // forEachEffect stops on true.
		});
		const auto * spell = dynamic_cast<const CSpell *>(mechanics.getSpell());
		return supported && count == 1 && spell && spell->getId() == SpellID::MAGIC_ARROW
			&& spell->counteredSpells.empty();
	}
	catch(...) { return false; } // Optional effect admission; castEval is outside this catch.
}

/// Finite Arrow observer: any additional state operation withholds the receipt.
/// Every operation still forwards unchanged; no outcome is replayed.
class Observation final : public ServerCallback
{
	ServerCallback & delegate;
	const HypotheticBattle & state;
	const uint32_t target;
	const BattleSide side;
	const int level;
	bool valid = true;
	unsigned injuries = 0, marks = 0, completed = 0, wisdomCasts = 0;
	std::vector<std::byte> injuryBytes, markBytes, wisdomBytes;
public:
	Observation(ServerCallback & delegate, const HypotheticBattle & state, uint32_t target, BattleSide side, int level)
		: delegate(delegate), state(state), target(target), side(side), level(level) {}
	std::optional<Outcome> finish(BattleHex position) const
	{
		if(!valid || injuries != 1 || completed != 1 || marks > 1 || wisdomCasts != 1) return std::nullopt;
		return Outcome{target, position, injuryBytes, markBytes, wisdomBytes};
	}
	void complain(const std::string & problem) override { valid = false; delegate.complain(problem); }
	bool describeChanges() const override { return delegate.describeChanges(); }
	vstd::RNG * getRNG() override { return delegate.getRNG(); }
	bool rollCombatAbility(const IBattleInfoCallback & b, const battle::Unit & a, int p) override
	{ valid = false; return delegate.rollCombatAbility(b, a, p); }
	bool rollHostileCombatAbility(const IBattleInfoCallback & b, const battle::Unit & a,
		const battle::Unit & t, int p) override
	{ valid = false; return delegate.rollHostileCombatAbility(b, a, t, p); }
	bool resolveAdverseCombatRoll(const BattleID & b, BattleSide s, bool stochastic, bool adverse,
		const std::function<bool()> & draw) override
	{ valid = false; return delegate.resolveAdverseCombatRoll(b, s, stochastic, adverse, draw); }
	void apply(CPackForClient & p) override { valid = false; delegate.apply(p); }
	void apply(BattleLogMessage & p) override { delegate.apply(p); }
	void apply(BattleStackMoved & p) override { valid = false; delegate.apply(p); }
	void apply(BattleObstaclesChanged & p) override { valid = false; delegate.apply(p); }
	void apply(CatapultAttack & p) override { valid = false; delegate.apply(p); }
	void apply(SetStackEffect & p) override { valid = false; delegate.apply(p); }
	void apply(StacksInjured & p) override
	{
		++injuries;
		valid &= marks == 0 && completed == 0 && p.stacks.size() == 1
			&& p.stacks.front().stackAttacked == target;
		injuryBytes = bytes(p);
		delegate.apply(p);
	}
	void apply(BattleUnitsChanged & p) override
	{
		++marks;
		valid &= injuries == 1 && completed == 0 && p.changedStacks.size() == 1
			&& p.changedStacks.front().id == target
			&& p.changedStacks.front().operation == UnitChanges::EOperation::UPDATE;
		markBytes = bytes(p);
		delegate.apply(p);
	}
	void recordArcaneInspirationEffects(const SetStackEffect & packet) override
	{ valid = false; delegate.recordArcaneInspirationEffects(packet); }
	void recordCompletedWisdomSpellCast(BattleSide s, const SpellID & spell, int32_t round, bool automatic) override
	{
		++wisdomCasts;
		valid &= wisdomCasts == 1 && injuries == 0 && completed == 0
			&& s == side && spell == SpellID::MAGIC_ARROW && round == state.getRound() && !automatic;
		delegate.recordCompletedWisdomSpellCast(s, spell, round, automatic);
		wisdomBytes = bytes(state.getWisdomCombatState(side));
	}
	void recordCompletedHeroSpellCast(BattleSide s, int32_t l) override
	{ ++completed; valid &= s == side && l == level; delegate.recordCompletedHeroSpellCast(s, l); }
	void recordCompletedHeroSpellCast(BattleSide s) override
	{ valid = false; delegate.recordCompletedHeroSpellCast(s); }
	void recordCrossSchoolFormulaCast(BattleSide s, const newHorizonsCrossSchoolFormula::Receipt & r) override
	{ valid = false; delegate.recordCrossSchoolFormulaCast(s, r); }
	void recordExtendSpellCast(BattleSide s) override { valid = false; delegate.recordExtendSpellCast(s); }
	void recordResolvedSpellRecipient(uint32_t id) override
	{ valid &= id == target; delegate.recordResolvedSpellRecipient(id); }
	void recordCompletedAutomaticHeroSpellCast(BattleSide s, const SpellID & spell,
		const spells::AutomaticHeroCastReceipt & receipt) override
	{ valid = false; delegate.recordCompletedAutomaticHeroSpellCast(s, spell, receipt); }
};
}

bool isArrow(const PossibleSpellcast & candidate)
{
	return candidate.command == HeroCommand::NONE && candidate.spell
		&& candidate.spell->getId() == SpellID::MAGIC_ARROW;
}
bool resourceIndependent(const HypotheticBattle & state)
{
	// Earlier saved Magic policies have not been certified for this receipt.
	if(!newHorizonsMagic::rulesActive(state.getMagicRules())
		|| state.getMagicRules()["rulesetVersion"].Integer() != newHorizonsMagic::CURRENT_RULESET_VERSION)
		return false;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		// FULL never issues new Orders: with none present, Divine Fury cannot run.
		if(state.getCounterspellArmed(side) || state.getMetamagicCountersequenceArmed(side)
			|| !state.getHeroOrderStates(side).empty() || state.getActiveOrder(side) != HeroCommand::NONE)
			return false;
	for(const auto * unit : state.battleGetAllUnits(false))
		if(unit->creatureId() == CreatureID::IMP || unit->creatureId() == CreatureID::FAMILIAR
			|| unit->hasBonusOfType(BonusType::MANA_DRAIN)
			|| unit->hasBonus(CSelector([](const Bonus * bonus)
			{ return bonus && newHorizonsPassiveDivineMandate::isVanguardRiderEntitlement(*bonus); })))
			return false;
	return true;
}
std::optional<Outcome> project(spells::Mechanics & mechanics, HypotheticBattle & state,
	const spells::Target & target)
{
	const bool supported = target.size() == 1 && target.front().unitValue
		&& target.front().hexValue == target.front().unitValue->getPosition()
		&& sourcesSupported(state, target.front().unitValue->unitId())
		&& mechanics.getHeroCaster() && !mechanics.isMagicMirror()
		&& newHorizonsMagic::spellAllowedBySavedRoster(state.getMagicRules(), SpellID::MAGIC_ARROW)
		&& canonical(mechanics) && resourceIndependent(state);
	if(!supported)
	{
		mechanics.castEval(state.getServerCallback(), target);
		return std::nullopt;
	}
	Observation observation(*state.getServerCallback(), state, target.front().unitValue->unitId(),
		mechanics.getCasterSide(), state.battleGetSpellLevel(SpellID::MAGIC_ARROW));
	mechanics.castEval(&observation, target);
	if(!canonical(mechanics)) return std::nullopt; // Loaded effects can change during bootstrap.
	return observation.finish(target.front().hexValue);
}
}
