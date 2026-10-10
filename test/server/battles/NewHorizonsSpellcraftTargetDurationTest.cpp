/* NewHorizonsSpellcraftTargetDurationTest.cpp, part of VCMI; GPL v2.0 or later. */
#include "StdInc.h"
#include "NewHorizonsElementalTerrainFixture.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/TargetCondition.h"
#include "../../mock/mock_spells_Mechanics.h"
#include "../../mock/mock_battle_Unit.h"
#include "../../../lib/spells/NewHorizonsSpellcraft.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#ifdef ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/callback/CBattleCallback.h"
#endif

namespace
{
class HealthCountMechanics : public spells::MechanicsMock
{
public:
	bool counting = false;
	int64_t ceiling = 100;
	bool isCountingSpellTargets() const override { return counting; }
	int64_t getTargetAwareEffectValue(const battle::Unit *) const override { return ceiling; }
};

constexpr auto SPELLCRAFT = "new-horizons:spellcraft";
SpellID spell(const char * key) { return SpellID(SpellID::decode(key)); }

#ifdef ENABLE_BATTLE_AI
class SpellcraftEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit SpellcraftEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif

class NewHorizonsSpellcraftTargetDurationTest : public NewHorizonsElementalTerrainFixture
{
protected:
	CStack * ally = nullptr;
	CStack * enemy = nullptr;
	void prepare(bool concentration = true, bool extend = true)
	{
		startGame();
		for(const auto * key : {"new-horizons:lightMagic", "new-horizons:sorceryMagic", "new-horizons:havocMagic",
			"new-horizons:natureMagic", "new-horizons:shadowMagic", "new-horizons:chaosMagic"})
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(key)), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(SPELLCRAFT)), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({SPELLCRAFT, concentration
			? std::string(newHorizonsSpellcraft::CONCENTRATION) : "new-horizons:spellcraft.spellPenetration"});
		if(extend)
			attackerSideHero->applyPerkSelection({SPELLCRAFT, std::string(newHorizonsSpellcraft::EXTEND_SPELL)});
		ASSERT_EQ(attackerSideHero->hasActivePerk(SPELLCRAFT, std::string(newHorizonsSpellcraft::CONCENTRATION)), concentration);
		ASSERT_EQ(attackerSideHero->hasActivePerk(SPELLCRAFT, std::string(newHorizonsSpellcraft::EXTEND_SPELL)), extend);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 200, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		for(const auto * key : {"core:magicArrow", "core:haste", "core:fireball", "new-horizons:timeStop", "new-horizons:phantomArmy"})
			attackerSideHero->addSpellToSpellbook(spell(key));
		setTestSpellPointTotal(attackerSideHero, 1000);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		ally = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(leftHex), 1000);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10000);
		ASSERT_NE(ally, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), ally);
	}
	std::unique_ptr<spells::Mechanics> mechanics(SpellID id, const spells::Target & target,
		const CBattleInfoCallback * callback = nullptr, spells::Mode mode = spells::Mode::HERO) const
	{
		spells::BattleCast cast(callback ? callback : battle(), attackerSideHero, mode, id.toSpell());
		return cast.mechanicsForTarget(target);
	}
	bool paid(SpellID id, const CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = id;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(BattleSide::ATTACKER), action);
	}
};
}

TEST_F(NewHorizonsSpellcraftTargetDurationTest, ConcentrationScalesOnlyThePowerComponentAndPaidDamageMatches)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, false));
	const auto id = spell("core:magicArrow");
	spells::BattleCast unclassified(battle(), attackerSideHero, spells::Mode::HERO, id.toSpell());
	const auto ordinary = id.toSpell()->battleMechanics(&unclassified);
	const auto focused = mechanics(id, {spells::Destination(enemy)});
	ASSERT_EQ(focused->getTargetedStackCount({spells::Destination(enemy)}), 1u);
	EXPECT_EQ(focused->getCastSpellPowerComponentBonusPercent(), 15);
	EXPECT_EQ(focused->getEffectValue(), 20 + (ordinary->getEffectValue() - 20) * 115 / 100);
	const auto expected = focused->adjustEffectValueBeforeExecution(enemy);
	const auto hp = enemy->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto cost = battle()->battleGetSpellCost(id.toSpell(), attackerSideHero);
	ASSERT_TRUE(paid(id, enemy));
	EXPECT_EQ(hp - enemy->getAvailableHealth(), expected);
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), cost);
	EXPECT_EQ(battle()->getExtendSpellLastRound(BattleSide::ATTACKER), -1);
}

