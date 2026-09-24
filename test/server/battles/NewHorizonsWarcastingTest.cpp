/*
 * NewHorizonsWarcastingTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "../../../lib/spells/CSpell.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/HeroActionAllowanceState.h"
#include "../../../lib/battle/NewHorizonsWarcasting.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"

namespace
{
constexpr auto warcastingSkill = "new-horizons:warcasting";
constexpr auto metamagicSkill = "new-horizons:metamagic";
constexpr auto metamagicGrandPerk = "new-horizons:metamagic.grandMetamagic";
constexpr auto counterspellKey = "new-horizons:counterspell";
constexpr auto sorcerySkill = "new-horizons:sorceryMagic";
constexpr auto countermagePerk = "new-horizons:sorceryMagic.countermage";
constexpr auto martialChannelingPerk = "new-horizons:warcasting.martialChanneling";
constexpr auto arcaneChannelingPerk = "new-horizons:warcasting.arcaneChanneling";
constexpr auto tacticalWeavingPerk = "new-horizons:warcasting.tacticalWeaving";
constexpr auto battleMeditationPerk = "new-horizons:warcasting.battleMeditation";

std::shared_ptr<Bonus> testTimeStopMarker(BattleSide side)
{
	auto marker = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID());
	marker->parameters = std::make_shared<BonusParameters>(static_cast<int32_t>(side));
	return marker;
}

SpellID counterspellId()
{
	return SpellID(SpellID::decode(counterspellKey));
}

class WarcastingEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit WarcastingEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsWarcastingTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		auto magicRules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		magicRules["warcasting"] = JsonNode(true);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);

		auto perkRules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		if(!plannedWarcastingPerkBeforeInit.empty())
		{
			auto & perks = perkRules["skills"][warcastingSkill]["perks"].Vector();
			const auto planned = std::find_if(perks.begin(), perks.end(), [&](const auto & perk)
			{
				return perk["id"].String() == plannedWarcastingPerkBeforeInit;
			});
			if(planned == perks.end())
				throw std::runtime_error("Unknown Warcasting perk requested for saved planned-rule fixture");
			(*planned)["effect"]["status"].String() = "planned";
		}
		if(activateBattleMeditationBeforeInit && plannedWarcastingPerkBeforeInit != battleMeditationPerk)
		{
			auto & perks = perkRules["skills"][warcastingSkill]["perks"].Vector();
			const auto meditation = std::find_if(perks.begin(), perks.end(), [](const auto & perk)
			{
				return perk["id"].String() == battleMeditationPerk;
			});
			if(meditation == perks.end())
				throw std::runtime_error("Missing Battle Meditation perk in saved rules fixture");
			(*meditation)["effect"]["status"].String() = "active";
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perkRules);
	}

	void markPerkPlannedBeforeInitialization(const std::string & perkId)
	{
		plannedWarcastingPerkBeforeInit = perkId;
	}

	void activateBattleMeditationBeforeInitialization()
	{
		activateBattleMeditationBeforeInit = true;
	}

	void prepareWarcasting(int rank = 1, bool withMetamagic = false, bool defenderCountermage = false)
	{
		startGame();
		const int decodedWarcasting = SecondarySkill::decode(warcastingSkill);
		ASSERT_GE(decodedWarcasting, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(decodedWarcasting), rank, ChangeValueMode::ABSOLUTE);
		if(withMetamagic)
		{
			const int decodedMetamagic = SecondarySkill::decode(metamagicSkill);
			ASSERT_GE(decodedMetamagic, 0);
			attackerSideHero->setSecSkillLevel(SecondarySkill(decodedMetamagic), 1, ChangeValueMode::ABSOLUTE);
		}
		if(defenderCountermage)
		{
			const int decodedSorcery = SecondarySkill::decode(sorcerySkill);
			ASSERT_GE(decodedSorcery, 0);
			// Countermage is an Advanced Sorcery perk, so its fixture hero must
			// meet the same rank gate as an ordinary selection.
			defenderSideHero->setSecSkillLevel(SecondarySkill(decodedSorcery), MasteryLevel::ADVANCED,
				ChangeValueMode::ABSOLUTE);
			defenderSideHero->applyPerkSelection({std::string(sorcerySkill), std::string(countermagePerk)});
			ASSERT_TRUE(defenderSideHero->hasActivePerk(std::string(sorcerySkill), std::string(countermagePerk)));
		}
		attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 100, ChangeValueMode::ABSOLUTE);
		// Recovery restores Normal, not Buffer: give these casting fixtures real
		// capacity instead of implicitly overcharging a zero-Knowledge hero.
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		setTestSpellPointTotal(defenderSideHero, 1000);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto spell : {SpellID::HASTE, SpellID::SLOW, SpellID::MAGIC_ARROW})
			attackerSideHero->addSpellToSpellbook(spell);

		startBattle();
		attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 10);
		defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10);
		beginCombat();

		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit != attacker && unit != defender)
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		activate(attacker);
	}

	void activate(const CStack * stack)
	{
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = stack->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
	}

	uint32_t grantOrderAllowanceForFixture(BattleSide side)
	{
		auto & allowances = battle()->getSide(side).heroActionAllowances;
		return allowances.grantAllowance(HeroActionAllowanceState::AllowanceKind::ORDER,
			HeroActionAllowanceState::GrantSource::PERK, battle()->battleGetRound());
	}

	void selectWarcastingPerk(const std::string & perkId)
	{
		attackerSideHero->applyPerkSelection({std::string(warcastingSkill), perkId});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(std::string(warcastingSkill), perkId));
	}

	const JsonNode & savedPerkDefinition(const std::string & perkId) const
	{
		const auto & perks = attackerSideHero->getPerkState().rules["skills"][warcastingSkill]["perks"].Vector();
		const auto definition = std::find_if(perks.begin(), perks.end(), [&](const auto & perk)
		{
			return perk["id"].String() == perkId;
		});
		if(definition == perks.end())
			throw std::runtime_error("Missing Warcasting perk in saved rules snapshot");
		return *definition;
	}

	bool cast(SpellID spell, const CStack * target, bool followup = false, bool grand = false)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.metamagicFollowup = followup;
		action.metamagicGrand = grand;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool declineMetamagic()
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeMetamagicDecline(BattleSide::ATTACKER));
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}

	CStack * attacker = nullptr;
	CStack * defender = nullptr;
	std::string plannedWarcastingPerkBeforeInit;
	bool activateBattleMeditationBeforeInit = false;
};
}

TEST_F(NewHorizonsWarcastingTest, CounterspelledSpellReadiesOrderAndOrderSnapshotsTheBonus)
{
	prepareWarcasting();
	ASSERT_EQ(newHorizonsWarcasting::rank(attackerSideHero), 1);
	const int32_t spellRound = battle()->battleGetRound();

	// The authoritative cast counts even though the defender's armed ward negates
	// the effect. It must still ready the next Order action.
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto spellToOrder = battle()->getWarcastingState(BattleSide::ATTACKER);
	EXPECT_EQ(spellToOrder.nextEligibleAction, AlternatingHeroActionState::Action::ORDER);
	EXPECT_EQ(spellToOrder.empowermentPercent, 10);
	EXPECT_EQ(spellToOrder.expiryRound, spellRound + 1);
	const auto casts = server.castsOf(SpellID::HASTE);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.counterspellNegated);

	// Invalid commands never cross the accepted StartAction boundary.
	EXPECT_FALSE(issue(static_cast<HeroCommand>(127)));
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER), spellToOrder);

	advanceRound();
	ASSERT_EQ(battle()->battleGetRound(), spellRound + 1);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto orderState = battle()->getHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(orderState);
	EXPECT_EQ(orderState->warcastingBonusPercent, 10);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const auto & line)
	{
		return line.find("Warcasting adds +10 percentage points to attribute-derived efficiency.") != std::string::npos;
	}));
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER).nextEligibleAction,
		AlternatingHeroActionState::Action::SPELL);

	const auto & formula = battle()->getHeroCommandRules()["commands"]["charge"]["effects"]["meleeDamagePercent"];
	EXPECT_EQ(heroCommands::coefficient(formula, *attackerSideHero), 30);
	EXPECT_EQ(heroCommands::coefficient(formula, *attackerSideHero, orderState->warcastingBonusPercent), 32);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const auto & line)
	{
		return line.find("gains +32% damage, plus 2 percentage points per additional hex, this round.")
			!= std::string::npos;
	}));
	JsonNode flatOnly = formula;
	flatOnly["attack"] = JsonNode(0.0);
	flatOnly["defense"] = JsonNode(0.0);
	EXPECT_EQ(heroCommands::coefficient(flatOnly, *attackerSideHero), 10);
	EXPECT_EQ(heroCommands::coefficient(flatOnly, *attackerSideHero, orderState->warcastingBonusPercent), 10);

	// The Order retains the Spell-to-Order snapshot after this Order has already
	// rearmed Spell readiness; later attack evaluation must use the former.
	EXPECT_EQ(newHorizonsWarcasting::orderBonus(battle()->getWarcastingState(BattleSide::ATTACKER),
		battle()->battleGetRound()), 0);
	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	EXPECT_EQ(restored->getWarcastingState(BattleSide::ATTACKER),
		battle()->getWarcastingState(BattleSide::ATTACKER));
	ASSERT_TRUE(restored->getHeroOrderState(BattleSide::ATTACKER));
	EXPECT_EQ(restored->getHeroOrderState(BattleSide::ATTACKER)->warcastingBonusPercent, 10);

	CMemorySerializer oldSave;
	oldSave.oser.version = ESerializationVersion::NEW_HORIZONS_CURE_AFFLICTION;
	EXPECT_THROW(oldSave.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(oldSave.extractBuffer().empty());
}

TEST_F(NewHorizonsWarcastingTest, OrderReadinessIsAvailableThroughInclusiveExpiryAndCastRearmsIt)
{
	prepareWarcasting();
	const int32_t orderRound = battle()->battleGetRound();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_FALSE(std::ranges::any_of(server.battleLogLines, [](const auto & line)
	{
		return line.find("Warcasting adds") != std::string::npos;
	}));
	const auto orderToSpell = battle()->getWarcastingState(BattleSide::ATTACKER);
	EXPECT_EQ(newHorizonsWarcasting::spellBonus(orderToSpell, orderRound), 10);
	EXPECT_EQ(orderToSpell.expiryRound, orderRound + 1);

	advanceRound();
	ASSERT_EQ(battle()->battleGetRound(), orderRound + 1);
	EXPECT_EQ(newHorizonsWarcasting::spellBonus(battle()->getWarcastingState(BattleSide::ATTACKER),
		battle()->battleGetRound()), 10);
	const auto beforeInvalid = battle()->getWarcastingState(BattleSide::ATTACKER);
	EXPECT_FALSE(issue(static_cast<HeroCommand>(127)));
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER), beforeInvalid);

	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER).nextEligibleAction,
		AlternatingHeroActionState::Action::ORDER);
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER).empowermentPercent, 10);

	advanceRound();
	EXPECT_EQ(newHorizonsWarcasting::orderBonus(battle()->getWarcastingState(BattleSide::ATTACKER),
		battle()->battleGetRound()), 10);
	advanceRound();
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER), AlternatingHeroActionState{});
}

TEST_F(NewHorizonsWarcastingTest, MartialChannelingAddsOnlyToSpellToOrderReadiness)
{
	prepareWarcasting();
	EXPECT_EQ(savedPerkDefinition(martialChannelingPerk)["effect"]["status"].String(), "active");
	selectWarcastingPerk(martialChannelingPerk);
	const int32_t spellRound = battle()->getRound();

	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto spellToOrder = battle()->getWarcastingState(BattleSide::ATTACKER);
	EXPECT_EQ(spellToOrder.nextEligibleAction, AlternatingHeroActionState::Action::ORDER);
	EXPECT_EQ(spellToOrder.empowermentPercent, 20);
	EXPECT_EQ(spellToOrder.expiryRound, spellRound + 1);
	auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->getWarcastingState(BattleSide::ATTACKER), spellToOrder);
	EXPECT_EQ(newHorizonsWarcasting::orderBonus(restored->getWarcastingState(BattleSide::ATTACKER), spellRound), 20);

	advanceRound();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto orderState = battle()->getHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(orderState);
	EXPECT_EQ(orderState->warcastingBonusPercent, 20);
	const auto & chargeFormula = battle()->getHeroCommandRules()["commands"]["charge"]["effects"]["meleeDamagePercent"];
	EXPECT_EQ(heroCommands::coefficient(chargeFormula, *attackerSideHero, orderState->warcastingBonusPercent), 34);

	// Martial Channeling does not leak onto the Order-to-Spell direction.
	const auto orderToSpell = battle()->getWarcastingState(BattleSide::ATTACKER);
	EXPECT_EQ(orderToSpell.nextEligibleAction, AlternatingHeroActionState::Action::SPELL);
	EXPECT_EQ(orderToSpell.empowermentPercent, 10);
}

TEST_F(NewHorizonsWarcastingTest, ArcaneChannelingAddsOnlyToOrderToSpellReadiness)
{
	prepareWarcasting();
	EXPECT_EQ(savedPerkDefinition(arcaneChannelingPerk)["effect"]["status"].String(), "active");
	selectWarcastingPerk(arcaneChannelingPerk);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 24, ChangeValueMode::ABSOLUTE);
	const int32_t orderRound = battle()->getRound();

	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto orderToSpell = battle()->getWarcastingState(BattleSide::ATTACKER);
	EXPECT_EQ(orderToSpell.nextEligibleAction, AlternatingHeroActionState::Action::SPELL);
	EXPECT_EQ(orderToSpell.empowermentPercent, 20);
	EXPECT_EQ(orderToSpell.expiryRound, orderRound + 1);

	advanceRound();
	ASSERT_EQ(battle()->getRound(), orderRound + 1);
	const auto healthBefore = defender->getAvailableHealth();
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, defender));
	EXPECT_EQ(healthBefore - defender->getAvailableHealth(), 77);

	// Arcane Channeling does not leak onto the Spell-to-Order direction.
	const auto spellToOrder = battle()->getWarcastingState(BattleSide::ATTACKER);
	EXPECT_EQ(spellToOrder.nextEligibleAction, AlternatingHeroActionState::Action::ORDER);
	EXPECT_EQ(spellToOrder.empowermentPercent, 10);
}

TEST_F(NewHorizonsWarcastingTest, TacticalWeavingKeepsReadinessThroughSecondInclusiveRound)
{
	prepareWarcasting(2);
	EXPECT_EQ(savedPerkDefinition(tacticalWeavingPerk)["effect"]["status"].String(), "active");
	selectWarcastingPerk(tacticalWeavingPerk);
	const int32_t spellRound = battle()->getRound();

	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto spellToOrder = battle()->getWarcastingState(BattleSide::ATTACKER);
	EXPECT_EQ(spellToOrder.empowermentPercent, 20);
	EXPECT_EQ(spellToOrder.expiryRound, spellRound + 2);
	EXPECT_EQ(newHorizonsWarcasting::readinessLifetimeRounds(attackerSideHero), 2);

	advanceRound();
	advanceRound();
	ASSERT_EQ(battle()->getRound(), spellRound + 2);
	EXPECT_EQ(newHorizonsWarcasting::orderBonus(battle()->getWarcastingState(BattleSide::ATTACKER),
		battle()->getRound()), 20);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_TRUE(battle()->getHeroOrderState(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->getHeroOrderState(BattleSide::ATTACKER)->warcastingBonusPercent, 20);
}

TEST_F(NewHorizonsWarcastingTest, UnselectedPerksDoNotChangeBaseReadiness)
{
	prepareWarcasting(2);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(std::string(warcastingSkill), martialChannelingPerk));
	EXPECT_FALSE(attackerSideHero->hasActivePerk(std::string(warcastingSkill), arcaneChannelingPerk));
	EXPECT_FALSE(attackerSideHero->hasActivePerk(std::string(warcastingSkill), tacticalWeavingPerk));
	EXPECT_EQ(newHorizonsWarcasting::readinessLifetimeRounds(attackerSideHero), 1);

	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER).empowermentPercent, 20);
	advanceRound();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto orderState = battle()->getHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(orderState);
	EXPECT_EQ(orderState->warcastingBonusPercent, 20);
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER).empowermentPercent, 20);
}

TEST_F(NewHorizonsWarcastingTest, SavedPlannedMartialChannelingCannotBeSelectedOrBoostSpellToOrder)
{
	markPerkPlannedBeforeInitialization(martialChannelingPerk);
	prepareWarcasting();
	EXPECT_EQ(savedPerkDefinition(martialChannelingPerk)["effect"]["status"].String(), "planned");
	EXPECT_THROW(attackerSideHero->applyPerkSelection({std::string(warcastingSkill), martialChannelingPerk}),
		std::runtime_error);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(std::string(warcastingSkill), martialChannelingPerk));

	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER).empowermentPercent, 10);
	advanceRound();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_TRUE(battle()->getHeroOrderState(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->getHeroOrderState(BattleSide::ATTACKER)->warcastingBonusPercent, 10);
}

TEST_F(NewHorizonsWarcastingTest, SavedPlannedArcaneChannelingCannotBeSelectedOrBoostOrderToSpell)
{
	markPerkPlannedBeforeInitialization(arcaneChannelingPerk);
	prepareWarcasting();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 24, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(savedPerkDefinition(arcaneChannelingPerk)["effect"]["status"].String(), "planned");
	EXPECT_THROW(attackerSideHero->applyPerkSelection({std::string(warcastingSkill), arcaneChannelingPerk}),
		std::runtime_error);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(std::string(warcastingSkill), arcaneChannelingPerk));

	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER).empowermentPercent, 10);
	advanceRound();
	const auto healthBefore = defender->getAvailableHealth();
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, defender));
	EXPECT_EQ(healthBefore - defender->getAvailableHealth(), 72);
}

TEST_F(NewHorizonsWarcastingTest, RankLossDisablesSelectedChanneling)
{
	prepareWarcasting();
	selectWarcastingPerk(martialChannelingPerk);
	const int decodedWarcasting = SecondarySkill::decode(warcastingSkill);
	ASSERT_GE(decodedWarcasting, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(decodedWarcasting), 0, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(std::string(warcastingSkill), martialChannelingPerk));

	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER), AlternatingHeroActionState{});
	advanceRound();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_TRUE(battle()->getHeroOrderState(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->getHeroOrderState(BattleSide::ATTACKER)->warcastingBonusPercent, 0);
}

TEST_F(NewHorizonsWarcastingTest, RankLossDisablesAdvancedTacticalWeaving)
{
	prepareWarcasting(2);
	selectWarcastingPerk(tacticalWeavingPerk);
	const int decodedWarcasting = SecondarySkill::decode(warcastingSkill);
	ASSERT_GE(decodedWarcasting, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(decodedWarcasting), 1, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(std::string(warcastingSkill), tacticalWeavingPerk));
	EXPECT_EQ(newHorizonsWarcasting::readinessLifetimeRounds(attackerSideHero), 1);

	const int32_t spellRound = battle()->getRound();
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER).expiryRound, spellRound + 1);
}

TEST_F(NewHorizonsWarcastingTest, TacticalOrderToSpellReadinessExpiresUnusedAtRoundThree)
{
	prepareWarcasting(2);
	selectWarcastingPerk(tacticalWeavingPerk);
	const int32_t orderRound = battle()->getRound();

	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto orderToSpell = battle()->getWarcastingState(BattleSide::ATTACKER);
	EXPECT_EQ(orderToSpell.nextEligibleAction, AlternatingHeroActionState::Action::SPELL);
	EXPECT_EQ(orderToSpell.empowermentPercent, 20);
	EXPECT_EQ(orderToSpell.expiryRound, orderRound + 2);

	advanceRound();
	advanceRound();
	ASSERT_EQ(battle()->getRound(), orderRound + 2);
	EXPECT_EQ(newHorizonsWarcasting::spellBonus(battle()->getWarcastingState(BattleSide::ATTACKER),
		battle()->getRound()), 20);
	advanceRound();
	ASSERT_EQ(battle()->getRound(), orderRound + 3);
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER), AlternatingHeroActionState{});
	EXPECT_TRUE(server.casts.empty());
}

TEST_F(NewHorizonsWarcastingTest, MetamagicDeclineDoesNotChangeReadiness)
{
	prepareWarcasting(1, true);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto afterOrdinaryCast = battle()->getWarcastingState(BattleSide::ATTACKER);
	ASSERT_EQ(afterOrdinaryCast.nextEligibleAction, AlternatingHeroActionState::Action::ORDER);
	ASSERT_TRUE(declineMetamagic());
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER), afterOrdinaryCast);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).castSpellsCount, 1u);
}

TEST_F(NewHorizonsWarcastingTest, AcceptedMetamagicFollowupDoesNotChangeReadiness)
{
	prepareWarcasting(1, true);
	selectWarcastingPerk(martialChannelingPerk);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto beforeFollowup = battle()->getWarcastingState(BattleSide::ATTACKER);
	ASSERT_EQ(beforeFollowup.empowermentPercent, 20);
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true));
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER), beforeFollowup);
}

TEST_F(NewHorizonsWarcastingTest, ReadinessAndConsumedOrderBonusRoundTripAndOldReadsAreInert)
{
	SideInBattle currentSide(nullptr);
	currentSide.warcastingState = {AlternatingHeroActionState::Action::ORDER, 20, 7};
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	current.oser & currentSide;
	SideInBattle restoredSide(nullptr);
	current.iser & restoredSide;
	EXPECT_EQ(restoredSide.warcastingState, currentSide.warcastingState);

	HeroOrderState order;
	order.command = HeroCommand::CHARGE;
	order.issuedRound = 7;
	order.warcastingBonusPercent = 20;
	CMemorySerializer orderWire;
	orderWire.oser.version = ESerializationVersion::CURRENT;
	orderWire.iser.version = ESerializationVersion::CURRENT;
	orderWire.oser & order;
	HeroOrderState restoredOrder;
	orderWire.iser & restoredOrder;
	EXPECT_EQ(restoredOrder, order);

	CMemorySerializer oldSave;
	oldSave.oser.version = ESerializationVersion::NEW_HORIZONS_CURE_AFFLICTION;
	EXPECT_THROW(oldSave.oser & currentSide, std::runtime_error);
	EXPECT_TRUE(oldSave.extractBuffer().empty());
	CMemorySerializer oldOrderSave;
	oldOrderSave.oser.version = ESerializationVersion::NEW_HORIZONS_CURE_AFFLICTION;
	EXPECT_THROW(oldOrderSave.oser & order, std::runtime_error);
	EXPECT_TRUE(oldOrderSave.extractBuffer().empty());

	SideInBattle legacySide(nullptr);
	CMemorySerializer oldWire;
	oldWire.oser.version = ESerializationVersion::NEW_HORIZONS_CURE_AFFLICTION;
	oldWire.iser.version = ESerializationVersion::NEW_HORIZONS_CURE_AFFLICTION;
	oldWire.oser & legacySide;
	legacySide.warcastingState = currentSide.warcastingState;
	oldWire.iser & legacySide;
	EXPECT_EQ(legacySide.warcastingState, AlternatingHeroActionState{});
}

TEST_F(NewHorizonsWarcastingTest, CounteredEmpoweredSpellLogsConsumptionWithoutClaimingItsEffect)
{
	prepareWarcasting();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	advanceRound();
	const auto hasteEffect = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::HASTE)));
	ASSERT_FALSE(attacker->hasBonus(hasteEffect));
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_FALSE(attacker->hasBonus(hasteEffect));
	const auto casts = server.castsOf(SpellID::HASTE);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.counterspellNegated);
	EXPECT_TRUE(std::ranges::any_of(casts.front().logLines, [](const auto & line)
	{
		return line.find("consumes Warcasting (+10%) while casting") != std::string::npos
			&& line.find("the spell was counterspelled") != std::string::npos;
	}));
	EXPECT_EQ(newHorizonsWarcasting::spellBonus(battle()->getWarcastingState(BattleSide::ATTACKER),
		battle()->getRound()), 0);
}

TEST_F(NewHorizonsWarcastingTest, BattleMeditationRecoversManaAfterEmpoweredSpellsAndRearmsNextRound)
{
	activateBattleMeditationBeforeInitialization();
	prepareWarcasting();
	selectWarcastingPerk(battleMeditationPerk);

	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	advanceRound();
	const auto firstSpellRound = battle()->battleGetRound();
	const auto * haste = SpellID(SpellID::HASTE).toSpell();
	ASSERT_NE(haste, nullptr);
	const auto orderToSpell = battle()->getWarcastingState(BattleSide::ATTACKER);
	const auto manaBeforeRejectedCast = attackerSideHero->getManaAvailable();
	BattleAction rejected;
	rejected.actionType = EActionType::HERO_SPELL;
	rejected.side = BattleSide::ATTACKER;
	rejected.spell = SpellID::HASTE;
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), rejected));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeRejectedCast);
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER), orderToSpell);
	auto manaBeforeCast = attackerSideHero->getManaAvailable();
	const auto hasteCost = battle()->battleGetSpellCost(haste, attackerSideHero);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeCast - hasteCost + newHorizonsWarcasting::BATTLE_MEDITATION_MANA_RECOVERY);
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER).lastManaRecoveryRound, firstSpellRound);
	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->getWarcastingState(BattleSide::ATTACKER).lastManaRecoveryRound, firstSpellRound);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const auto & line)
	{
		return line.find("Orrin") != std::string::npos
			&& line.find("recovers 3 Mana from Battle Meditation after casting Haste") != std::string::npos;
	}));

	// The empowered Spell re-arms Order; an accepted Order next round re-arms
	// Spell again, and the round marker permits another recovery in that round.
	advanceRound();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER).lastManaRecoveryRound, firstSpellRound);
	advanceRound();
	const auto secondSpellRound = battle()->battleGetRound();
	ASSERT_NE(secondSpellRound, firstSpellRound);
	manaBeforeCast = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeCast - hasteCost + newHorizonsWarcasting::BATTLE_MEDITATION_MANA_RECOVERY);
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER).lastManaRecoveryRound, secondSpellRound);
}

TEST_F(NewHorizonsWarcastingTest, BattleMeditationCanRecoverOnAnAcceptedCounterspelledCast)
{
	activateBattleMeditationBeforeInitialization();
	prepareWarcasting();
	selectWarcastingPerk(battleMeditationPerk);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	advanceRound();
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;

	const auto * haste = SpellID(SpellID::HASTE).toSpell();
	const auto manaBeforeCast = attackerSideHero->getManaAvailable();
	const auto hasteCost = battle()->battleGetSpellCost(haste, attackerSideHero);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeCast - hasteCost + newHorizonsWarcasting::BATTLE_MEDITATION_MANA_RECOVERY);
	EXPECT_FALSE(attacker->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::HASTE)))));
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER).lastManaRecoveryRound, battle()->battleGetRound());
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const auto & line)
	{
		return line.find("Orrin") != std::string::npos
			&& line.find("recovers 3 Mana from Battle Meditation after casting Haste") != std::string::npos;
	}));
}

TEST_F(NewHorizonsWarcastingTest, BattleMeditationDoesNotRecoverTwiceInAMarkedRound)
{
	activateBattleMeditationBeforeInitialization();
	prepareWarcasting();
	selectWarcastingPerk(battleMeditationPerk);

	// Model a rearmed Order-to-Spell readiness with the per-round recovery
	// already spent; the normal hero-action budget prevents constructing two
	// accepted hero actions in one round through the public action path.
	const auto round = battle()->battleGetRound();
	auto & state = battle()->getSide(BattleSide::ATTACKER).warcastingState;
	state.nextEligibleAction = AlternatingHeroActionState::Action::SPELL;
	state.empowermentPercent = 10;
	state.expiryRound = round + 1;
	state.lastManaRecoveryRound = round;

	const auto * haste = SpellID(SpellID::HASTE).toSpell();
	const auto manaBeforeCast = attackerSideHero->getManaAvailable();
	const auto hasteCost = battle()->battleGetSpellCost(haste, attackerSideHero);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeCast - hasteCost);
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER).lastManaRecoveryRound, round);
}

TEST_F(NewHorizonsWarcastingTest, BattleMeditationDoesNotRecoverFromExpiredReadinessOrMetamagicFollowup)
{
	activateBattleMeditationBeforeInitialization();
	prepareWarcasting(1, true);
	selectWarcastingPerk(battleMeditationPerk);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	advanceRound();
	advanceRound();
	const auto * haste = SpellID(SpellID::HASTE).toSpell();
	auto manaBeforeCast = attackerSideHero->getManaAvailable();
	const auto hasteCost = battle()->battleGetSpellCost(haste, attackerSideHero);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeCast - hasteCost);
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER).lastManaRecoveryRound, -1);

	// Deliberately provide otherwise-eligible readiness to isolate the explicit
	// follow-up exclusion, rather than passing because the readiness is expired.
	auto & followupReadiness = battle()->getSide(BattleSide::ATTACKER).warcastingState;
	followupReadiness.recordAcceptedAction(AlternatingHeroActionState::Action::ORDER,
		battle()->getRound(), 10);
	ASSERT_TRUE(newHorizonsWarcasting::battleMeditationEligible(battle()->getMagicRules(),
		attackerSideHero, followupReadiness, battle()->getRound()));
	const auto readinessBeforeFollowup = followupReadiness;
	manaBeforeCast = attackerSideHero->getManaAvailable();
	const auto slowCost = battle()->battleGetSpellCost(SpellID(SpellID::SLOW).toSpell(), attackerSideHero);
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeCast - slowCost);
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER), readinessBeforeFollowup);
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER).lastManaRecoveryRound, -1);
	EXPECT_FALSE(std::ranges::any_of(server.battleLogLines, [](const auto & line)
	{
		return line.find("Battle Meditation") != std::string::npos;
	}));
}

TEST_F(NewHorizonsWarcastingTest, PlannedBattleMeditationIsInactive)
{
	markPerkPlannedBeforeInitialization(battleMeditationPerk);
	prepareWarcasting();
	ASSERT_FALSE(attackerSideHero->hasActivePerk(warcastingSkill, battleMeditationPerk));
	EXPECT_THROW(attackerSideHero->applyPerkSelection({std::string(warcastingSkill), battleMeditationPerk}),
		std::runtime_error);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	advanceRound();
	const auto manaBeforeCast = attackerSideHero->getManaAvailable();
	const auto hasteCost = battle()->battleGetSpellCost(SpellID(SpellID::HASTE).toSpell(), attackerSideHero);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeCast - hasteCost);
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER).lastManaRecoveryRound, -1);
}

TEST_F(NewHorizonsWarcastingTest, FreshBattleStartRoundTripsBeforeAnyHeroActionIsGranted)
{
	startGame();
	startBattle();
	ASSERT_EQ(battle()->getRound(), 0);
	EXPECT_EQ(battle()->battleHeroActionAllowanceCounts(BattleSide::ATTACKER).heroActions, 0u);
	EXPECT_NO_THROW(EXPECT_FALSE(battle()->battleCanUseMetamagicFollowup(BattleSide::ATTACKER)));
	EXPECT_NO_THROW(EXPECT_NE(battle()->battleCanCastSpell(attackerSideHero, spells::Mode::HERO),
		ESpellCastProblem::OK));
	BattleStart packet;
	packet.battleID = BattleID(0);
	ASSERT_NO_THROW(packet.info = CMemorySerializer::deepCopy(*battle(), gameState().get()));
	std::unique_ptr<BattleStart> restored;
	ASSERT_NO_THROW(restored = CMemorySerializer::deepCopy(packet, gameState().get()));
	ASSERT_TRUE(restored && restored->info);
	EXPECT_EQ(restored->info->getRound(), 0);
	EXPECT_EQ(restored->info->battleHeroActionAllowanceCounts(BattleSide::ATTACKER).heroActions, 0u);
	EXPECT_EQ(restored->info->getHeroActionAllowances(BattleSide::ATTACKER).currentRound, -1);

	auto & preplayAllowances = battle()->getSide(BattleSide::ATTACKER).heroActionAllowances;
	preplayAllowances.resetForRound(0);
	preplayAllowances.grantAllowance(HeroActionAllowanceState::AllowanceKind::ORDER,
		HeroActionAllowanceState::GrantSource::PERK, 0);
	EXPECT_THROW(CMemorySerializer::deepCopy(*battle(), gameState().get()), std::runtime_error);
}

TEST_F(NewHorizonsWarcastingTest, TypedOrderAfterHeroSpellPreservesMetamagicAndRoundTrips)
{
	prepareWarcasting(1, true);
	const auto side = BattleSide::ATTACKER;
	const auto round = battle()->battleGetRound();
	const auto orderGrantId = grantOrderAllowanceForFixture(side);
	const auto beforeSpellCounts = battle()->getHeroActionAllowances(side).remainingCounts(round);
	EXPECT_EQ(beforeSpellCounts.heroActions, 1u);
	EXPECT_EQ(beforeSpellCounts.orderActions, 1u);
	EXPECT_EQ(beforeSpellCounts.spellActions, 0u);

	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto & afterSpell = battle()->getSide(side);
	ASSERT_EQ(afterSpell.castSpellsCount, 1u);
	ASSERT_EQ(afterSpell.metamagicPendingCount, 1u);
	ASSERT_EQ(afterSpell.metamagicSequenceSpells, (std::vector<SpellID>{SpellID::HASTE}));
	const auto afterSpellCounts = afterSpell.heroActionAllowances.remainingCounts(round);
	EXPECT_EQ(afterSpellCounts.heroActions, 0u);
	EXPECT_EQ(afterSpellCounts.orderActions, 1u);
	EXPECT_EQ(afterSpellCounts.spellActions, 1u);

	const auto eligibleOrder = afterSpell.heroActionAllowances.eligibleAllowance(
		HeroActionAllowanceState::ActionKind::ORDER, round);
	ASSERT_TRUE(eligibleOrder);
	EXPECT_EQ(eligibleOrder->grantId, orderGrantId);
	EXPECT_EQ(eligibleOrder->allowance, HeroActionAllowanceState::AllowanceKind::ORDER);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));

	const auto & afterOrder = battle()->getSide(side);
	EXPECT_EQ(battle()->getHeroCommandUsed(side), true);
	EXPECT_EQ(battle()->getActiveOrder(side), HeroCommand::CHARGE);
	EXPECT_EQ(afterOrder.castSpellsCount, 1u);
	EXPECT_EQ(afterOrder.usedSpellsHistory, (std::vector<SpellID>{SpellID::HASTE}));
	EXPECT_EQ(afterOrder.metamagicPendingCount, 1u);
	EXPECT_EQ(afterOrder.metamagicSequenceSpells, (std::vector<SpellID>{SpellID::HASTE}));
	const auto afterOrderCounts = afterOrder.heroActionAllowances.remainingCounts(round);
	EXPECT_EQ(afterOrderCounts.heroActions, 0u);
	EXPECT_EQ(afterOrderCounts.orderActions, 0u);
	EXPECT_EQ(afterOrderCounts.spellActions, 1u);
	ASSERT_EQ(afterOrder.heroActionAllowances.grants.size(), 1u);
	EXPECT_EQ(afterOrder.heroActionAllowances.grants.front().allowance,
		HeroActionAllowanceState::AllowanceKind::SPELL);
	EXPECT_EQ(afterOrder.heroActionAllowances.grants.front().source,
		HeroActionAllowanceState::GrantSource::METAMAGIC);

	auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	const auto & restoredSide = restored->getSide(side);
	EXPECT_EQ(restoredSide.castSpellsCount, 1u);
	EXPECT_EQ(restoredSide.usedSpellsHistory, afterOrder.usedSpellsHistory);
	EXPECT_EQ(restoredSide.metamagicPendingCount, 1u);
	EXPECT_EQ(restoredSide.metamagicSequenceSpells, afterOrder.metamagicSequenceSpells);
	EXPECT_TRUE(restored->getHeroCommandUsed(side));
	EXPECT_EQ(restored->getActiveOrder(side), HeroCommand::CHARGE);
	EXPECT_EQ(restored->getHeroOrderState(side), battle()->getHeroOrderState(side));
	EXPECT_EQ(restored->getHeroActionAllowances(side), afterOrder.heroActionAllowances);
	const auto restoredCounts = restored->getHeroActionAllowances(side).remainingCounts(round);
	EXPECT_EQ(restoredCounts.orderActions, 0u);
	EXPECT_EQ(restoredCounts.spellActions, 1u);
}

TEST_F(NewHorizonsWarcastingTest, NormalMetamagicOfferAfterGrandUseRoundTrips)
{
	prepareWarcasting(1, true);
	const int decodedMetamagic = SecondarySkill::decode(metamagicSkill);
	ASSERT_GE(decodedMetamagic, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(decodedMetamagic), 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({std::string(metamagicSkill), std::string(metamagicGrandPerk)});

	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true, true));
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, defender, true));
	ASSERT_TRUE(battle()->getSide(BattleSide::ATTACKER).metamagicGrandUsed);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 0u);

	advanceRound();
	activate(attacker);
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, defender));
	const auto & afterNewOffer = battle()->getSide(BattleSide::ATTACKER);
	EXPECT_TRUE(afterNewOffer.metamagicGrandUsed);
	ASSERT_EQ(afterNewOffer.metamagicPendingCount, 1u);
	ASSERT_EQ(afterNewOffer.metamagicSequenceSpells, (std::vector<SpellID>{SpellID::MAGIC_ARROW}));
	ASSERT_EQ(afterNewOffer.heroActionAllowances.grants.size(), 1u);
	EXPECT_EQ(afterNewOffer.heroActionAllowances.grants.front().source,
		HeroActionAllowanceState::GrantSource::METAMAGIC);

	auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	const auto & restoredSide = restored->getSide(BattleSide::ATTACKER);
	EXPECT_TRUE(restoredSide.metamagicGrandUsed);
	EXPECT_EQ(restoredSide.metamagicPendingCount, 1u);
	EXPECT_EQ(restoredSide.metamagicSequenceSpells, afterNewOffer.metamagicSequenceSpells);
	EXPECT_EQ(restored->getHeroActionAllowances(BattleSide::ATTACKER),
		afterNewOffer.heroActionAllowances);
}

TEST_F(NewHorizonsWarcastingTest, HypotheticalBattleSpendsTypedAllowancesWithoutChangingLiveState)
{
	prepareWarcasting(1, true);
	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	const auto side = BattleSide::ATTACKER;
	const auto authoritative = battle()->getHeroActionAllowances(side);
	ASSERT_EQ(authoritative.remainingCounts(battle()->getRound()).heroActions, 1u);
	ASSERT_TRUE(projection.projectHeroSpellAllowance(side, SpellID::HASTE, attacker->unitId(), false, false));
	EXPECT_EQ(projection.battleHeroActionAllowanceCounts(side).heroActions, 0u);
	EXPECT_EQ(projection.battleHeroActionAllowanceCounts(side).spellActions, 1u);
	EXPECT_FALSE(projection.projectHeroOrderAllowance(side));
	const auto readiness = projection.getWarcastingState(side);
	ASSERT_TRUE(projection.projectHeroSpellAllowance(side, SpellID::SLOW, defender->unitId(), true, false));
	EXPECT_EQ(projection.battleHeroActionAllowanceCounts(side).spellActions, 0u);
	EXPECT_EQ(projection.getMetamagicUsesConsumed(side), 1);
	EXPECT_EQ(projection.getWarcastingState(side), readiness);
	EXPECT_EQ(battle()->getHeroActionAllowances(side), authoritative);
	EXPECT_EQ(battle()->getMetamagicUsesConsumed(side), 0);
	projection.nextRound();
	EXPECT_EQ(projection.battleHeroActionAllowanceCounts(side).heroActions, 1u);
	EXPECT_EQ(projection.battleHeroActionAllowanceCounts(side).spellActions, 0u);
	ASSERT_TRUE(projection.projectHeroOrderAllowance(side));
	EXPECT_EQ(projection.battleHeroActionAllowanceCounts(side).heroActions, 0u);
}

TEST_F(NewHorizonsWarcastingTest, HypotheticalHeroReceiptExpiresOnlyItsTimeStopAndWard)
{
	prepareWarcasting(1, true);
	auto attackerMarker = testTimeStopMarker(BattleSide::ATTACKER);
	auto defenderMarker = testTimeStopMarker(BattleSide::DEFENDER);
	battle()->addOrUpdateUnitBonus(attacker, *attackerMarker, true);
	battle()->addOrUpdateUnitBonus(defender, *defenderMarker, true);
	battle()->notePendingTimeStopHeroAction(BattleSide::ATTACKER);
	battle()->notePendingTimeStopHeroAction(BattleSide::DEFENDER);
	battle()->getSide(BattleSide::ATTACKER).counterspellArmed = true;
	battle()->getSide(BattleSide::ATTACKER).metamagicCountersequenceArmed = true;
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;

	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	const auto prepared = projection.prepareHeroSpellAllowance(BattleSide::ATTACKER, false, false);
	ASSERT_TRUE(prepared);
	EXPECT_EQ(prepared->action.receipt.allowance, HeroActionAllowanceState::AllowanceKind::HERO);
	ASSERT_TRUE(projection.beginProjectedHeroAction(BattleSide::ATTACKER, *prepared));

	EXPECT_FALSE(projection.getCounterspellArmed(BattleSide::ATTACKER));
	EXPECT_FALSE(projection.getMetamagicCountersequenceArmed(BattleSide::ATTACKER));
	EXPECT_TRUE(projection.getCounterspellArmed(BattleSide::DEFENDER));
	auto projectedAttacker = projection.battleGetUnitByID(attacker->unitId());
	auto projectedDefender = projection.battleGetUnitByID(defender->unitId());
	ASSERT_NE(projectedAttacker, nullptr);
	ASSERT_NE(projectedDefender, nullptr);
	EXPECT_FALSE(projectedAttacker->isTimeStopped());
	EXPECT_TRUE(projectedDefender->isTimeStopped());
	EXPECT_EQ(projection.getProjectedPendingTimeStopHeroActionSides(), 2u);
	EXPECT_TRUE(attacker->isTimeStopped());
	EXPECT_TRUE(defender->isTimeStopped());
	EXPECT_EQ(battle()->getPendingTimeStopHeroActionSides(), 3u);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);
}

TEST_F(NewHorizonsWarcastingTest, TypedSpellAndOrderReceiptsPreserveOwnTimeStopAndWard)
{
	prepareWarcasting(1, true);
	const auto round = battle()->battleGetRound();
	auto & allowances = battle()->getSide(BattleSide::ATTACKER).heroActionAllowances;
	allowances.grantAllowance(HeroActionAllowanceState::AllowanceKind::SPELL,
		HeroActionAllowanceState::GrantSource::PERK, round);
	allowances.grantAllowance(HeroActionAllowanceState::AllowanceKind::ORDER,
		HeroActionAllowanceState::GrantSource::PERK, round);
	auto marker = testTimeStopMarker(BattleSide::ATTACKER);
	battle()->addOrUpdateUnitBonus(attacker, *marker, true);
	battle()->notePendingTimeStopHeroAction(BattleSide::ATTACKER);
	battle()->getSide(BattleSide::ATTACKER).counterspellArmed = true;
	battle()->getSide(BattleSide::ATTACKER).metamagicCountersequenceArmed = true;

	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	auto spell = projection.prepareHeroSpellAllowance(BattleSide::ATTACKER, false, false);
	ASSERT_TRUE(spell);
	EXPECT_EQ(spell->action.receipt.allowance, HeroActionAllowanceState::AllowanceKind::SPELL);
	EXPECT_EQ(spell->action.receipt.source, HeroActionAllowanceState::GrantSource::PERK);
	ASSERT_TRUE(projection.beginProjectedHeroAction(BattleSide::ATTACKER, *spell));
	ASSERT_TRUE(projection.projectAcceptedHeroSpell(BattleSide::ATTACKER, SpellID::HASTE,
		attacker->unitId(), false, false, false, false, *spell));
	EXPECT_TRUE(projection.getCounterspellArmed(BattleSide::ATTACKER));
	EXPECT_TRUE(projection.getMetamagicCountersequenceArmed(BattleSide::ATTACKER));
	EXPECT_TRUE(projection.battleGetUnitByID(attacker->unitId())->isTimeStopped());
	EXPECT_EQ(projection.getProjectedPendingTimeStopHeroActionSides(), 1u);

	auto order = projection.prepareHeroOrderAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(order);
	EXPECT_EQ(order->action.receipt.allowance, HeroActionAllowanceState::AllowanceKind::ORDER);
	ASSERT_TRUE(projection.beginProjectedHeroAction(BattleSide::ATTACKER, *order));
	ASSERT_TRUE(projection.projectAcceptedHeroOrder(BattleSide::ATTACKER, *order));
	EXPECT_TRUE(projection.getCounterspellArmed(BattleSide::ATTACKER));
	EXPECT_TRUE(projection.getMetamagicCountersequenceArmed(BattleSide::ATTACKER));
	EXPECT_TRUE(projection.battleGetUnitByID(attacker->unitId())->isTimeStopped());
	EXPECT_EQ(projection.getProjectedPendingTimeStopHeroActionSides(), 1u);
}

TEST_F(NewHorizonsWarcastingTest, PreparedAllowanceCommitCannotBeReplayed)
{
	prepareWarcasting(1, true);
	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	const auto prepared = projection.prepareHeroSpellAllowance(BattleSide::ATTACKER, false, false);
	ASSERT_TRUE(prepared);
	ASSERT_TRUE(projection.beginProjectedHeroAction(BattleSide::ATTACKER, *prepared));
	ASSERT_TRUE(projection.projectAcceptedHeroSpell(BattleSide::ATTACKER, SpellID::HASTE,
		attacker->unitId(), false, false, false, false, *prepared));
	const auto afterCommit = projection.getHeroActionAllowances(BattleSide::ATTACKER);
	EXPECT_FALSE(projection.projectAcceptedHeroSpell(BattleSide::ATTACKER, SpellID::HASTE,
		attacker->unitId(), false, false, false, false, *prepared));
	EXPECT_EQ(projection.getHeroActionAllowances(BattleSide::ATTACKER), afterCommit);
	EXPECT_EQ(projection.getMetamagicUsesConsumed(BattleSide::ATTACKER), 0);
}

TEST_F(NewHorizonsWarcastingTest, StalePreparedActionCannotClearEffectsBeforeCommitRejection)
{
	prepareWarcasting(1, true);
	const auto side = BattleSide::ATTACKER;
	const auto round = battle()->battleGetRound();
	battle()->getSide(side).heroActionAllowances.grantAllowance(
		HeroActionAllowanceState::AllowanceKind::ORDER,
		HeroActionAllowanceState::GrantSource::PERK, round);
	auto marker = testTimeStopMarker(side);
	battle()->addOrUpdateUnitBonus(attacker, *marker, true);
	battle()->notePendingTimeStopHeroAction(side);
	battle()->getSide(side).counterspellArmed = true;
	battle()->getSide(side).metamagicCountersequenceArmed = true;

	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	const auto staleSpell = projection.prepareHeroSpellAllowance(side, false, false);
	ASSERT_TRUE(staleSpell);

	// A typed Order consumes a different grant without crossing the Hero-action
	// boundary. It makes the prepared spell token stale while preserving effects.
	ASSERT_TRUE(projection.projectHeroOrderAllowance(side));
	EXPECT_TRUE(projection.getCounterspellArmed(side));
	EXPECT_TRUE(projection.getMetamagicCountersequenceArmed(side));
	EXPECT_EQ(projection.getProjectedPendingTimeStopHeroActionSides(), 1u);
	auto projectedAttacker = projection.battleGetUnitByID(attacker->unitId());
	ASSERT_NE(projectedAttacker, nullptr);
	EXPECT_TRUE(projectedAttacker->isTimeStopped());

	EXPECT_FALSE(projection.beginProjectedHeroAction(side, *staleSpell));
	EXPECT_FALSE(projection.projectAcceptedHeroSpell(side, SpellID::HASTE,
		attacker->unitId(), false, false, false, false, *staleSpell));
	EXPECT_TRUE(projection.getCounterspellArmed(side));
	EXPECT_TRUE(projection.getMetamagicCountersequenceArmed(side));
	EXPECT_EQ(projection.getProjectedPendingTimeStopHeroActionSides(), 1u);
	EXPECT_TRUE(projectedAttacker->isTimeStopped());
}

TEST_F(NewHorizonsWarcastingTest, BegunOrderTokenCannotAuthorizeSameEpochSpellCommit)
{
	prepareWarcasting(1, true);
	const auto side = BattleSide::ATTACKER;
	const auto round = battle()->battleGetRound();
	battle()->getSide(side).heroActionAllowances.grantAllowance(
		HeroActionAllowanceState::AllowanceKind::ORDER,
		HeroActionAllowanceState::GrantSource::PERK, round);
	auto marker = testTimeStopMarker(side);
	battle()->addOrUpdateUnitBonus(attacker, *marker, true);
	battle()->notePendingTimeStopHeroAction(side);
	battle()->getSide(side).counterspellArmed = true;
	battle()->getSide(side).metamagicCountersequenceArmed = true;

	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	const auto spell = projection.prepareHeroSpellAllowance(side, false, false);
	const auto order = projection.prepareHeroOrderAllowance(side);
	ASSERT_TRUE(spell);
	ASSERT_TRUE(order);
	EXPECT_EQ(spell->action.receipt.allowance, HeroActionAllowanceState::AllowanceKind::HERO);
	EXPECT_EQ(order->action.receipt.allowance, HeroActionAllowanceState::AllowanceKind::ORDER);

	ASSERT_TRUE(projection.beginProjectedHeroAction(side, *order));
	EXPECT_FALSE(projection.projectAcceptedHeroSpell(side, SpellID::HASTE,
		attacker->unitId(), false, false, false, false, *spell));
	EXPECT_TRUE(projection.getCounterspellArmed(side));
	EXPECT_TRUE(projection.getMetamagicCountersequenceArmed(side));
	EXPECT_EQ(projection.getProjectedPendingTimeStopHeroActionSides(), 1u);
	auto projectedAttacker = projection.battleGetUnitByID(attacker->unitId());
	ASSERT_NE(projectedAttacker, nullptr);
	EXPECT_TRUE(projectedAttacker->isTimeStopped());

	ASSERT_TRUE(projection.projectAcceptedHeroOrder(side, *order));
	EXPECT_TRUE(projection.getCounterspellArmed(side));
	EXPECT_TRUE(projection.getMetamagicCountersequenceArmed(side));
	EXPECT_TRUE(projectedAttacker->isTimeStopped());
}

TEST_F(NewHorizonsWarcastingTest, HypotheticalCounterspellUsesCountersequenceAndRealManaThreshold)
{
	prepareWarcasting(1, true);
	const int listedCost = attackerSideHero->getListedSpellCost(SpellID(SpellID::HASTE).toSpell());
	const int wardCost = newHorizonsMagic::counterspellCost(listedCost, false, true);
	setTestSpellPointTotal(defenderSideHero, wardCost - 1);
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	battle()->getSide(BattleSide::DEFENDER).metamagicCountersequenceArmed = true;

	WarcastingEnvironment environment(gameState());
	// Counterspell resolution needs the opposing hero's saved perk and mana.
	// Use an all-knowing battle view here; the player-scoped attacker view
	// intentionally hides the defender hero.
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor::SPECTATOR);
	ASSERT_NE(callback->battleGetFightingHero(BattleSide::DEFENDER), nullptr);
	HypotheticBattle lowManaProjection(&environment, callback);
	auto lowManaPrepared = lowManaProjection.prepareHeroSpellAllowance(BattleSide::ATTACKER, false, false);
	ASSERT_TRUE(lowManaPrepared);
	ASSERT_TRUE(lowManaProjection.beginProjectedHeroAction(BattleSide::ATTACKER, *lowManaPrepared));
	auto outcome = lowManaProjection.resolveProjectedCounterspell(BattleSide::ATTACKER,
		SpellID(SpellID::HASTE).toSpell());
	EXPECT_TRUE(outcome.wardActive);
	EXPECT_EQ(outcome.wardSide, BattleSide::DEFENDER);
	ASSERT_TRUE(outcome.resolutionKnown);
	ASSERT_TRUE(outcome.manaCost.has_value());
	ASSERT_TRUE(outcome.negated.has_value());
	EXPECT_EQ(*outcome.manaCost, wardCost);
	EXPECT_FALSE(*outcome.negated);
	ASSERT_TRUE(lowManaProjection.projectAcceptedHeroSpell(BattleSide::ATTACKER, SpellID::HASTE,
		attacker->unitId(), false, false, outcome.wardActive, *outcome.negated, *lowManaPrepared));
	EXPECT_FALSE(lowManaProjection.getMetamagicFirstCounterspellNegated(BattleSide::ATTACKER));
	EXPECT_EQ(lowManaProjection.getMetamagicPendingCount(BattleSide::ATTACKER), 1);
	EXPECT_FALSE(lowManaProjection.getCounterspellArmed(BattleSide::DEFENDER));
	EXPECT_EQ(defenderSideHero->getManaAvailable(), wardCost - 1);

	setTestSpellPointTotal(defenderSideHero, wardCost);
	HypotheticBattle negatedProjection(&environment, callback);
	const auto prepared = negatedProjection.prepareHeroSpellAllowance(BattleSide::ATTACKER, false, false);
	ASSERT_TRUE(prepared);
	ASSERT_TRUE(negatedProjection.beginProjectedHeroAction(BattleSide::ATTACKER, *prepared));
	outcome = negatedProjection.resolveProjectedCounterspell(BattleSide::ATTACKER,
		SpellID(SpellID::HASTE).toSpell());
	ASSERT_TRUE(outcome.resolutionKnown);
	ASSERT_TRUE(outcome.manaCost.has_value());
	ASSERT_TRUE(outcome.negated.has_value());
	EXPECT_TRUE(*outcome.negated);
	EXPECT_EQ(*outcome.manaCost, wardCost);
	ASSERT_TRUE(negatedProjection.projectAcceptedHeroSpell(BattleSide::ATTACKER, SpellID::HASTE,
		attacker->unitId(), false, false, outcome.wardActive, *outcome.negated, *prepared));
	EXPECT_TRUE(negatedProjection.getMetamagicFirstCounterspellNegated(BattleSide::ATTACKER));
	EXPECT_EQ(negatedProjection.getMetamagicPendingCount(BattleSide::ATTACKER), 1);
	EXPECT_FALSE(negatedProjection.getCounterspellArmed(BattleSide::DEFENDER));
	EXPECT_EQ(defenderSideHero->getManaAvailable(), wardCost);
}

TEST_F(NewHorizonsWarcastingTest, HypotheticalCounterspellUsesCountermageCostWithoutSpendingMana)
{
	prepareWarcasting(1, true, true);
	const int listedCost = attackerSideHero->getListedSpellCost(SpellID(SpellID::HASTE).toSpell());
	const int wardCost = newHorizonsMagic::counterspellCost(listedCost, true, false);
	setTestSpellPointTotal(defenderSideHero, wardCost - 1);
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;

	WarcastingEnvironment environment(gameState());
	// Counterspell resolution needs the opposing hero's saved perk and mana.
	// Use an all-knowing battle view here; the player-scoped attacker view
	// intentionally hides the defender hero.
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor::SPECTATOR);
	ASSERT_NE(callback->battleGetFightingHero(BattleSide::DEFENDER), nullptr);
	HypotheticBattle projection(&environment, callback);
	const auto outcome = projection.resolveProjectedCounterspell(BattleSide::ATTACKER,
		SpellID(SpellID::HASTE).toSpell());
	EXPECT_TRUE(outcome.wardActive);
	ASSERT_TRUE(outcome.resolutionKnown);
	ASSERT_TRUE(outcome.manaCost.has_value());
	ASSERT_TRUE(outcome.negated.has_value());
	EXPECT_EQ(*outcome.manaCost, wardCost);
	EXPECT_FALSE(*outcome.negated);
	EXPECT_EQ(defenderSideHero->getManaAvailable(), wardCost - 1);
}

TEST_F(NewHorizonsWarcastingTest, PlayerViewKeepsHiddenArmedCounterspellUnresolved)
{
	prepareWarcasting(1, true);
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	setTestSpellPointTotal(defenderSideHero, 0);
	ASSERT_FALSE(defenderSideHero->hasActivePerk(std::string(sorcerySkill), std::string(countermagePerk)));

	WarcastingEnvironment environment(gameState());
	auto projectFromAttackerView = [&]()
	{
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		EXPECT_EQ(callback->battleGetFightingHero(BattleSide::DEFENDER), nullptr);
		EXPECT_TRUE(callback->battleWasCounterspellArmed(BattleSide::DEFENDER));
		HypotheticBattle projection(&environment, callback);
		const auto allowancesBefore = projection.getHeroActionAllowances(BattleSide::ATTACKER);
		const auto outcome = projection.resolveProjectedCounterspell(BattleSide::ATTACKER,
			SpellID(SpellID::HASTE).toSpell());
		EXPECT_TRUE(outcome.wardActive);
		EXPECT_EQ(outcome.wardSide, BattleSide::DEFENDER);
		EXPECT_FALSE(outcome.resolutionKnown);
		EXPECT_FALSE(outcome.manaCost.has_value());
		EXPECT_FALSE(outcome.negated.has_value());

		// The convenience wrapper must not turn an unknown ward into a resolved
		// cast or spend the projected allowance while doing so.
		EXPECT_FALSE(projection.projectHeroSpellAllowance(BattleSide::ATTACKER, SpellID::HASTE,
			attacker->unitId(), false, false));
		EXPECT_EQ(projection.getHeroActionAllowances(BattleSide::ATTACKER), allowancesBefore);
		EXPECT_TRUE(projection.getCounterspellArmed(BattleSide::DEFENDER));
		return outcome;
	};

	const auto withoutCountermage = projectFromAttackerView();
	ASSERT_GE(SecondarySkill::decode(sorcerySkill), 0);
	defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(sorcerySkill)),
		MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	defenderSideHero->applyPerkSelection({std::string(sorcerySkill), std::string(countermagePerk)});
	ASSERT_TRUE(defenderSideHero->hasActivePerk(std::string(sorcerySkill), std::string(countermagePerk)));
	setTestSpellPointTotal(defenderSideHero, 1000);
	const auto withCountermage = projectFromAttackerView();

	EXPECT_EQ(withCountermage.wardActive, withoutCountermage.wardActive);
	EXPECT_EQ(withCountermage.wardSide, withoutCountermage.wardSide);
	EXPECT_EQ(withCountermage.resolutionKnown, withoutCountermage.resolutionKnown);
	EXPECT_EQ(withCountermage.manaCost, withoutCountermage.manaCost);
	EXPECT_EQ(withCountermage.negated, withoutCountermage.negated);
}

TEST_F(NewHorizonsWarcastingTest, TypedCounterspellReplacesOldCountersequenceProvenance)
{
	prepareWarcasting();
	const auto round = battle()->battleGetRound();
	battle()->getSide(BattleSide::ATTACKER).heroActionAllowances.grantAllowance(
		HeroActionAllowanceState::AllowanceKind::SPELL,
		HeroActionAllowanceState::GrantSource::PERK, round);
	battle()->getSide(BattleSide::ATTACKER).counterspellArmed = true;
	battle()->getSide(BattleSide::ATTACKER).metamagicCountersequenceArmed = true;

	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	const auto prepared = projection.prepareHeroSpellAllowance(BattleSide::ATTACKER, false, false);
	ASSERT_TRUE(prepared);
	EXPECT_EQ(prepared->action.receipt.allowance, HeroActionAllowanceState::AllowanceKind::SPELL);
	ASSERT_TRUE(projection.beginProjectedHeroAction(BattleSide::ATTACKER, *prepared));
	const auto id = counterspellId();
	ASSERT_TRUE(id.hasValue());
	ASSERT_TRUE(projection.projectAcceptedHeroSpell(BattleSide::ATTACKER, id,
		std::numeric_limits<uint32_t>::max(), false, false, false, false, *prepared));
	EXPECT_TRUE(projection.getCounterspellArmed(BattleSide::ATTACKER));
	EXPECT_FALSE(projection.getMetamagicCountersequenceArmed(BattleSide::ATTACKER));
}

TEST_F(NewHorizonsWarcastingTest, HypotheticalBattleCopiesAndExpiresItsReadinessIndependently)
{
	prepareWarcasting();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	battle()->getSide(BattleSide::ATTACKER).warcastingState.lastManaRecoveryRound = battle()->getRound();
	const auto authoritative = battle()->getWarcastingState(BattleSide::ATTACKER);
	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	EXPECT_EQ(projection.getWarcastingState(BattleSide::ATTACKER), authoritative);

	projection.nextRound();
	EXPECT_EQ(newHorizonsWarcasting::spellBonus(projection.getWarcastingState(BattleSide::ATTACKER),
		projection.getRound()), 10);
	EXPECT_EQ(projection.getWarcastingState(BattleSide::ATTACKER).lastManaRecoveryRound,
		authoritative.lastManaRecoveryRound);
	projection.nextRound();
	EXPECT_EQ(projection.getWarcastingState(BattleSide::ATTACKER).nextEligibleAction,
		AlternatingHeroActionState::Action::NONE);
	EXPECT_EQ(projection.getWarcastingState(BattleSide::ATTACKER).lastManaRecoveryRound,
		authoritative.lastManaRecoveryRound);
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER), authoritative);
}
