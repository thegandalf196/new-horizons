/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../battleAI/ProjectedTargetHitFixture.h"
#include "../../../lib/spells/NewHorizonsMetamagicOverflow.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/effects/native/Damage.h"
#include "../../../lib/spells/effects/native/UnitEffect.h"
#include "../../../lib/ScopeGuard.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsSorcerySpells.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/combatScripts/nativeEvents/NativeEventCommon.h"
#include "../../mock/mock_ServerCallback.h"
#include "../../mock/mock_vstd_RNG.h"
namespace
{
using namespace newHorizonsMetamagic;
class OverflowAuthority : public testing::NiceMock<ServerCallbackMock>
{
public:
	unsigned primaryWitnesses = 0;
	void recordMetamagicOverflowPrimary(const OverflowPrimaryDamage &) override { ++primaryWitnesses; }
};
class MetamagicOverflowPipeline : public HeroCommandFixture
{
protected:
	OverflowAuthority authority;
	testing::NiceMock<vstd::RNGMock> random;
	CStack * primary = nullptr;
	CStack * secondary = nullptr;
	void SetUp() override
	{
		useRevisedOrders = true;
		HeroCommandFixture::SetUp();
		ON_CALL(authority, getRNG()).WillByDefault(testing::Return(&random));
		ON_CALL(random, nextInt(testing::_, testing::_)).WillByDefault(testing::Return(99));
		ON_CALL(authority, apply(testing::Matcher<CPackForClient &>(testing::_))).WillByDefault([&](CPackForClient & p) { gameHandler->sendAndApply(p); });
		ON_CALL(authority, apply(testing::Matcher<StacksInjured &>(testing::_))).WillByDefault([&](StacksInjured & p) { gameHandler->sendAndApply(p); });
		ON_CALL(authority, apply(testing::Matcher<SetStackEffect &>(testing::_))).WillByDefault([&](SetStackEffect & p) { gameHandler->sendAndApply(p); });
		ON_CALL(authority, apply(testing::Matcher<BattleUnitsChanged &>(testing::_))).WillByDefault([&](BattleUnitsChanged & p) { gameHandler->sendAndApply(p); });
	}
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		auto perks = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		// Characterize the retained policy-five contract, independently of the
		// current policy-six catalogue. The strict decoder rejects mixed profiles.
		perks["metamagicPolicy"]["version"].Integer() = 5;
		perks["metamagicPolicy"]["arcaneOverflowPercentByRank"].Vector() = {JsonNode(0), JsonNode(25), JsonNode(50), JsonNode(75)};
		perks["metamagicPolicy"].Struct().erase("friendlyDamageReductionPercentByRank");
		perks["metamagicPolicy"].Struct().erase("debuffRetryChancePercentByRank");
		perks.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepare(int rank = 1)
	{
		startGame();
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:neela")));
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")), rank, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), 1, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		setTestSpellPointTotal(attackerSideHero, 100);
		startBattle();
		primary = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(10, 5), 8);
		secondary = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(11, 5), 100);
		ASSERT_NE(primary, nullptr); ASSERT_NE(secondary, nullptr);
		ASSERT_EQ(primary->getAvailableHealth(), 80);
		beginCombat();
	}
	spells::BattleCast cast(const CBattleInfoCallback * callback = nullptr, spells::Mode mode = spells::Mode::HERO)
	{
		spells::BattleCast result(callback ? callback : battle(), attackerSideHero, mode, SpellID(SpellID::MAGIC_ARROW).toSpell());
		result.setEffectValue(200); result.setSpellLevel(0);
		return result;
	}
	void bonus(CStack & target, BonusType type, int value, BonusSubtypeID subtype = BonusSubtypeID{})
	{
		target.addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, value, BonusSourceID(), subtype));
	}
};
class MetamagicOverflowRankPipeline : public MetamagicOverflowPipeline, public testing::WithParamInterface<int> {};
TEST_P(MetamagicOverflowRankPipeline, ActualNativeLiveAndPurePreviewUseSameExcessAndOneSecondaryInjury)
{
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	auto event = cast();
	auto mechanics = event.getSpell()->battleMechanics(&event);
	const spells::Target target{spells::Destination(primary)};
	const auto before = projectedHitTest::serialized(*gameHandler->randomizer);
	EXPECT_CALL(random, nextInt(testing::_, testing::_)).Times(0);
	const auto preview = previewOverflow(*mechanics, target);
	ASSERT_TRUE(preview);
	EXPECT_EQ(preview->primaryHealthBefore, 80);
	EXPECT_EQ(preview->postShieldPrimaryDamage, 200);
	EXPECT_EQ(preview->rawSpill, 30 * GetParam());
	EXPECT_EQ(preview->recipient, secondary->unitId());
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), before);
	testing::Mock::VerifyAndClearExpectations(&random);
	const auto health = secondary->getAvailableHealth();
	const auto firstInjury = server.injuries.size();
	mechanics->cast(&authority, target);
	EXPECT_FALSE(primary->alive());
	EXPECT_EQ(health - secondary->getAvailableHealth(), preview->damageIfLands);
	ASSERT_TRUE(mechanics->metamagicOverflowReceipt());
	EXPECT_EQ(mechanics->metamagicOverflowReceipt()->rawSpill, preview->rawSpill);
	unsigned secondaryInjuries = 0;
	for(size_t index = firstInjury; index < server.injuries.size(); ++index)
		for(const auto & injury : server.injuries[index].stacks)
			if(injury.flags & BattleStackAttacked::SECONDARY)
			{
				++secondaryInjuries;
				EXPECT_EQ(injury.stackAttacked, secondary->unitId());
				EXPECT_EQ(injury.damageAmount, preview->damageIfLands);
			}
	EXPECT_EQ(secondaryInjuries, 1u);
	EXPECT_EQ(authority.primaryWitnesses, 0u); // Spill bypasses original recorder/observer.
	EXPECT_EQ(server.casts.size(), 1u);
}
INSTANTIATE_TEST_SUITE_P(OrdinaryRanks, MetamagicOverflowRankPipeline, testing::Values(1, 2, 3));
TEST_F(MetamagicOverflowPipeline, ValidatedAuthoredArrowConsumesExactlyOneHeroActionAndManaPayment)
{
	ASSERT_NO_FATAL_FAILURE(prepare(3));
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:astral")));
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), 3, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 100);
	ASSERT_TRUE(attackerSideHero->getPerkState().getSelected().empty());
	ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
	const auto spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell);
	event.setOvercharge(0); // Ordinary authored producer; no setEffectValue override.
	const spells::Target aim{spells::Destination(primary)};
	const auto mechanics = event.mechanicsForTarget(aim);
	const auto preview = previewOverflow(*mechanics, aim);
	ASSERT_TRUE(preview);
	ASSERT_GT(preview->postShieldPrimaryDamage, primary->getAvailableHealth());
	EXPECT_EQ(preview->primaryResistancePercent, 0);
	EXPECT_EQ(preview->secondaryResistancePercent, 0);
	EXPECT_FALSE(preview->conditionalRebirth);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto cost = mechanics->getTargetAwareManaCost(aim);
	const auto health = secondary->getAvailableHealth();
	const auto casts = server.casts.size();
	const auto started = server.startedActions.size();
	const auto completed = battle()->getCastSpells(BattleSide::ATTACKER);
	const auto round = battle()->getRound();
	const auto before = battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(round);
	RecordProperty("fixture_raw_spell_power", attackerSideHero->getRawBasePrimarySkillValue(PrimarySkill::SPELL_POWER));
	RecordProperty("fixture_initial_mana_available", std::to_string(mana));
	RecordProperty("predicted_primary_postshield_damage", std::to_string(preview->postShieldPrimaryDamage));
	RecordProperty("predicted_overflow_raw_spill", std::to_string(preview->rawSpill));
	RecordProperty("predicted_overflow_defended_hp_loss", std::to_string(preview->damageIfLands));
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL; action.side = BattleSide::ATTACKER;
	action.spell = spell->getId(); action.spellOvercharge = 0; action.aimToUnit(primary);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), PlayerColor(0), action));
	EXPECT_FALSE(primary->alive());
	EXPECT_EQ(health - secondary->getAvailableHealth(), preview->damageIfLands);
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), cost);
	EXPECT_EQ(server.casts.size(), casts + 1);
	EXPECT_EQ(server.startedActions.size(), started + 1);
	EXPECT_EQ(battle()->getCastSpells(BattleSide::ATTACKER), completed + 1);
	const auto after = battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(round);
	RecordProperty("actual_secondary_hp_loss", std::to_string(health - secondary->getAvailableHealth()));
	RecordProperty("actual_mana_delta", std::to_string(mana - attackerSideHero->getManaAvailable()));
	RecordProperty("actual_hero_action_delta", std::to_string(before.heroActions - after.heroActions));
	RecordProperty("actual_started_action_delta", std::to_string(server.startedActions.size() - started));
	RecordProperty("actual_cast_count_delta", std::to_string(server.casts.size() - casts));
	EXPECT_EQ(before.heroActions, after.heroActions + 1);
	EXPECT_EQ(before.spellActions, after.spellActions);
	EXPECT_EQ(before.orderActions, after.orderActions);
}
TEST_F(MetamagicOverflowPipeline, TargetProtectionAndImmunityDoNotRerouteOrMultiplyCasterOutputAgain)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	bonus(*secondary, BonusType::SPELL_DAMAGE_REDUCTION, 50, BonusSubtypeID(SpellSchool::ANY));
	auto event = cast(); auto mechanics = event.getSpell()->battleMechanics(&event);
	const spells::Target target{spells::Destination(primary)};
	const auto protectedPreview = previewOverflow(*mechanics, target);
	ASSERT_TRUE(protectedPreview);
	EXPECT_EQ(protectedPreview->rawSpill, 30);
	EXPECT_EQ(protectedPreview->damageIfLands, 15);
	bonus(*secondary, BonusType::SPELL_IMMUNITY, 1, BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW)));
	const auto immune = previewOverflow(*mechanics, target);
	ASSERT_TRUE(immune);
	EXPECT_EQ(immune->recipient, secondary->unitId());
	EXPECT_EQ(immune->damageIfLands, 0);
	const auto health = secondary->getAvailableHealth();
	mechanics->cast(&authority, target);
	EXPECT_EQ(secondary->getAvailableHealth(), health);
}
TEST_F(MetamagicOverflowPipeline, CurrentNativeResistanceCapRemainsSeventyFiveAndCanLand)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	bonus(*secondary, BonusType::MAGIC_RESISTANCE, 100);
	auto event = cast(); auto mechanics = event.getSpell()->battleMechanics(&event);
	const auto preview = previewOverflow(*mechanics, {spells::Destination(primary)});
	ASSERT_TRUE(preview);
	EXPECT_EQ(preview->secondaryResistancePercent, 75); // Existing NH cap, never widened by Overflow.
	EXPECT_EQ(preview->damageIfLands, 30);
	const auto health = secondary->getAvailableHealth();
	mechanics->cast(&authority, {spells::Destination(primary)});
	EXPECT_EQ(health - secondary->getAvailableHealth(), 30); // Mock sample99 lands despite configured100.
}
TEST_F(MetamagicOverflowPipeline, ProjectionOutcomeIsChosenBeforeEffectsAndKeepsOneCompletion)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	projectedHitTest::Environment environment(gameState());
	auto live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	for(const auto outcome : {OverflowProjectionOutcome::PrimaryResisted, OverflowProjectionOutcome::SpillResisted, OverflowProjectionOutcome::BothLand})
	{
		auto state = std::make_shared<HypotheticBattle>(&environment, live);
		auto event = cast(state.get()); auto mechanics = event.getSpell()->battleMechanics(&event);
		mechanics->setMetamagicOverflowProjectionOutcome(outcome);
		const auto secondaryHealth = state->battleGetUnitByID(secondary->unitId())->getAvailableHealth();
		const auto originalMana = state->getProjectedManaAvailable(BattleSide::ATTACKER);
		mechanics->castEval(state->getServerCallback(), {spells::Destination(state->battleGetUnitByID(primary->unitId()))});
		EXPECT_EQ(state->getProjectedManaAvailable(BattleSide::ATTACKER), originalMana); // Existing evaluation resource contract.
		EXPECT_TRUE(state->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
		EXPECT_EQ(state->battleGetUnitByID(primary->unitId())->alive(), outcome == OverflowProjectionOutcome::PrimaryResisted);
		EXPECT_EQ(secondaryHealth - state->battleGetUnitByID(secondary->unitId())->getAvailableHealth(), outcome == OverflowProjectionOutcome::BothLand ? 30 : 0);
	}
	EXPECT_TRUE(primary->alive());
}
TEST_F(MetamagicOverflowPipeline, ConsumedTemporaryHpBelongsToPrimaryHealthExactlyOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	primary->health.addTemporaryHitPoints(20);
	auto event = cast(); auto mechanics = event.getSpell()->battleMechanics(&event);
	const auto preview = previewOverflow(*mechanics, {spells::Destination(primary)});
	ASSERT_TRUE(preview);
	EXPECT_EQ(preview->primaryHealthBefore, 100);
	EXPECT_EQ(preview->postShieldPrimaryDamage, 200);
	EXPECT_EQ(preview->rawSpill, 25); // (200 - body80 - temporary20) / 4.
	const auto health = secondary->getAvailableHealth();
	mechanics->cast(&authority, {spells::Destination(primary)});
	EXPECT_EQ(health - secondary->getAvailableHealth(), 25);
}
TEST_F(MetamagicOverflowPipeline, ForceLanceUsesBypassedPrimaryHpButSpillKeepsOrdinaryDefensesAndNoBind)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto lance = SpellID(SpellID::decode(newHorizonsSorcerySpells::FORCE_LANCE));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addSpellToSpellbook(lance);
	primary->health.addTemporaryHitPoints(300);
	bonus(*secondary, BonusType::SPELL_IMMUNITY, 1, BonusSubtypeID(lance));
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, lance.toSpell());
	event.setEffectValue(200);
	auto mechanics = lance.toSpell()->battleMechanics(&event);
	const auto preview = previewOverflow(*mechanics, {spells::Destination(primary)});
	ASSERT_TRUE(preview);
	EXPECT_EQ(preview->primaryHealthBefore, 80);
	EXPECT_EQ(preview->rawSpill, 30);
	EXPECT_EQ(preview->recipient, secondary->unitId());
	EXPECT_EQ(preview->damageIfLands, 0);
	const auto health = secondary->getAvailableHealth();
	mechanics->cast(&authority, {spells::Destination(primary)});
	EXPECT_FALSE(primary->alive());
	EXPECT_EQ(secondary->getAvailableHealth(), health);
	EXPECT_FALSE(secondary->hasBonusOfType(BonusType::BIND_EFFECT));
}
TEST_F(MetamagicOverflowPipeline, NativeCachedSecondaryResistanceRollBlocksWithoutRerouteOrAdditionalDraw)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	bonus(*secondary, BonusType::MAGIC_RESISTANCE, 50);
	const auto roster = battle()->battleGetAllUnits(false);
	const auto selected = std::ranges::find(roster, secondary);
	ASSERT_NE(selected, roster.end());
	const size_t selectedIndex = std::distance(roster.begin(), selected);
	size_t draws = 0;
	ON_CALL(random, nextInt(0, 99)).WillByDefault([&](int, int) { return draws++ == selectedIndex ? 0 : 99; });
	auto event = cast(); auto mechanics = event.getSpell()->battleMechanics(&event);
	const auto preview = previewOverflow(*mechanics, {spells::Destination(primary)});
	ASSERT_TRUE(preview);
	EXPECT_EQ(preview->secondaryResistancePercent, 50);
	EXPECT_EQ(preview->damageIfLands, 30);
	EXPECT_EQ(draws, 0u);
	// This local projection override MUST NOT alter the authoritative cast.
	mechanics->setMetamagicOverflowProjectionOutcome(OverflowProjectionOutcome::BothLand);
	const auto health = secondary->getAvailableHealth();
	mechanics->cast(&authority, {spells::Destination(primary)});
	EXPECT_FALSE(primary->alive());
	EXPECT_EQ(secondary->getAvailableHealth(), health);
	EXPECT_EQ(draws, roster.size());
}
TEST_F(MetamagicOverflowPipeline, NoOtherLivingEnemyGivesCertifiedNoSpill)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	static const JsonNode parameters;
	static const CombatEventPayload payload;
	const nativeEvents::Context context{authority, *battle(), nullptr, nullptr, parameters, payload};
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit->unitSide() == BattleSide::DEFENDER && unit->unitId() != primary->unitId())
			nativeEvents::damage(context, *unit, unit->getAvailableHealth(), nullptr, true);
	auto event = cast(); auto mechanics = event.getSpell()->battleMechanics(&event);
	EXPECT_EQ(overflowSupport(*mechanics, {spells::Destination(primary)}), OverflowSupport::Supported);
	EXPECT_FALSE(previewOverflow(*mechanics, {spells::Destination(primary)}));
}
TEST_F(MetamagicOverflowPipeline, PrimaryRebirthAliveAtCompletionPreventsSpill)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	bonus(*primary, BonusType::REBIRTH, 100);
	bonus(*primary, BonusType::CASTS, 1);
	auto event = cast(); auto mechanics = event.getSpell()->battleMechanics(&event);
	const auto preview = previewOverflow(*mechanics, {spells::Destination(primary)});
	EXPECT_FALSE(preview); // Primary native Rebirth is not a certified three-outcome preview.
	EXPECT_EQ(overflowSupport(*mechanics, {spells::Destination(primary)}), OverflowSupport::Unsupported);
	const auto health = secondary->getAvailableHealth();
	mechanics->cast(&authority, {spells::Destination(primary)});
	EXPECT_TRUE(primary->alive());
	EXPECT_EQ(secondary->getAvailableHealth(), health);
	EXPECT_FALSE(mechanics->metamagicOverflowReceipt());
}
TEST_F(MetamagicOverflowPipeline, CreatureAutomaticAndFriendlyOriginsCannotBecomeOriginalOverflow)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	spells::BattleCast creature(battle(), primary, spells::Mode::CREATURE_ACTIVE, SpellID(SpellID::MAGIC_ARROW).toSpell());
	auto native = creature.getSpell()->battleMechanics(&creature);
	EXPECT_EQ(overflowSupport(*native, {spells::Destination(secondary)}), OverflowSupport::Inapplicable);
	auto automatic = cast();
	automatic.setAutomaticHeroCast({spells::AutomaticHeroCastKind::DIVINE_FURY});
	native = automatic.getSpell()->battleMechanics(&automatic);
	EXPECT_EQ(overflowSupport(*native, {spells::Destination(primary)}), OverflowSupport::Inapplicable);
	auto original = cast(); native = original.getSpell()->battleMechanics(&original);
	const auto roster = battle()->battleGetAllUnits(false);
	const auto found = std::ranges::find_if(roster, [](const battle::Unit * unit)
	{ return unit->alive() && unit->unitSide() == BattleSide::ATTACKER; });
	ASSERT_NE(found, roster.end());
	const auto * ally = *found;
	ASSERT_EQ(battle()->battleGetUnitByID(ally->unitId()), ally);
	ASSERT_EQ(ally->unitSide(), BattleSide::ATTACKER);
	EXPECT_EQ(overflowSupport(*native, {spells::Destination(ally)}), OverflowSupport::Inapplicable);
}
TEST_F(MetamagicOverflowPipeline, ActualPostKillResistanceAuraContextGovernsSpillNotPrekillPreview)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	bonus(*primary, BonusType::SPELL_RESISTANCE_AURA, 20);
	ASSERT_EQ(primary->magicResistance(), 0);
	ASSERT_EQ(secondary->magicResistance(), 20);
	auto event = cast(); auto mechanics = event.getSpell()->battleMechanics(&event);
	const spells::Target aim{spells::Destination(primary)};
	EXPECT_EQ(overflowSupport(*mechanics, aim), OverflowSupport::Unsupported);
	EXPECT_FALSE(previewOverflow(*mechanics, aim));
	EXPECT_EQ(overflowSupport(*mechanics, aim, true), OverflowSupport::Supported);
	ON_CALL(random, nextInt(0, 99)).WillByDefault(testing::Return(0));
	const auto health = secondary->getAvailableHealth();
	mechanics->cast(&authority, aim);
	ASSERT_TRUE(mechanics->metamagicOverflowReceipt());
	EXPECT_EQ(mechanics->metamagicOverflowReceipt()->secondaryResistancePercent, 0);
	EXPECT_EQ(health - secondary->getAvailableHealth(), 30);
}
TEST_F(MetamagicOverflowPipeline, SharedDefendingRerollContextIsNotAnIndependentThreeOutcomeProof)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	bonus(*secondary, BonusType::MAGIC_RESISTANCE, 20);
	battle()->setAdverseCombatRerollState(BattleSide::DEFENDER, {true, false});
	auto event = cast(); auto mechanics = event.getSpell()->battleMechanics(&event);
	EXPECT_EQ(overflowSupport(*mechanics, {spells::Destination(primary)}, true), OverflowSupport::Unsupported);
	EXPECT_FALSE(previewOverflow(*mechanics, {spells::Destination(primary)}));
}
TEST_F(MetamagicOverflowPipeline, RawMalformedChainIsRefusedBeforeAnyTargetOrConditionCallbacks)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto event = cast(); auto mechanics = event.getSpell()->battleMechanics(&event);
	auto & level = const_cast<CSpell::LevelInfo &>(event.getSpell()->getLevelInfo(mechanics->getEffectLevel()));
	const auto original = level;
	const auto restore = vstd::makeScopeGuard([&]() { level = original; });
	std::vector<JsonNode> malformed{JsonNode("invalid"), JsonNode(std::numeric_limits<double>::quiet_NaN()),
		JsonNode(std::numeric_limits<double>::infinity())};
	EXPECT_CALL(random, nextInt(testing::_, testing::_)).Times(0);
	for(const auto & bad : malformed)
	{
		level.battleEffects["malformed"]["chainLength"] = bad;
		EXPECT_EQ(overflowSupport(*mechanics, {spells::Destination(primary)}), OverflowSupport::Unsupported);
		EXPECT_FALSE(previewOverflow(*mechanics, {spells::Destination(primary)}));
	}
}
class OpaqueOverflowEffect final : public spells::effects::native::UnitEffect
{
public:
	mutable unsigned targetCalls = 0;
	void adjustTargetTypes(std::vector<spells::AimType> &, const spells::Mechanics *) const override { ++targetCalls; }
};
class OpaqueOverflowCondition final : public spells::IReceptiveCheck
{
public:
	mutable unsigned nativeCalls = 0;
	bool isReceptive(const spells::Mechanics *, const battle::Unit *) const override { ++nativeCalls; return true; }
};
TEST_F(MetamagicOverflowPipeline, UnknownEffectAndConditionAreNotExecutedByPureSupportOrMistakenForBlockedZero)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto event = cast();
	auto effects = std::make_shared<spells::effects::Effects>();
	auto opaque = std::make_shared<OpaqueOverflowEffect>();
	for(auto & level : effects->data) level.emplace("opaque", opaque);
	auto condition = std::make_shared<OpaqueOverflowCondition>();
	spells::BattleSpellMechanics unknown(&event, effects, condition);
	EXPECT_EQ(overflowSupport(unknown, {spells::Destination(primary)}), OverflowSupport::Unsupported);
	EXPECT_FALSE(previewOverflow(unknown, {spells::Destination(primary)}));
	EXPECT_EQ(opaque->targetCalls, 0u);
	EXPECT_EQ(condition->nativeCalls, 0u);
	for(auto & level : effects->data) { level.clear(); level.emplace("damage", std::make_shared<spells::effects::native::Damage>()); }
	spells::BattleSpellMechanics native(&event, effects, condition);
	EXPECT_EQ(overflowSupport(native, {spells::Destination(primary)}), OverflowSupport::Unsupported);
	EXPECT_FALSE(previewOverflow(native, {spells::Destination(primary)}));
	EXPECT_EQ(condition->nativeCalls, 0u);
	const auto health = secondary->getAvailableHealth();
	native.cast(&authority, {spells::Destination(primary)}); // Existing native condition remains authoritative.
	EXPECT_GT(condition->nativeCalls, 0u);
	EXPECT_EQ(health - secondary->getAvailableHealth(), 30);
}
TEST_F(MetamagicOverflowPipeline, OriginalPrimaryMirrorCannotBePredictedAsAnOverflowHit)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	bonus(*primary, BonusType::MAGIC_MIRROR, 100);
	auto event = cast(); auto mechanics = event.getSpell()->battleMechanics(&event);
	const spells::Target aim{spells::Destination(primary)};
	EXPECT_EQ(overflowSupport(*mechanics, aim, true), OverflowSupport::Unsupported);
	EXPECT_FALSE(previewOverflow(*mechanics, aim));
	const auto health = secondary->getAvailableHealth();
	mechanics->cast(&authority, aim);
	EXPECT_TRUE(primary->alive());
	EXPECT_EQ(secondary->getAvailableHealth(), health);
	EXPECT_FALSE(mechanics->metamagicOverflowReceipt());
}
TEST_F(MetamagicOverflowPipeline, SecondaryMirrorDoesNotBecomeAnExtraReflectableSpell)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	bonus(*secondary, BonusType::MAGIC_MIRROR, 100);
	auto event = cast(); auto mechanics = event.getSpell()->battleMechanics(&event);
	const auto preview = previewOverflow(*mechanics, {spells::Destination(primary)});
	ASSERT_TRUE(preview);
	EXPECT_EQ(preview->damageIfLands, 30);
	const auto health = secondary->getAvailableHealth();
	mechanics->cast(&authority, {spells::Destination(primary)});
	EXPECT_EQ(health - secondary->getAvailableHealth(), 30);
	EXPECT_EQ(server.casts.size(), 1u);
}
TEST_F(MetamagicOverflowPipeline, NoRankNoOverkillAndNonOriginalSpellShapesAreInapplicable)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0));
	auto event = cast(); auto mechanics = event.getSpell()->battleMechanics(&event);
	EXPECT_EQ(overflowSupport(*mechanics, {spells::Destination(primary)}), OverflowSupport::Inapplicable);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")), 1, ChangeValueMode::ABSOLUTE);
	event.setEffectValue(80); mechanics = event.getSpell()->battleMechanics(&event);
	EXPECT_FALSE(previewOverflow(*mechanics, {spells::Destination(primary)}));
	for(const auto mode : {spells::Mode::PASSIVE, spells::Mode::MAGIC_MIRROR})
	{
		auto other = cast(nullptr, mode); auto excluded = other.getSpell()->battleMechanics(&other);
		EXPECT_EQ(overflowSupport(*excluded, {spells::Destination(primary)}), OverflowSupport::Inapplicable);
	}
	for(const auto spell : {SpellID(SpellID::FIREBALL), SpellID(SpellID::CHAIN_LIGHTNING)})
	{
		spells::BattleCast area(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		auto excluded = spell.toSpell()->battleMechanics(&area);
		EXPECT_EQ(overflowSupport(*excluded, {spells::Destination(primary)}), OverflowSupport::Inapplicable);
	}
}
}