TEST_F(NewHorizonsSpellcraftTargetDurationTest, AreaOneStackQualifiesButTwoStacksAndCreatureModeDoNot)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, false));
	const auto id = spell("core:fireball");
	const spells::Target target{spells::Destination(enemy->getPosition())};
	// The common melee fixture starts the ally inside Fireball's radius.
	// Move its entire footprint away before asserting the one-recipient case.
	auto relocated = ally->acquireState();
	relocated->position = BattleHex(leftHex - 4);
	BattleUnitsChanged update;
	update.battleID = BattleID(0);
	update.changedStacks.emplace_back(ally->unitId(), UnitChanges::EOperation::UPDATE);
	update.changedStacks.back().data = relocated->save();
	gameHandler->sendAndApply(update);
	const auto area = mechanics(id, target)->rangeInHexes(enemy->getPosition());
	for(const auto hex : ally->getHexes())
		ASSERT_FALSE(area.contains(hex));
	const auto one = mechanics(id, target);
	ASSERT_EQ(one->getTargetedStackCount(target), 1u);
	EXPECT_EQ(one->getCastSpellPowerComponentBonusPercent(), 15);
	const auto neighbor = enemy->getPosition().cloneInDirection(BattleHex::TOP_LEFT);
	ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), neighbor, 100), nullptr);
	const auto two = mechanics(id, target);
	EXPECT_GE(two->getTargetedStackCount(target), 2u);
	EXPECT_EQ(two->getCastSpellPowerComponentBonusPercent(), 0);
	EXPECT_EQ(mechanics(spell("core:magicArrow"), {spells::Destination(enemy)}, nullptr,
		spells::Mode::PASSIVE)->getCastSpellPowerComponentBonusPercent(), 0);
}

TEST_F(NewHorizonsSpellcraftTargetDurationTest, FirstTemporaryPaidCastExtendsThenNextRoundRestoresAvailability)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	const auto id = spell("core:haste");
	const spells::Target target{spells::Destination(ally)};
	const auto duration = mechanics(id, target)->getEffectDuration();
	ASSERT_EQ(mechanics(id, target)->getExtendSpellBonusRounds(), 1);
	ASSERT_TRUE(paid(id, ally));
	EXPECT_EQ(battle()->getExtendSpellLastRound(BattleSide::ATTACKER), battle()->getRound());
	const auto bonuses = ally->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(id)));
	ASSERT_FALSE(bonuses->empty());
	for(const auto & bonus : *bonuses)
		if(bonus->duration & BonusDuration::N_TURNS)
			EXPECT_EQ(bonus->turnsRemain, duration);
	EXPECT_EQ(mechanics(id, target)->getExtendSpellBonusRounds(), 0);
	EXPECT_EQ(mechanics(id, target)->getEffectDuration(), duration - 1);
	BattleNextRound next;
	next.battleID = BattleID(0);
	gameHandler->sendAndApply(next);
	EXPECT_EQ(mechanics(id, target)->getExtendSpellBonusRounds(), 1);
	EXPECT_EQ(mechanics(id, target)->getEffectDuration(), duration);
}

TEST_F(NewHorizonsSpellcraftTargetDurationTest, RejectedCastAndInstantSpellDoNotSpendTheTemporaryAllowance)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_FALSE(paid(spell("core:haste"), enemy));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->getExtendSpellLastRound(BattleSide::ATTACKER), -1);
	ASSERT_TRUE(paid(spell("core:magicArrow"), enemy));
	EXPECT_EQ(battle()->getExtendSpellLastRound(BattleSide::ATTACKER), -1);
	EXPECT_EQ(mechanics(spell("core:haste"), {spells::Destination(ally)})->getExtendSpellBonusRounds(), 1);
}

