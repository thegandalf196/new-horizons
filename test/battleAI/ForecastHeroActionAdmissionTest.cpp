/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapterFixture.h"
#include "../../AI/BattleAI/ForecastHeroActionAdmission.h"
#include "../../AI/BattleAI/PossibleSpellcast.h"
#include "../../AI/BattleAI/HeroSpellRefinement.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/ScopeGuard.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/spells/SpellBonusProvenance.h"
#include "../../lib/spells/NewHorizonsSorcerySpells.h"
#include "../../lib/spells/effects/native/Damage.h"
#include "../../lib/spells/effects/native/Timed.h"
#include "../../lib/spells/effects/native/UnitEffect.h"
#include <limits>
#include <vcmi/ServerCallback.h>

namespace
{
using namespace forecast::heroAction;
constexpr auto SIDE = BattleSide::ATTACKER;
// Observe native query dispatch by COMPOSING the real admitted battle. The
// production final class and its exact ownership admission remain unchanged.
class TargetCallbackProbe final : public CPlayerBattleCallback
{
	std::shared_ptr<HypotheticBattle> source;
public:
	explicit TargetCallbackProbe(std::shared_ptr<HypotheticBattle> source)
		: CPlayerBattleCallback(source.get(), source->getPlayerID().value()), source(std::move(source)) {}
	mutable unsigned identities = 0, rosters = 0, positions = 0;
	void reset() const { identities = rosters = positions = 0; }
	const battle::Unit * battleGetUnitByID(uint32_t id) const override
	{ ++identities; return source->battleGetUnitByID(id); }
	battle::Units battleGetUnitsIf(const battle::UnitFilter & predicate) const override
	{ ++rosters; return source->battleGetUnitsIf(predicate); }
	const battle::Unit * battleGetUnitByPos(const BattleHex & hex, bool alive) const override
	{ ++positions; return source->battleGetUnitByPos(hex, alive); }
};
class ForecastHeroActionAdmissionTest : public ForecastNativeAdapterTest
{
protected:
	void prepareHero(bool selected = false)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(false, false, selected, "core:pikeman", false, true));
		ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	}
	PossibleSpellcast spell(SpellID id, bool friendly = false)
	{
		PossibleSpellcast result;
		result.spell = id.toSpell();
		for(const auto * unit : parent->battleGetAllUnits(false))
			if((unit->unitSide() == SIDE) == friendly) { result.dest = {spells::Destination(unit)}; break; }
		return result;
	}
	void expectPure(const PossibleSpellcast & candidate, Route route)
	{
		const auto authority = projectedHitTest::serialized(*handler->randomizer);
		const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
		const auto owned = parent->forecastSide(SIDE);
		const auto * compact = parent->forecastState();
		const auto round = parent->battleGetRound();
		const auto casts = parent->getCastSpells(SIDE);
		std::map<uint32_t, JsonNode> before;
		for(const auto * unit : parent->battleGetAllUnits(false))
			before.emplace(unit->unitId(), parent->getForUpdate(unit->unitId())->save());
		const auto decision = classify(*parent, SIDE, candidate);
		EXPECT_EQ(decision.route, route) << reasonText(decision.reason);
		EXPECT_EQ(parent->forecastState(), compact);
		EXPECT_EQ(parent->forecastSide(SIDE), owned);
		EXPECT_EQ(parent->battleGetRound(), round);
		EXPECT_EQ(parent->getCastSpells(SIDE), casts);
		for(const auto & [id, state] : before) EXPECT_EQ(parent->getForUpdate(id)->save(), state);
		EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), authority);
		EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
	}
};
TEST_F(ForecastHeroActionAdmissionTest, DamageNumericFocusAndHeroBindHaveFinitePureClosures)
{
	ASSERT_NO_FATAL_FAILURE(prepareHero());
	expectPure(spell(SpellID::MAGIC_ARROW), Route::Compact);
	expectPure(spell(SpellID::HASTE, true), Route::Compact);
	// The authored Hero root has a finite round clock. core:bind is the
	// creature ability whose addInfo is patched with its unit caster ID.
	expectPure(spell(SpellID(SpellID::decode("new-horizons:entangle"))), Route::Compact);
	expectPure(spell(SpellID(SpellID::decode("new-horizons:focusMagic")), true), Route::Compact);
}
TEST_F(ForecastHeroActionAdmissionTest, ExistingSixOrdersHaveNoSpellPrefixAndSpecialContinuationsRouteReference)
{
	ASSERT_NO_FATAL_FAILURE(prepareHero());
	for(const auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE,
		HeroCommand::FOCUS_FIRE, HeroCommand::RIPOSTE, HeroCommand::BRACE, HeroCommand::FEINT})
	{
		PossibleSpellcast candidate;
		candidate.command = command;
		expectPure(candidate, Route::Compact);
	}
	for(const auto command : {HeroCommand::VANGUARD, HeroCommand::SECOND_WIND})
	{
		PossibleSpellcast candidate;
		candidate.command = command;
		EXPECT_EQ(classify(*parent, SIDE, candidate).reason, Reason::UnsupportedOrder);
		expectPure(candidate, Route::CurrentNativeReference);
	}
}
TEST_F(ForecastHeroActionAdmissionTest, NoFuelMetamagicAndSorceryDoNotForceBlanketReference)
{
	ASSERT_NO_FATAL_FAILURE(prepareHero(true));
	{ SCOPED_TRACE("no-fuel Magic Arrow"); expectPure(spell(SpellID::MAGIC_ARROW), Route::Compact); }
	{ SCOPED_TRACE("non-damaging Haste"); expectPure(spell(SpellID::HASTE, true), Route::Compact); }
}
TEST_F(ForecastHeroActionAdmissionTest, ActualEligibleMetamagicFuelRoutesBeforeCandidatePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepareHero());
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto metamagic = SecondarySkill(SecondarySkill::decode("new-horizons:metamagic"));
	hero->setSecSkillLevel(metamagic, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	// Meta6's offensive removal is optional, not granted by the skill core.
	ASSERT_TRUE(hero->getPerkState().canAcquirePerk("new-horizons:metamagic",
		"new-horizons:metamagic.unravelingMagic", MasteryLevel::BASIC));
	hero->applyPerkSelection({"new-horizons:metamagic", "new-horizons:metamagic.unravelingMagic"});
	ASSERT_TRUE(hero->hasActivePerk("new-horizons:metamagic", "new-horizons:metamagic.unravelingMagic"));
	parent = std::make_shared<HypotheticBattle>(environment.get(), visible);
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto candidate = spell(SpellID::MAGIC_ARROW);
	ASSERT_FALSE(candidate.dest.empty());
	const auto id = candidate.dest.front().unitValue->unitId();
	Bonus fuel(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT,
		2, BonusSourceID(SpellID(SpellID::HASTE)));
	fuel.turnsRemain = 2;
	spells::stampSpellBonusProvenance(fuel, SpellID::HASTE, SpellID::HASTE,
		PlayerColor(1), PlayerColor(1));
	SetStackEffect packet;
	packet.battleID = parent->getBattleID();
	packet.toAdd.emplace_back(id, std::vector<Bonus>{fuel});
	parent->getServerCallback()->apply(packet);
	EXPECT_EQ(classify(*parent, SIDE, candidate).reason, Reason::StochasticCompletion);
	expectPure(candidate, Route::CurrentNativeReference);
}
TEST_F(ForecastHeroActionAdmissionTest, UnknownRelocationHealthAndStochasticDamageDoNotStartPrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepareHero());
	for(const auto id : {SpellID(SpellID::TELEPORT), SpellID(SpellID::RESURRECTION),
		SpellID(SpellID::IMPLOSION), SpellID(SpellID::decode("new-horizons:handOfFate"))})
		expectPure(spell(id), Route::CurrentNativeReference);
	const auto automatic = [&] { auto candidate = spell(SpellID::HASTE, true); candidate.divineMandateFollowup = true; return candidate; }();
	EXPECT_EQ(classify(*parent, SIDE, automatic).reason, Reason::AutomaticCompletion);
	expectPure(automatic, Route::CurrentNativeReference);
}
TEST_F(ForecastHeroActionAdmissionTest, NestedUnsupportedCandidateRetainsChangedCurrentSource)
{
	ASSERT_NO_FATAL_FAILURE(prepareHero());
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	const auto id = parent->battleGetAllUnits(false).back()->unitId();
	const auto original = parent->battleGetUnitByID(id)->getAvailableHealth();
	int64_t damage = 7;
	child->getForUpdate(id)->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	const auto changed = child->battleGetUnitByID(id)->getAvailableHealth();
	ASSERT_LT(changed, original);
	const auto candidate = spell(SpellID::TELEPORT);
	EXPECT_EQ(classify(*child, SIDE, candidate).route, Route::CurrentNativeReference);
	EXPECT_EQ(child->battleGetUnitByID(id)->getAvailableHealth(), changed);
	EXPECT_EQ(parent->battleGetUnitByID(id)->getAvailableHealth(), original);
}
TEST_F(ForecastHeroActionAdmissionTest, FullHeroClassifierRejectsEveryUnsafeChainBeforeTargetTransformation)
{
	ASSERT_NO_FATAL_FAILURE(prepareHero());
	auto state = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *state);
	auto probe = std::make_shared<TargetCallbackProbe>(state);
	const auto sideBefore = state->forecastSide(SIDE);
	for(const auto id : {SpellID(SpellID::MAGIC_ARROW), SpellID(SpellID::HASTE),
		SpellID(SpellID::decode("new-horizons:focusMagic")), SpellID(SpellID::CHAIN_LIGHTNING)})
	{
		auto candidate = spell(id, id != SpellID::MAGIC_ARROW);
		spells::BattleCast cast(probe.get(), probe->battleGetFightingHero(SIDE), spells::Mode::HERO, candidate.spell);
		auto mechanics = candidate.spell->battleMechanics(&cast);
		spells::effects::native::UnitEffect * actual = nullptr;
		mechanics->forEachEffect([&](const spells::effects::Effect & effect)
		{
			actual = const_cast<spells::effects::native::UnitEffect *>(dynamic_cast<const spells::effects::native::UnitEffect *>(&effect));
			return actual != nullptr;
		});
		ASSERT_NE(actual, nullptr);
		// Factory-created mechanics share this actual loaded native effect. Save
		// and restore its complete base payload; no global config is rewritten.
		const spells::effects::native::UnitEffect original = *actual;
		const auto restore = vstd::makeScopeGuard([&]() { *actual = original; });
		std::vector<JsonNode> badChains(5);
		badChains[0].String() = "invalid chain";
		badChains[1].Float() = std::numeric_limits<double>::infinity();
		badChains[2].Float() = std::numeric_limits<double>::quiet_NaN();
		badChains[3].Integer() = 2;
		badChains[4].Integer() = std::numeric_limits<int64_t>::max();
		if(id == SpellID::CHAIN_LIGHTNING)
		{
			// The current native policy preserves explicitly finite 0/1 chains;
			// only configured multi-target chains receive the fixed target count.
			JsonNode finite;
			finite["chainLength"].Integer() = 0;
			actual->init(finite);
			EXPECT_TRUE(actual->supportsFiniteForecastTargeting(*mechanics));
		}
		for(const auto & bad : badChains)
		{
			JsonNode descriptor;
			descriptor["chainLength"] = bad;
			actual->init(descriptor);
			EXPECT_EQ(heroDecisionPressure::spellScoutDomain(*mechanics, candidate.dest),
				heroDecisionPressure::SpellScoutDomain::Unknown);
			probe->identities = probe->rosters = probe->positions = 0;
			const auto decision = classify(*state, SIDE, candidate);
			EXPECT_EQ(decision.route, Route::CurrentNativeReference);
			EXPECT_EQ(decision.reason, Reason::UnsafeTargeting);
			EXPECT_EQ(state->forecastSide(SIDE), sideBefore);
			// This is the SAME effect recipe invoked by full classify, after its
			// ownership/aim binding. Any transform here reaches the real observer.
			forecast::EventMask required = 0;
			probe->reset();
			EXPECT_EQ(inspectSupportedEffects(*mechanics, candidate.dest, required), Reason::UnsafeTargeting);
			EXPECT_EQ(probe->identities, 0u);
			EXPECT_EQ(probe->rosters, 0u);
			EXPECT_EQ(probe->positions, 0u);
		}
	}
}
TEST_F(ForecastHeroActionAdmissionTest, CreatureEntryUsesTheSamePretargetDescriptorGateWithoutHeroResources)
{
	ASSERT_NO_FATAL_FAILURE(prepareHero());
	auto state = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *state);
	auto probe = std::make_shared<TargetCallbackProbe>(state);
	const auto candidate = spell(SpellID::MAGIC_ARROW);
	const auto roster = state->battleGetAllUnits(false);
	const auto actorEntry = std::ranges::find_if(roster, [](const battle::Unit * unit)
	{ return unit->unitSide() == SIDE && unit->unitSlot() == SlotID(0); });
	ASSERT_NE(actorEntry, roster.end());
	const auto * actor = *actorEntry;
	ASSERT_EQ(actor->creatureId(), CreatureID(CreatureID::decode("core:pikeman")));
	ASSERT_FALSE(candidate.dest.empty());
	ASSERT_EQ(candidate.dest.front().unitValue->unitSide(), BattleSide::DEFENDER);
	spells::BattleCast cast(state.get(), actor, spells::Mode::CREATURE_ACTIVE, candidate.spell);
	spells::BattleCast observed(probe.get(), actor, spells::Mode::CREATURE_ACTIVE, candidate.spell);
	auto effects = std::make_shared<spells::effects::Effects>();
	auto damage = std::make_shared<spells::effects::native::Damage>();
	for(auto & level : effects->data) level.emplace("damage", damage);
	spells::BattleSpellMechanics mechanics(&cast, effects, nullptr);
	spells::BattleSpellMechanics observedMechanics(&observed, effects, nullptr);
	JsonNode descriptor;
	descriptor["chainLength"].String() = "malformed";
	damage->init(descriptor);
	const auto owned = state->forecastSide(SIDE);
	const auto decision = classifyCreature(*state, mechanics, candidate.dest);
	EXPECT_EQ(decision.reason, Reason::UnsafeTargeting);
	forecast::EventMask required = 0;
	probe->reset();
	EXPECT_EQ(inspectSupportedEffects(observedMechanics, candidate.dest, required), Reason::UnsafeTargeting);
	EXPECT_EQ(probe->identities, 0u); // Pure recipe starts after caster admission.
	EXPECT_EQ(probe->rosters + probe->positions, 0u);
	EXPECT_EQ(state->forecastSide(SIDE), owned);
	EXPECT_EQ(decision.required & forecast::eventBit(forecast::Event::HeroResource), 0u);
	auto actual = candidate.spell->battleMechanics(&cast);
	const auto actualDecision = classifyCreature(*state, *actual, candidate.dest);
	EXPECT_EQ(actualDecision.route, Route::Compact) << reasonText(actualDecision.reason)
		<< " actor=" << actor->unitId() << " casterOwner=" << actual->getCasterColor().getNum();
	EXPECT_EQ(state->forecastSide(SIDE), owned);
}

