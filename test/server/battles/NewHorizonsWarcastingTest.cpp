/*
 * NewHorizonsWarcastingTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsWarcasting.h"
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
constexpr auto martialChannelingPerk = "new-horizons:warcasting.martialChanneling";
constexpr auto arcaneChannelingPerk = "new-horizons:warcasting.arcaneChanneling";
constexpr auto tacticalWeavingPerk = "new-horizons:warcasting.tacticalWeaving";

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
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perkRules);
	}

	void markPerkPlannedBeforeInitialization(const std::string & perkId)
	{
		plannedWarcastingPerkBeforeInit = perkId;
	}

	void prepareWarcasting(int rank = 1, bool withMetamagic = false)
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
		attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->mana = 1000;
		defenderSideHero->mana = 1000;
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

	bool cast(SpellID spell, const CStack * target, bool followup = false)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.metamagicFollowup = followup;
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

TEST_F(NewHorizonsWarcastingTest, HypotheticalBattleCopiesAndExpiresItsReadinessIndependently)
{
	prepareWarcasting();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto authoritative = battle()->getWarcastingState(BattleSide::ATTACKER);
	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	EXPECT_EQ(projection.getWarcastingState(BattleSide::ATTACKER), authoritative);

	projection.nextRound();
	EXPECT_EQ(newHorizonsWarcasting::spellBonus(projection.getWarcastingState(BattleSide::ATTACKER),
		projection.getRound()), 10);
	projection.nextRound();
	EXPECT_EQ(projection.getWarcastingState(BattleSide::ATTACKER), AlternatingHeroActionState{});
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER), authoritative);
}