TEST_F(NewHorizonsSpellcraftTargetDurationTest, ActionBoundAndPermanentEffectsAreExcludedButPhantomAndFormsQualify)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * haste = spell("core:haste").toSpell();
	ASSERT_FALSE(haste->hasBattleEffects());
	ASSERT_FALSE(haste->getLevelInfo(attackerSideHero->getEffectLevel(haste)).effects.Struct().empty());
	EXPECT_TRUE(newHorizonsSpellcraft::roundTemporary(*haste, attackerSideHero->getEffectLevel(haste)));
	EXPECT_FALSE(newHorizonsSpellcraft::roundTemporary(*spell("core:frenzy").toSpell(), 3));
	EXPECT_FALSE(newHorizonsSpellcraft::roundTemporary(*spell("core:disruptingRay").toSpell(), 3));
	EXPECT_FALSE(newHorizonsSpellcraft::roundTemporary(*spell("new-horizons:timeStop").toSpell(), 3));
	EXPECT_FALSE(newHorizonsSpellcraft::roundTemporary(*spell("new-horizons:elementalConvergence").toSpell(), 3));
	EXPECT_TRUE(newHorizonsSpellcraft::roundTemporary(*spell("new-horizons:phantomArmy").toSpell(), 3));
	EXPECT_TRUE(newHorizonsSpellcraft::roundTemporary(*spell("new-horizons:polymorph").toSpell(), 3));
	EXPECT_FALSE(newHorizonsSpellcraft::roundTemporary(*spell("new-horizons:puppetMaster").toSpell(), 3));
	EXPECT_EQ(mechanics(spell("new-horizons:phantomArmy"), {spells::Destination(ally)})->adjustEffectDuration(2), 3);
	EXPECT_EQ(newHorizonsSorcery::PHANTOM_ARMY_MAX_DURATION_ROUNDS, 4);
}

TEST_F(NewHorizonsSpellcraftTargetDurationTest, AcceptedCounterspellNegationConsumesOnlyAnEligibleTemporaryCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BattleSpellCast accepted;
	accepted.battleID = BattleID(0);
	accepted.side = BattleSide::ATTACKER;
	accepted.spellID = spell("core:haste");
	accepted.counterspellSide = BattleSide::DEFENDER;
	accepted.counterspellNegated = true;
	accepted.extendedSpell = true;
	gameHandler->sendAndApply(accepted);
	EXPECT_EQ(battle()->getExtendSpellLastRound(BattleSide::ATTACKER), battle()->getRound());
	EXPECT_THROW(gameHandler->sendAndApply(accepted), std::runtime_error);
}

TEST_F(NewHorizonsSpellcraftTargetDurationTest, CurrentRoundReceiptAndOldPrefixGuardsCoverBattleWorldAndLobby)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BattleSpellCast packet;
	packet.battleID = BattleID(0);
	packet.side = BattleSide::ATTACKER;
	packet.spellID = spell("core:haste");
	packet.extendedSpell = true;
	CMemorySerializer current;
	current.oser & packet;
	BattleSpellCast restored;
	current.iser & restored;
	EXPECT_TRUE(restored.extendedSpell);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	old.iser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	EXPECT_THROW(old.oser & packet, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	ASSERT_TRUE(paid(spell("core:haste"), ally));
	EXPECT_THROW(gameState()->validateExtendSpellSerialization(false), std::runtime_error);
	EXPECT_NO_THROW(gameState()->validateExtendSpellSerialization(true));
	CMemorySerializer world;
	world.oser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	world.iser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	EXPECT_THROW(world.oser & *gameState(), std::runtime_error);
	EXPECT_TRUE(world.extractBuffer().empty());
	LobbyStartGame lobby;
	lobby.initializedGameState = gameState();
	CMemorySerializer outer;
	outer.oser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	outer.iser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	EXPECT_THROW(outer.oser & lobby, std::runtime_error);
	EXPECT_TRUE(outer.extractBuffer().empty());
}

