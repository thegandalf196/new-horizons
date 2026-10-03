/*
 * NewHorizonsShroudNoEscapeTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsShroud.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/CHeroClass.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>

namespace
{
class NoEscapeEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit NoEscapeEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsShroudNoEscapeTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
		for(auto & perk : rules["skills"][std::string(newHorizonsShroud::SKILL_ID)]["perks"].Vector())
		{
			if(perk["id"].String() != newHorizonsShroud::NO_ESCAPE_PERK_ID)
				continue;
			const auto status = perk["effect"]["status"].String();
			if(status == "active")
				return;
			if(status != "planned")
				throw std::runtime_error("No Escape must be planned or active in canonical content");
			perk["effect"]["status"].String() = "active";
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(rules));
			return;
		}
		throw std::runtime_error("Missing No Escape registry entry");
	}

	HeroTypeID heroTypeForFaction(FactionID faction) const
	{
		for(const auto & id : LIBRARY->heroh->getDefaultAllowed())
		{
			const auto * hero = dynamic_cast<const CHero *>(id.toHeroType());
			if(hero && hero->heroClass && hero->heroClass->faction == faction)
				return id;
		}
		throw std::runtime_error("No default-allowed hero found for the requested fixture faction");
	}

	void neutralizeHero(CGHeroInstance * hero)
	{
		for(const auto & bonus : hero->getHeroType()->specialty)
			hero->removeBonus(bonus);
		for(int i = 0; i < LIBRARY->skillh->size(); ++i)
			hero->setSecSkillLevel(SecondarySkill(i), 0, ChangeValueMode::ABSOLUTE);
		for(auto skill : {PrimarySkill::ATTACK, PrimarySkill::DEFENSE, PrimarySkill::SPELL_POWER, PrimarySkill::KNOWLEDGE})
			hero->setPrimarySkill(skill, 0, ChangeValueMode::ABSOLUTE);
	}

	void startGameWithDungeonHero(BattleSide holderSide = BattleSide::ATTACKER)
	{
		const auto dungeonHero = heroTypeForFaction(FactionID::DUNGEON);
		const auto otherHero = heroTypeForFaction(FactionID::CASTLE);
		const auto attackerHero = holderSide == BattleSide::ATTACKER ? dungeonHero : otherHero;
		const auto defenderHero = holderSide == BattleSide::DEFENDER ? dungeonHero : otherHero;
		const CreatureID token(0);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.hero({5, 5, 0}, attackerHero, PlayerColor(0)).heroGarrison({{token, 1}})
			.hero({7, 7, 0}, defenderHero, PlayerColor(1)).heroGarrison({{token, 1}});
		startWithMap(std::move(builder));

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_EQ(holderSide == BattleSide::ATTACKER ? attackerSideHero->getHeroClass()->faction
			: defenderSideHero->getHeroClass()->faction, FactionID::DUNGEON);
		neutralizeHero(attackerSideHero);
		neutralizeHero(defenderSideHero);
	}

	SecondarySkill shroudSkill() const
	{
		const int decoded = SecondarySkill::decode(std::string(newHorizonsShroud::SKILL_ID));
		if(decoded < 0)
			throw std::runtime_error("Missing New Horizons Shroud of Malassa skill");
		return SecondarySkill(decoded);
	}

	bool offerContains(CGHeroInstance * hero, std::string_view perkId)
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			if(std::ranges::any_of(offers, [perkId](const auto & offer)
			{
				return offer.selection.skillId == newHorizonsShroud::SKILL_ID
					&& offer.selection.perkId == perkId;
			}))
				return true;
		}
		return false;
	}

	void acceptPerk(CGHeroInstance * hero, std::string_view perkId)
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
			{
				if(offers[choice].selection.skillId == newHorizonsShroud::SKILL_ID
					&& offers[choice].selection.perkId == perkId)
				{
					gameHandler->levelUpHero(hero, offers, choice, seed, false);
					ASSERT_TRUE(hero->hasActivePerk(std::string(newHorizonsShroud::SKILL_ID), std::string(perkId)));
					return;
				}
			}
		}
		FAIL() << "No legal Shroud of Malassa offer for " << perkId;
	}

	void selectNoEscape(CGHeroInstance * hero)
	{
		hero->setSecSkillLevel(shroudSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		EXPECT_FALSE(offerContains(hero, newHorizonsShroud::NO_ESCAPE_PERK_ID))
			<< "No Escape is an Advanced Shroud perk";
		ASSERT_TRUE(offerContains(hero, newHorizonsShroud::BACKSTAB_PERK_ID));
		acceptPerk(hero, newHorizonsShroud::BACKSTAB_PERK_ID);
		hero->setSecSkillLevel(shroudSkill(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(offerContains(hero, newHorizonsShroud::NO_ESCAPE_PERK_ID));
		acceptPerk(hero, newHorizonsShroud::NO_ESCAPE_PERK_ID);
		ASSERT_TRUE(newHorizonsShroud::hasNoEscape(hero));
	}

	void removeStartingUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	bool act(const battle::Unit * stack, const BattleAction & action)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(stack->unitSide()), action);
	}

	void defendOnFirstQueueActivation(CStack * stack)
	{
		for(int attempt = 0; attempt < 24 && battle()->battleActiveUnit() != stack; ++attempt)
		{
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			ASSERT_TRUE(act(active, BattleAction::makeDefend(active)));
		}
		ASSERT_EQ(battle()->battleActiveUnit(), stack)
			<< "The target must receive a real ordinary queue activation before the attacks";
		ASSERT_TRUE(act(stack, BattleAction::makeDefend(stack)));
	}

	void startNoEscapeBattle(BattleSide holderSide = BattleSide::ATTACKER)
	{
		startGameWithDungeonHero(holderSide);
		selectNoEscape(holderSide == BattleSide::ATTACKER ? attackerSideHero : defenderSideHero);
		startBattle();
		removeStartingUnits();
	}

	TConstBonusListPtr noEscapeBonuses(const battle::Unit * unit) const
	{
		return unit->getAllBonuses(CSelector([](const Bonus * bonus)
		{
			return newHorizonsShroud::isNoEscapeSpeedPenalty(bonus);
		}));
	}

	bool hasNoEscapePenalty(const battle::Unit * unit) const
	{
		const auto bonuses = noEscapeBonuses(unit);
		return bonuses && !bonuses->empty();
	}

	std::size_t noEscapePenaltyCount(const battle::Unit * unit) const
	{
		const auto bonuses = noEscapeBonuses(unit);
		return bonuses ? bonuses->size() : 0;
	}

	bool shoot(const CStack * shooter, const CStack * target)
	{
		battle()->activeStack = shooter->unitId();
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(shooter->unitSide()), BattleAction::makeShotAttack(shooter, target));
	}
};
}

TEST_F(NewHorizonsShroudNoEscapeTest, LegalAdvancedOfferProjectsAndAppliesOnlyToRearMeleeVictim)
{
	startGameWithDungeonHero();
	selectNoEscape(attackerSideHero);
	startBattle();
	removeStartingUnits();
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 1000);
	auto * front = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(80), 10);
	auto * rear = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(82), 10);
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 10);
	auto * reserve = addStack(BattleSide::ATTACKER, creatureByName("core:zombie"), BattleHex(4, 9), 1);
	ASSERT_NE(defender, nullptr);
	ASSERT_NE(front, nullptr);
	ASSERT_NE(rear, nullptr);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(reserve, nullptr);
	blockRetaliation(front);
	blockRetaliation(rear);
	blockRetaliation(shooter);
	beginCombat();
	defendOnFirstQueueActivation(defender);

	const auto baseMovement = defender->getMovementRange();
	BattleAttackInfo frontInfo(front, defender, 0, false);
	BattleAttackInfo rangedInfo(rear, defender, 0, true);
	BattleAttackInfo collateralInfo(rear, defender, 0, false);
	collateralInfo.secondaryAttack = true;
	const BattleAttackInfo rearInfo(rear, defender, 0, false);
	EXPECT_FALSE(battle()->battleIsShroudFlankingAttack(frontInfo));
	EXPECT_FALSE(battle()->battleIsShroudFlankingAttack(rangedInfo));
	EXPECT_FALSE(battle()->battleIsShroudFlankingAttack(collateralInfo));
	ASSERT_TRUE(battle()->battleIsShroudFlankingAttack(rearInfo));

	NoEscapeEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	battle()->activeStack = rear->unitId();
	const auto rearPreview = AttackPossibility::evaluate(BattleAttackInfo(
		model->getForUpdate(rear->unitId()).get(), model->getForUpdate(defender->unitId()).get(), 0, false),
		rear->getPosition(), cache, model);
	ASSERT_NE(rearPreview.effectPreview, nullptr);
	ASSERT_FALSE(rearPreview.defenderDead);
	auto projectedVictim = rearPreview.effectPreview->getForUpdate(defender->unitId());
	ASSERT_NE(projectedVictim, nullptr);
	EXPECT_EQ(projectedVictim->getMovementRange(), baseMovement - 2);
	EXPECT_TRUE(hasNoEscapePenalty(projectedVictim.get()));

	const auto frontPreview = AttackPossibility::evaluate(BattleAttackInfo(
		model->getForUpdate(front->unitId()).get(), model->getForUpdate(defender->unitId()).get(), 0, false),
		front->getPosition(), cache, model);
	ASSERT_NE(frontPreview.effectPreview, nullptr);
	EXPECT_FALSE(hasNoEscapePenalty(frontPreview.effectPreview->getForUpdate(defender->unitId()).get()));
	EXPECT_EQ(defender->getMovementRange(), baseMovement)
		<< "Evaluating the AI branch cannot mutate the live target";
	EXPECT_FALSE(hasNoEscapePenalty(defender));

	ASSERT_TRUE(attack(front, defender->getPosition()));
	EXPECT_TRUE(front->alive());
	EXPECT_EQ(defender->getMovementRange(), baseMovement)
		<< "A front-side physical melee attack is not a Shroud flank";
	ASSERT_TRUE(shoot(shooter, defender));
	EXPECT_TRUE(shooter->alive());
	EXPECT_EQ(defender->getMovementRange(), baseMovement)
		<< "An ordinary ranged attack does not trigger No Escape";
	ASSERT_TRUE(attack(rear, defender->getPosition()));
	EXPECT_TRUE(rear->alive());
	EXPECT_TRUE(defender->alive());
	EXPECT_EQ(defender->getMovementRange(), baseMovement - 2);
	EXPECT_EQ(noEscapePenaltyCount(defender), 1u);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const auto & line)
	{
		return line.find("No Escape reduces") != std::string::npos;
	})) << "The applied penalty is reported in the combat log";
}

TEST_F(NewHorizonsShroudNoEscapeTest, RepeatedFlanksRefreshOnceUntilTheVictimsNextActivation)
{
	startNoEscapeBattle();
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 1000);
	auto * reserve = addStack(BattleSide::ATTACKER, creatureByName("core:zombie"), BattleHex(4, 9), 1);
	ASSERT_NE(defender, nullptr);
	ASSERT_NE(reserve, nullptr);
	std::vector<CStack *> flankers;
	for(const auto & hex : defender->getSurroundingHexes())
	{
		if(!hex.isAvailable() || battle()->battleGetStackByPos(hex))
			continue;
		auto * candidate = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), hex, 1);
		ASSERT_NE(candidate, nullptr);
		if(battle()->battleIsShroudFlankingAttack(BattleAttackInfo(candidate, defender, 0, false)))
			flankers.push_back(candidate);
		if(flankers.size() == 2)
			break;
	}
	ASSERT_EQ(flankers.size(), 2u) << "The fixture needs two legal rear-facing attack hexes";
	for(auto * flanker : flankers)
		blockRetaliation(flanker);
	beginCombat();
	defendOnFirstQueueActivation(defender);
	const auto baseMovement = defender->getMovementRange();
	ASSERT_TRUE(attack(flankers[0], defender->getPosition()));
	EXPECT_TRUE(flankers[0]->alive());
	EXPECT_TRUE(defender->alive());
	EXPECT_EQ(defender->getMovementRange(), baseMovement - 2);
	ASSERT_TRUE(attack(flankers[1], defender->getPosition()));
	EXPECT_TRUE(flankers[1]->alive());
	EXPECT_TRUE(defender->alive());
	EXPECT_EQ(defender->getMovementRange(), baseMovement - 2)
		<< "A later qualifying hit refreshes the same penalty rather than stacking to -4";
	EXPECT_EQ(noEscapePenaltyCount(defender), 1u);

	BattleSetActiveStack activation;
	activation.battleID = BattleID(0);
	activation.stack = defender->unitId();
	activation.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activation);
	EXPECT_FALSE(hasNoEscapePenalty(defender));
	EXPECT_EQ(defender->getMovementRange(), baseMovement)
		<< "The marker expires at this stack's next genuine queue activation";
}

TEST_F(NewHorizonsShroudNoEscapeTest, DefenderOnlyPerkDoesNotSlowAnAttackerWhoFlanksIt)
{
	startNoEscapeBattle(BattleSide::DEFENDER);
	ASSERT_TRUE(newHorizonsShroud::hasNoEscape(defenderSideHero));
	EXPECT_FALSE(newHorizonsShroud::hasNoEscape(attackerSideHero));
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 1000);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(82), 10);
	auto * reserve = addStack(BattleSide::ATTACKER, creatureByName("core:zombie"), BattleHex(4, 9), 1);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(reserve, nullptr);
	blockRetaliation(attacker);
	beginCombat();
	defendOnFirstQueueActivation(target);
	const auto attackerMovement = attacker->getMovementRange();
	ASSERT_TRUE(battle()->battleIsShroudFlankingAttack(BattleAttackInfo(attacker, target, 0, false)));
	ASSERT_TRUE(attack(attacker, target->getPosition()));
	EXPECT_TRUE(attacker->alive());
	EXPECT_TRUE(target->alive());
	EXPECT_EQ(attacker->getMovementRange(), attackerMovement);
	EXPECT_FALSE(hasNoEscapePenalty(attacker));
	EXPECT_FALSE(hasNoEscapePenalty(target))
		<< "No Escape belongs to the attacking hero; its victim's hero cannot apply it backward";
}

TEST_F(NewHorizonsShroudNoEscapeTest, TimedPenaltyHasNarrowCurrentRoundTripAndRejectsOlderWriters)
{
	const auto original = newHorizonsShroud::noEscapeSpeedPenalty();
	CMemorySerializer current;
	current.oser & original;
	Bonus restored;
	current.iser & restored;
	EXPECT_EQ(restored.duration, original.duration);
	EXPECT_EQ(restored.type, original.type);
	EXPECT_EQ(restored.val, original.val);
	EXPECT_EQ(restored.source, original.source);
	EXPECT_EQ(restored.sid, original.sid);
	EXPECT_EQ(restored.stacking, original.stacking);
	EXPECT_EQ(restored.description.toString(LIBRARY->staticTexts()),
		original.description.toString(LIBRARY->staticTexts()));
	EXPECT_TRUE(newHorizonsShroud::isNoEscapeSpeedPenalty(&restored));

	CMemorySerializer older;
	older.oser.version = ESerializationVersion::NEW_HORIZONS_RESERVE;
	try
	{
		older.oser & original;
		FAIL() << "The older writer cannot discard the activation-scoped penalty";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_EQ(std::string(error.what()), "Cannot discard New Horizons creature activation bonus duration");
	}
	EXPECT_TRUE(older.extractBuffer().empty());
}