TEST_F(ForecastHeroActionAdmissionTest, CreatureEntryCannotBypassManualOrAutomaticHeroCompletion)
{
	ASSERT_NO_FATAL_FAILURE(prepareHero(true));
	auto state = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *state);
	auto probe = std::make_shared<TargetCallbackProbe>(state);
	const auto candidate = spell(SpellID::MAGIC_ARROW);
	const auto owned = state->forecastSide(SIDE);
	for(const bool automatic : {false, true})
	{
		spells::BattleCast cast(state.get(), state->battleGetFightingHero(SIDE), spells::Mode::HERO, candidate.spell);
		spells::BattleCast observed(probe.get(), state->battleGetFightingHero(SIDE), spells::Mode::HERO, candidate.spell);
		if(automatic)
		{
			cast.setAutomaticHeroCast({spells::AutomaticHeroCastKind::DIVINE_FURY});
			observed.setAutomaticHeroCast({spells::AutomaticHeroCastKind::DIVINE_FURY});
		}
		auto mechanics = candidate.spell->battleMechanics(&cast);
		EXPECT_EQ(classifyCreature(*state, *mechanics, candidate.dest).reason, Reason::WrongCastProvenance);
		// Separate actual wrapper-source rejection proves no callback queries
		// precede ownership admission; it does not grant the wrapper HB rights.
		auto foreign = candidate.spell->battleMechanics(&observed);
		probe->reset();
		EXPECT_EQ(classifyCreature(*state, *foreign, candidate.dest).reason, Reason::CustomMechanics);
		EXPECT_EQ(probe->identities + probe->rosters + probe->positions, 0u);
		EXPECT_EQ(state->forecastSide(SIDE), owned);
	}
}

