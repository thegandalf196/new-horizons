/*
 * NewHorizonsEvasiveShroudTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
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
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>

namespace
{
class EvasiveShroudEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit EvasiveShroudEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsEvasiveShroudTest : public BattleTestFixture
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
			if(perk["id"].String() != newHorizonsShroud::EVASIVE_SHROUD_PERK_ID)
				continue;
			const auto status = perk["effect"]["status"].String();
			if(status == "active")
				return;
			if(status != "planned")
				throw std::runtime_error("Evasive Shroud must be planned or active in canonical content");
			perk["effect"]["status"].String() = "active";
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(rules));
			return;
		}
		throw std::runtime_error("Missing Evasive Shroud registry entry");
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

	void startGameWithDungeonHero()
	{
		const auto dungeonHero = heroTypeForFaction(FactionID::DUNGEON);
		const auto otherHero = heroTypeForFaction(FactionID::CASTLE);
		const CreatureID token(0);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.hero({5, 5, 0}, dungeonHero, PlayerColor(0)).heroGarrison({{token, 1}})
			.hero({7, 7, 0}, otherHero, PlayerColor(1)).heroGarrison({{token, 1}});
		startWithMap(std::move(builder));

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_EQ(attackerSideHero->getHeroClass()->faction, FactionID::DUNGEON);
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
		const auto rankLookup = [hero](const std::string & skill) { return hero->getPerkSkillRank(skill); };
		for(uint64_t offerSeed = 0; offerSeed < 4096; ++offerSeed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, offerSeed);
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
		const auto rankLookup = [hero](const std::string & skill) { return hero->getPerkSkillRank(skill); };
		for(uint64_t offerSeed = 0; offerSeed < 4096; ++offerSeed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, offerSeed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
			{
				if(offers[choice].selection.skillId == newHorizonsShroud::SKILL_ID
					&& offers[choice].selection.perkId == perkId)
				{
					gameHandler->levelUpHero(hero, offers, choice, offerSeed, false);
					ASSERT_TRUE(hero->hasActivePerk(std::string(newHorizonsShroud::SKILL_ID), std::string(perkId)));
					return;
				}
			}
		}
		FAIL() << "No legal Shroud of Malassa offer for " << perkId;
	}

	void selectEvasiveShroud(CGHeroInstance * hero)
	{
		hero->setSecSkillLevel(shroudSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		EXPECT_FALSE(offerContains(hero, newHorizonsShroud::EVASIVE_SHROUD_PERK_ID))
			<< "Evasive Shroud is an Advanced Shroud perk";
		ASSERT_TRUE(offerContains(hero, newHorizonsShroud::BACKSTAB_PERK_ID));
		acceptPerk(hero, newHorizonsShroud::BACKSTAB_PERK_ID);
		hero->setSecSkillLevel(shroudSkill(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(offerContains(hero, newHorizonsShroud::EVASIVE_SHROUD_PERK_ID));
		acceptPerk(hero, newHorizonsShroud::EVASIVE_SHROUD_PERK_ID);
		ASSERT_TRUE(newHorizonsShroud::hasEvasiveShroud(hero));
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

	TConstBonusListPtr evasiveBonuses(const battle::Unit * unit) const
	{
		return unit->getAllBonuses(CSelector(newHorizonsShroud::isEvasiveShroudProtection));
	}

	bool hasEvasiveProtection(const battle::Unit * unit) const
	{
		const auto bonuses = evasiveBonuses(unit);
		return bonuses && !bonuses->empty();
	}

	std::size_t evasiveProtectionCount(const battle::Unit * unit) const
	{
		const auto bonuses = evasiveBonuses(unit);
		return bonuses ? bonuses->size() : 0;
	}

	bool act(const battle::Unit * stack, const BattleAction & action)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(stack->unitSide()), action);
	}

	void advanceUntilActive(const CStack * expected)
	{
		for(int attempt = 0; attempt < 48; ++attempt)
		{
			if(battle()->battleActiveUnit() == expected)
				return;
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			ASSERT_TRUE(act(active, BattleAction::makeDefend(active)));
		}
		FAIL() << "The expected stack did not receive an ordinary activation";
	}

	std::size_t queueActivations(const CStack * stack) const
	{
		return static_cast<std::size_t>(std::count_if(server.stackActivations.begin(), server.stackActivations.end(),
			[stack](const BattleSetActiveStack & activation)
			{
				return activation.stack == stack->unitId()
					&& activation.reason == BattleUnitTurnReason::TURN_QUEUE;
			}));
	}

	bool shoot(const CStack * shooter, const CStack * target)
	{
		battle()->activeStack = shooter->unitId();
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(shooter->unitSide()), BattleAction::makeShotAttack(shooter, target));
	}
};
}

TEST_F(NewHorizonsEvasiveShroudTest, LegalAdvancedFlankProtectsAttackerBeforeRetaliationAndProjectsToAI)
{
	startGameWithDungeonHero();
	selectEvasiveShroud(attackerSideHero);
	startBattle();
	removeStartingUnits();
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(81), 3000);
	auto * front = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 1);
	auto * flanker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(82), 20);
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 1);
	auto * reserve = addStack(BattleSide::ATTACKER, creatureByName("core:zombie"), BattleHex(4, 9), 1);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(front, nullptr);
	ASSERT_NE(flanker, nullptr);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(reserve, nullptr);
	blockRetaliation(front);
	blockRetaliation(shooter);
	beginCombat();
	advanceUntilActive(target);
	ASSERT_TRUE(act(target, BattleAction::makeDefend(target)));

	const BattleAttackInfo frontAttack(front, target, 0, false);
	const BattleAttackInfo rangedAttack(shooter, target, 0, true);
	const BattleAttackInfo flankAttack(flanker, target, 0, false);
	EXPECT_FALSE(battle()->battleIsShroudFlankingAttack(frontAttack));
	EXPECT_FALSE(battle()->battleIsShroudFlankingAttack(rangedAttack));
	ASSERT_TRUE(battle()->battleIsShroudFlankingAttack(flankAttack));

	EvasiveShroudEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto projectedAttack = BattleAttackInfo(model->getForUpdate(flanker->unitId()).get(),
		model->getForUpdate(target->unitId()).get(), 0, false);
	const auto preview = AttackPossibility::evaluate(projectedAttack, flanker->getPosition(), cache, model);
	ASSERT_NE(preview.effectPreview, nullptr);
	const auto projectedFlanker = preview.effectPreview->getForUpdate(flanker->unitId());
	ASSERT_NE(projectedFlanker, nullptr);
	EXPECT_TRUE(hasEvasiveProtection(projectedFlanker.get()));
	EXPECT_EQ(evasiveProtectionCount(projectedFlanker.get()), 1u);
	EXPECT_FALSE(hasEvasiveProtection(model->getForUpdate(flanker->unitId()).get()));
	EXPECT_FALSE(hasEvasiveProtection(flanker));

	auto selected = std::make_shared<HypotheticBattle>(&environment, model);
	BattleExchangeVariant replay;
	replay.trackAttack(preview, selected, cache);
	EXPECT_TRUE(hasEvasiveProtection(selected->getForUpdate(flanker->unitId()).get()));
	EXPECT_FALSE(hasEvasiveProtection(model->getForUpdate(flanker->unitId()).get()));

	ASSERT_TRUE(attack(front, target->getPosition()));
	EXPECT_FALSE(hasEvasiveProtection(front));
	ASSERT_TRUE(shoot(shooter, target));
	EXPECT_FALSE(hasEvasiveProtection(shooter));
	ASSERT_TRUE(target->alive());
	const auto primaryFlankRange = battle()->calculateDmgRange(BattleAttackInfo(flanker, target, 0, false)).damage;
	ASSERT_GT(primaryFlankRange.max, 0);
	ASSERT_LT(primaryFlankRange.max, target->getAvailableHealth())
		<< "The target's remaining health must exceed the maximum primary flank damage";
	BattleAttackInfo retaliationEstimate(target, flanker, 0, false);
	retaliationEstimate.retaliation = true;
	const auto unprotectedRetaliationRange = battle()->calculateDmgRange(retaliationEstimate).damage;
	ASSERT_LT(unprotectedRetaliationRange.max, flanker->getAvailableHealth())
		<< "Even an unprotected retaliation must leave the flanker alive for the reduction check";
	ASSERT_TRUE(attack(flanker, target->getPosition()));
	ASSERT_TRUE(flanker->alive());
	ASSERT_TRUE(target->alive());
	EXPECT_EQ(evasiveProtectionCount(flanker), 1u);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const auto & line)
	{
		return line.find("Evasive Shroud grants") != std::string::npos;
	})) << "The actual hit reports the acquired protection in combat feedback";

	const auto retaliation = std::ranges::find_if(server.attacks, [target](const BattleAttack & entry)
	{
		return entry.counter() && entry.stackAttacking == target->unitId();
	});
	ASSERT_NE(retaliation, server.attacks.end()) << "The target should make its ordinary retaliation after the flank";
	const auto hit = std::ranges::find(retaliation->bsa, flanker->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(hit, retaliation->bsa.end());
	EXPECT_GT(hit->damageAmount, 0);

	EvasiveShroudEnvironment postEnvironment(gameState());
	auto postCallback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle unprotected(&postEnvironment, postCallback);
	auto unprotectedFlanker = unprotected.getForUpdate(flanker->unitId());
	unprotectedFlanker->removeUnitBonus(CSelector(newHorizonsShroud::isEvasiveShroudProtection));
	BattleAttackInfo unprotectedRetaliation(unprotected.getForUpdate(target->unitId()).get(),
		unprotectedFlanker.get(), 0, false);
	unprotectedRetaliation.retaliation = true;
	const auto unprotectedRange = unprotected.calculateDmgRange(unprotectedRetaliation).damage;
	BattleAttackInfo protectedRetaliation(target, flanker, 0, false);
	protectedRetaliation.retaliation = true;
	const auto protectedRange = battle()->calculateDmgRange(protectedRetaliation).damage;
	EXPECT_LT(protectedRange.max, unprotectedRange.max)
		<< "The live marker lowers the target's physical damage against its protected attacker";
	EXPECT_LE(hit->damageAmount, protectedRange.max);
}

TEST_F(NewHorizonsEvasiveShroudTest, RepeatedAcceptedStrikesRefreshOneMarkerUntilItsNextRealActivation)
{
	startGameWithDungeonHero();
	selectEvasiveShroud(attackerSideHero);
	const auto creature = creatureByName("core:angel");
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::HERO_GRANTS_ATTACKS, BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(creature)));
	startBattle();
	removeStartingUnits();
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(81), 5000);
	auto * flanker = addStack(BattleSide::ATTACKER, creature, BattleHex(82), 20);
	auto * reserve = addStack(BattleSide::ATTACKER, creatureByName("core:zombie"), BattleHex(4, 9), 1);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(flanker, nullptr);
	ASSERT_NE(reserve, nullptr);
	ASSERT_EQ(attackerSideHero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS,
		BonusSubtypeID(creature)), 1);
	beginCombat();
	advanceUntilActive(flanker);
	const auto priorQueueActivations = queueActivations(flanker);
	ASSERT_GT(priorQueueActivations, 0u);
	ASSERT_TRUE(attack(flanker, target->getPosition()));
	ASSERT_TRUE(flanker->alive());
	ASSERT_TRUE(target->alive());
	EXPECT_EQ(evasiveProtectionCount(flanker), 1u)
		<< "A later accepted strike in the same attack sequence refreshes instead of stacking";
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const auto & line)
	{
		return line.find("Evasive Shroud refreshes") != std::string::npos;
	}));

	const auto firstRound = battle()->getRound();
	endRound();
	ASSERT_EQ(battle()->getRound(), firstRound + 1);
	advanceUntilActive(flanker);
	EXPECT_GT(queueActivations(flanker), priorQueueActivations);
	EXPECT_FALSE(hasEvasiveProtection(flanker))
		<< "The protection remains through the round and expires at the holder's next real queue activation";
}

TEST_F(NewHorizonsEvasiveShroudTest, TimedProtectionUsesOnlyTheAdvancedBonusAndRejectsOlderWriters)
{
	const auto original = newHorizonsShroud::evasiveShroudProtection();
	EXPECT_EQ(original.duration, BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION);
	EXPECT_EQ(original.type, BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS);
	EXPECT_EQ(original.val, newHorizonsShroud::EVASIVE_SHROUD_REDUCTION_BASIS_POINTS);
	EXPECT_EQ(original.val, 1500);
	EXPECT_EQ(original.source, BonusSource::SECONDARY_SKILL);
	EXPECT_EQ(original.stacking, newHorizonsShroud::EVASIVE_SHROUD_STACKING_KEY);
	EXPECT_TRUE(newHorizonsShroud::isEvasiveShroudProtection(&original));

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
	EXPECT_TRUE(newHorizonsShroud::isEvasiveShroudProtection(&restored));

	CMemorySerializer older;
	older.oser.version = ESerializationVersion::NEW_HORIZONS_RESERVE;
	try
	{
		older.oser & original;
		FAIL() << "The older writer cannot discard the activation-scoped protection";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_EQ(std::string(error.what()), "Cannot discard New Horizons creature activation bonus duration");
	}
	EXPECT_TRUE(older.extractBuffer().empty());
}