TEST(SpellcraftHealthConditionTest, IntendedCountingIgnoresOnlyTheCeilingAndFinalLegalityUsesBoostedCeiling)
{
	testing::NiceMock<HealthCountMechanics> mechanics;
	testing::NiceMock<UnitMock> target;
	EXPECT_CALL(target, getAvailableHealth()).WillRepeatedly(testing::Return(110));
	EXPECT_CALL(mechanics, applySpellBonus(testing::_, &target)).WillRepeatedly(testing::ReturnArg<0>());
	const auto condition = spells::TargetConditionItemFactory::getDefault()->createConfigurable("", "healthValueSpecial", "");
	ASSERT_NE(condition, nullptr);
	EXPECT_FALSE(condition->isReceptive(&mechanics, &target));
	mechanics.counting = true;
	EXPECT_TRUE(condition->isReceptive(&mechanics, &target));
	mechanics.counting = false;
	EXPECT_FALSE(condition->isReceptive(&mechanics, &target));
	mechanics.ceiling = 115;
	EXPECT_TRUE(condition->isReceptive(&mechanics, &target));
}

TEST_F(NewHorizonsSpellcraftTargetDurationTest, ExtendedPhantomJsonRejectsOldReadersAndBinaryDescriptorLoss)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto phantom = ally->acquireState();
	phantom->summoned = true;
	phantom->initializePhantomProfile(200, 4);
	UnitChanges update(ally->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = phantom->save();
	BattleUnitsChanged apply;
	apply.battleID = BattleID(0);
	apply.changedStacks.push_back(update);
	gameHandler->sendAndApply(apply);
	CMemorySerializer current;
	current.oser & update;
	UnitChanges restored;
	current.iser & restored;
	EXPECT_EQ(restored.data["state"]["phantomRoundsRemaining"].Integer(), 4);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	old.iser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	EXPECT_THROW(old.oser & update, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	CMemorySerializer descriptor;
	EXPECT_THROW(descriptor.oser & *ally, std::runtime_error);
	EXPECT_TRUE(descriptor.extractBuffer().empty());
	EXPECT_THROW(gameState()->validateExtendSpellSerialization(true), std::runtime_error);
}

#ifdef ENABLE_BATTLE_AI
TEST_F(NewHorizonsSpellcraftTargetDurationTest, DetachedProjectionCopiesAndConsumesItsOwnAllowanceWithoutTouchingLiveState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto callback = std::make_shared<CBattleCallback>(battle()->sideToPlayer(BattleSide::ATTACKER), nullptr);
	callback->onBattleStarted(battle());
	SpellcraftEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback->getBattle(BattleID(0)));
	const auto mana = attackerSideHero->getManaAvailable();
	const auto liveHp = enemy->getAvailableHealth();
	spells::BattleCast cast(&projected, attackerSideHero, spells::Mode::HERO, spell("core:haste").toSpell());
	const spells::Target target{spells::Destination(projected.battleGetUnitByID(ally->unitId()))};
	const auto projectedMechanics = cast.mechanicsForTarget(target);
	ASSERT_TRUE(projectedMechanics->canBeCastAt(target));
	ASSERT_EQ(projectedMechanics->getExtendSpellBonusRounds(), 1);
	cast.castEval(projected.getServerCallback(), target);
	EXPECT_EQ(projected.getExtendSpellLastRound(BattleSide::ATTACKER), projected.getRound());
	EXPECT_EQ(battle()->getExtendSpellLastRound(BattleSide::ATTACKER), -1);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(enemy->getAvailableHealth(), liveHp);
	// A second independent forecast starts from authoritative, unspent state.
	HypotheticBattle independent(&environment, callback->getBattle(BattleID(0)));
	EXPECT_EQ(independent.getExtendSpellLastRound(BattleSide::ATTACKER), -1);
}
#endif