TEST_F(ForecastHeroActionAdmissionTest, CreatureEntryRejectsAncestorCasterWithSameIdAfterCurrentSourceMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepareHero());
	auto state = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *state);
	const auto roster = parent->battleGetAllUnits(false);
	const auto ancestorEntry = std::ranges::find_if(roster, [](const battle::Unit * unit)
	{ return unit->unitSide() == SIDE && unit->unitSlot() == SlotID(0); });
	ASSERT_NE(ancestorEntry, roster.end());
	const auto * ancestor = *ancestorEntry;
	int64_t damage = 7;
	state->getForUpdate(ancestor->unitId())->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	ASSERT_NE(state->battleGetUnitByID(ancestor->unitId()), ancestor);
	const auto health = state->battleGetUnitByID(ancestor->unitId())->getAvailableHealth();
	const auto candidate = spell(SpellID::MAGIC_ARROW);
	spells::BattleCast cast(state.get(), ancestor, spells::Mode::CREATURE_ACTIVE, candidate.spell);
	auto mechanics = candidate.spell->battleMechanics(&cast);
	EXPECT_EQ(classifyCreature(*state, *mechanics, candidate.dest).reason, Reason::WrongCastProvenance);
	auto probe = std::make_shared<TargetCallbackProbe>(state);
	spells::BattleCast observed(probe.get(), ancestor, spells::Mode::CREATURE_ACTIVE, candidate.spell);
	auto foreign = candidate.spell->battleMechanics(&observed);
	probe->reset();
	EXPECT_EQ(classifyCreature(*state, *foreign, candidate.dest).reason, Reason::CustomMechanics);
	EXPECT_EQ(probe->identities, 0u); // Wrapper rejected BEFORE any target dispatch.
	EXPECT_EQ(probe->rosters + probe->positions, 0u);
	EXPECT_EQ(state->battleGetUnitByID(ancestor->unitId())->getAvailableHealth(), health);
	EXPECT_GT(ancestor->getAvailableHealth(), health);
}

TEST_F(ForecastHeroActionAdmissionTest, EmptyRecipientSetCannotAdmitMalformedOrUnprovenTimedPayload)
{
	ASSERT_NO_FATAL_FAILURE(prepareHero());
	auto state = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *state);
	auto probe = std::make_shared<TargetCallbackProbe>(state);
	const auto * actor = state->battleGetAllUnits(false).front();
	const auto spell = SpellID(SpellID::HASTE).toSpell();
	spells::BattleCast cast(state.get(), actor, spells::Mode::CREATURE_ACTIVE, spell);
	spells::BattleCast observed(probe.get(), actor, spells::Mode::CREATURE_ACTIVE, spell);
	auto effects = std::make_shared<spells::effects::Effects>();
	auto timed = std::make_shared<spells::effects::native::Timed>();
	for(auto & level : effects->data) level.emplace("timed", timed);
	spells::BattleSpellMechanics mechanics(&cast, effects, nullptr);
	spells::BattleSpellMechanics observedMechanics(&observed, effects, nullptr);
	JsonNode descriptor;
	descriptor["bonus"]["bad"]["val"].Float() = std::numeric_limits<double>::infinity();
	timed->init(descriptor);
	EXPECT_EQ(classifyCreature(*state, mechanics, {}).reason, Reason::UnprovenTimedPayload);
	forecast::EventMask required = 0;
	probe->reset();
	EXPECT_EQ(inspectSupportedEffects(observedMechanics, {}, required), Reason::UnprovenTimedPayload);
	EXPECT_EQ(probe->identities, 0u);
	EXPECT_EQ(probe->rosters + probe->positions, 0u);
}

TEST_F(ForecastHeroActionAdmissionTest, UnadmittedRealSourceNeverClaimsCompactPermission)
{
	ASSERT_NO_FATAL_FAILURE(prepareHero());
	EXPECT_EQ(classify(*visible, SIDE, spell(SpellID::MAGIC_ARROW)).reason, Reason::SourceNotAdmitted);
}
class SpoofDamage final : public spells::effects::native::Damage {};
class SpoofFocus final : public spells::effects::native::UnitEffect {};
TEST(ForecastHeroActionImplementationTest, ExactNativeImplementationCannotBeSpoofedByNameOrInheritedEnum)
{
	spells::effects::native::Damage damage;
	spells::effects::native::Timed timed;
	SpoofDamage custom;
	SpoofFocus fake;
	fake.name = "new-horizons:focusMagic";
	EXPECT_TRUE(supportedImplementation(damage));
	EXPECT_TRUE(supportedImplementation(timed));
	EXPECT_FALSE(supportedImplementation(custom));
	EXPECT_FALSE(supportedImplementation(fake));
	const auto focus = nativeSpells::createFocusEnchantmentEffect("spells/focusMagicEnchantment");
	ASSERT_NE(focus, nullptr);
	EXPECT_TRUE(supportedImplementation(*focus));
}
TEST(ForecastHeroActionImplementationTest, DamageDescriptorExcludesStochasticCountedDestructiveAndUnboundedChainPaths)
{
	spells::effects::native::Damage damage;
	EXPECT_TRUE(damage.supportsFiniteForecast());
	for(const auto * key : {"handOfFate", "killByPercentage", "killByCount", "destroyRemains"})
	{
		JsonNode parameters;
		parameters[key].Bool() = true;
		damage.init(parameters);
		EXPECT_FALSE(damage.supportsFiniteForecast()) << key;
	}
	for(const auto count : {2.0, -1.0, std::numeric_limits<double>::infinity()})
	{
		JsonNode parameters;
		parameters["chainLength"].Float() = count;
		damage.init(parameters);
		EXPECT_FALSE(damage.supportsFiniteForecast());
	}
}
}
