/*
 * NewHorizonsShadowAssaultTest.cpp, part of VCMI engine
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
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <cstddef>

namespace
{
class ShadowAssaultEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ShadowAssaultEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsShadowAssaultTest : public BattleTestFixture
{
protected:
	struct AdjacentAttackers
	{
		CStack * front = nullptr;
		std::vector<CStack *> flankers;
	};

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
			if(perk["id"].String() != newHorizonsShroud::SHADOW_ASSAULT_PERK_ID)
				continue;
			const auto status = perk["effect"]["status"].String();
			if(status == "active")
				return;
			if(status != "planned")
				throw std::runtime_error("Shadow Assault must be planned or active in canonical content");
			perk["effect"]["status"].String() = "active";
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(rules));
			return;
		}
		throw std::runtime_error("Missing Shadow Assault registry entry");
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

	HeroTypeID otherHeroTypeForFaction(FactionID faction, const HeroTypeID & excluded) const
	{
		for(const auto & id : LIBRARY->heroh->getDefaultAllowed())
		{
			const auto * hero = dynamic_cast<const CHero *>(id.toHeroType());
			if(id != excluded && hero && hero->heroClass && hero->heroClass->faction == faction)
				return id;
		}
		throw std::runtime_error("No distinct default-allowed hero found for the requested fixture faction");
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

	void startGameWithDungeonHeroes()
	{
		const auto dungeonHero = heroTypeForFaction(FactionID::DUNGEON);
		const auto secondDungeonHero = otherHeroTypeForFaction(FactionID::DUNGEON, dungeonHero);
		const CreatureID token(0);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.hero({5, 5, 0}, dungeonHero, PlayerColor(0)).heroGarrison({{token, 1}})
			.hero({7, 7, 0}, secondDungeonHero, PlayerColor(1)).heroGarrison({{token, 1}});
		startWithMap(std::move(builder));

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_EQ(attackerSideHero->getHeroClass()->faction, FactionID::DUNGEON);
		ASSERT_EQ(defenderSideHero->getHeroClass()->faction, FactionID::DUNGEON);
		ASSERT_NE(attackerSideHero->getHeroTypeID(), defenderSideHero->getHeroTypeID());
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

	void selectShadowAssault(CGHeroInstance * hero)
	{
		EXPECT_FALSE(newHorizonsShroud::hasShadowAssault(hero));
		hero->setSecSkillLevel(shroudSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(offerContains(hero, newHorizonsShroud::SHADOW_ASSAULT_PERK_ID));
		acceptPerk(hero, newHorizonsShroud::SHADOW_ASSAULT_PERK_ID);
		ASSERT_TRUE(newHorizonsShroud::hasShadowAssault(hero));
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

	AdjacentAttackers addAdjacentAttackers(const CStack * target, BattleSide side, std::size_t neededFlankers)
	{
		AdjacentAttackers result;
		for(const auto & hex : target->getSurroundingHexes())
		{
			if(!hex.isAvailable() || battle()->battleGetStackByPos(hex))
				continue;
			auto * candidate = addStack(side, creatureByName("core:angel"), hex, 20);
			if(!candidate)
			{
				ADD_FAILURE() << "Could not add a legal adjacent fixture stack at " << hex;
				return result;
			}
			blockRetaliation(candidate);
			if(battle()->battleIsShroudFlankingAttack(BattleAttackInfo(candidate, target, 0, false)))
				result.flankers.push_back(candidate);
			else if(!result.front)
				result.front = candidate;
			if(result.flankers.size() >= neededFlankers && (neededFlankers == 1 || result.front))
				break;
		}
		return result;
	}

	TConstBonusListPtr shadowMarkers(const battle::Unit * unit, BattleSide side) const
	{
		return unit->getAllBonuses(CSelector([side](const Bonus * bonus)
		{
			return newHorizonsShroud::isShadowAssaultSpentMarker(bonus, side);
		}));
	}

	std::size_t markerCount(const battle::Unit * unit, BattleSide side) const
	{
		const auto bonuses = shadowMarkers(unit, side);
		return bonuses ? bonuses->size() : 0;
	}

	bool hasMarker(const battle::Unit * unit, BattleSide side) const
	{
		return markerCount(unit, side) != 0;
	}

	bool shoot(const CStack * shooter, const CStack * target)
	{
		battle()->activeStack = shooter->unitId();
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(shooter->unitSide()), BattleAction::makeShotAttack(shooter, target));
	}

	std::size_t shadowLogCount() const
	{
		return static_cast<std::size_t>(std::ranges::count_if(server.battleLogLines, [](const auto & line)
		{
			return line.find("Shadow Assault ignores 25% of") != std::string::npos;
		}));
	}

	void expectMarkerRoundTrip(const CStack * target, BattleSide side)
	{
		const auto bonuses = shadowMarkers(target, side);
		ASSERT_NE(bonuses, nullptr);
		ASSERT_EQ(bonuses->size(), 1u);
		const Bonus realMarker = *bonuses->front();
		CMemorySerializer current;
		current.oser & realMarker;
		Bonus restoredMarker;
		current.iser & restoredMarker;
		EXPECT_TRUE(newHorizonsShroud::isShadowAssaultSpentMarker(&restoredMarker, side));
		EXPECT_EQ(restoredMarker.duration, realMarker.duration);
		EXPECT_EQ(restoredMarker.type, realMarker.type);
		EXPECT_EQ(restoredMarker.val, realMarker.val);
		EXPECT_EQ(restoredMarker.source, realMarker.source);
		EXPECT_EQ(restoredMarker.sid, realMarker.sid);
		EXPECT_EQ(restoredMarker.stacking, realMarker.stacking);
		EXPECT_EQ(restoredMarker.hidden, realMarker.hidden);
	}
};
}

TEST_F(NewHorizonsShadowAssaultTest, LegalPerkIgnoresDefenseOncePerTargetAndAttackingSide)
{
	startGameWithDungeonHeroes();
	selectShadowAssault(attackerSideHero);
	selectShadowAssault(defenderSideHero);
	startBattle();
	removeStartingUnits();
	const auto angel = creatureByName("core:angel");
	const auto archer = creatureByName("core:archer");
	auto * attackerTarget = addStack(BattleSide::ATTACKER, angel, BattleHex(3, 5), 1000);
	auto * defenderTarget = addStack(BattleSide::DEFENDER, angel, BattleHex(13, 3), 1000);
	auto * freshDefenderTarget = addStack(BattleSide::DEFENDER, angel, BattleHex(13, 8), 1000);
	ASSERT_NE(attackerTarget, nullptr);
	ASSERT_NE(defenderTarget, nullptr);
	ASSERT_NE(freshDefenderTarget, nullptr);
	const auto attackerTargetDefense = attackerTarget->getDefense(false);
	const auto defenderTargetDefense = defenderTarget->getDefense(false);
	const auto freshTargetDefense = freshDefenderTarget->getDefense(false);

	auto attackerApproach = addAdjacentAttackers(defenderTarget, BattleSide::ATTACKER, 2);
	ASSERT_NE(attackerApproach.front, nullptr) << "The front-side negative control must be adjacent";
	ASSERT_EQ(attackerApproach.flankers.size(), 2u) << "Need two attackers against one target";
	auto freshApproach = addAdjacentAttackers(freshDefenderTarget, BattleSide::ATTACKER, 1);
	ASSERT_EQ(freshApproach.flankers.size(), 1u) << "A second target must have an independent first flank";
	auto defenderApproach = addAdjacentAttackers(attackerTarget, BattleSide::DEFENDER, 1);
	ASSERT_EQ(defenderApproach.flankers.size(), 1u) << "The other attacking side needs a legal flank";
	const auto shooterHex = BattleHex(8, 0);
	auto * shooter = addStack(BattleSide::ATTACKER, archer, shooterHex, 20);
	auto * reserve = addStack(BattleSide::ATTACKER, creatureByName("core:zombie"), BattleHex(8, 10), 1);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(reserve, nullptr);
	blockRetaliation(shooter);
	beginCombat();

	auto * firstAttackerFlanker = attackerApproach.flankers[0];
	auto * secondAttackerFlanker = attackerApproach.flankers[1];
	auto * freshTargetFlanker = freshApproach.flankers[0];
	auto * defenderSideFlanker = defenderApproach.flankers[0];
	EXPECT_FALSE(battle()->battleIsShroudFlankingAttack(BattleAttackInfo(
		attackerApproach.front, defenderTarget, 0, false)));
	EXPECT_FALSE(battle()->battleIsShroudFlankingAttack(BattleAttackInfo(shooter, defenderTarget, 0, true)));
	ASSERT_TRUE(battle()->battleIsShroudFlankingAttack(BattleAttackInfo(
		firstAttackerFlanker, defenderTarget, 0, false)));
	ASSERT_TRUE(battle()->battleIsShroudFlankingAttack(BattleAttackInfo(
		defenderSideFlanker, attackerTarget, 0, false)));

	EXPECT_EQ(newHorizonsShroud::shadowAssaultDefenseIgnorePercent(
		attackerSideHero, defenderTarget, BattleSide::ATTACKER), newHorizonsShroud::SHADOW_ASSAULT_DEFENSE_IGNORE_PERCENT);
	EXPECT_EQ(newHorizonsShroud::shadowAssaultDefenseIgnorePercent(
		defenderSideHero, attackerTarget, BattleSide::DEFENDER), newHorizonsShroud::SHADOW_ASSAULT_DEFENSE_IGNORE_PERCENT);
	EXPECT_FALSE(hasMarker(defenderTarget, BattleSide::ATTACKER));
	EXPECT_FALSE(hasMarker(defenderTarget, BattleSide::DEFENDER));

	ShadowAssaultEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	battle()->activeStack = firstAttackerFlanker->unitId();
	BattleAttackInfo candidateAttack(model->getForUpdate(firstAttackerFlanker->unitId()).get(),
		model->getForUpdate(defenderTarget->unitId()).get(), 0, false);
	const auto preview = AttackPossibility::evaluate(
		candidateAttack, firstAttackerFlanker->getPosition(), cache, model);
	ASSERT_NE(preview.effectPreview, nullptr);
	ASSERT_FALSE(preview.defenderDead);
	auto projectedTarget = preview.effectPreview->getForUpdate(defenderTarget->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	EXPECT_TRUE(hasMarker(projectedTarget.get(), BattleSide::ATTACKER));
	EXPECT_FALSE(hasMarker(projectedTarget.get(), BattleSide::DEFENDER));
	EXPECT_EQ(newHorizonsShroud::shadowAssaultDefenseIgnorePercent(
		attackerSideHero, projectedTarget.get(), BattleSide::ATTACKER), 0);
	EXPECT_FALSE(hasMarker(model->getForUpdate(defenderTarget->unitId()).get(), BattleSide::ATTACKER));
	EXPECT_FALSE(hasMarker(defenderTarget, BattleSide::ATTACKER));
	const auto aiFirstRange = model->calculateDmgRange(candidateAttack).damage;
	BattleAttackInfo projectedRepeat(
		preview.effectPreview->getForUpdate(firstAttackerFlanker->unitId()).get(),
		projectedTarget.get(), 0, false);
	const auto aiRepeatRange = preview.effectPreview->calculateDmgRange(projectedRepeat).damage;
	EXPECT_GT(aiFirstRange.max, aiRepeatRange.max)
		<< "The AI preview spends the target-side allowance after the first projected flank";

	auto selected = std::make_shared<HypotheticBattle>(&environment, model);
	BattleExchangeVariant replay;
	replay.trackAttack(preview, selected, cache);
	EXPECT_TRUE(hasMarker(selected->getForUpdate(defenderTarget->unitId()).get(), BattleSide::ATTACKER));
	EXPECT_FALSE(hasMarker(model->getForUpdate(defenderTarget->unitId()).get(), BattleSide::ATTACKER));
	auto sibling = std::make_shared<HypotheticBattle>(&environment, model);
	EXPECT_FALSE(hasMarker(sibling->getForUpdate(defenderTarget->unitId()).get(), BattleSide::ATTACKER));

	ASSERT_TRUE(attack(attackerApproach.front, defenderTarget->getPosition()));
	ASSERT_TRUE(shoot(shooter, defenderTarget));
	EXPECT_FALSE(hasMarker(defenderTarget, BattleSide::ATTACKER))
		<< "Front-side and ordinary ranged attacks leave the target's first-flank allowance available";
	EXPECT_EQ(newHorizonsShroud::shadowAssaultDefenseIgnorePercent(
		attackerSideHero, defenderTarget, BattleSide::ATTACKER), newHorizonsShroud::SHADOW_ASSAULT_DEFENSE_IGNORE_PERCENT);
	const auto logsBeforeFirst = shadowLogCount();
	const auto firstRange = battle()->calculateDmgRange(
		BattleAttackInfo(firstAttackerFlanker, defenderTarget, 0, false)).damage;
	ASSERT_GT(firstRange.max, 0);
	ASSERT_TRUE(attack(firstAttackerFlanker, defenderTarget->getPosition()));
	ASSERT_TRUE(defenderTarget->alive());
	EXPECT_TRUE(hasMarker(defenderTarget, BattleSide::ATTACKER));
	EXPECT_FALSE(hasMarker(defenderTarget, BattleSide::DEFENDER));
	EXPECT_EQ(markerCount(defenderTarget, BattleSide::ATTACKER), 1u);
	EXPECT_EQ(newHorizonsShroud::shadowAssaultDefenseIgnorePercent(
		attackerSideHero, defenderTarget, BattleSide::ATTACKER), 0);
	EXPECT_EQ(defenderTarget->getDefense(false), defenderTargetDefense)
		<< "Shadow Assault changes the attack's ignored Creature Defense, not the unit's own Defense";
	EXPECT_EQ(shadowLogCount(), logsBeforeFirst + 1u);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const auto & line)
	{
		return line.find("Shadow Assault ignores 25% of") != std::string::npos
			&& line.find("Creature Defense on the first flanking attack this combat.") != std::string::npos;
	})) << ::testing::PrintToString(server.battleLogLines);
	const auto repeatRange = battle()->calculateDmgRange(
		BattleAttackInfo(secondAttackerFlanker, defenderTarget, 0, false)).damage;
	EXPECT_LT(repeatRange.max, firstRange.max)
		<< "A second friendly attacker gets no second first-flank bonus against the marked target";
	const auto logsBeforeRepeat = shadowLogCount();
	ASSERT_TRUE(attack(secondAttackerFlanker, defenderTarget->getPosition()));
	EXPECT_EQ(markerCount(defenderTarget, BattleSide::ATTACKER), 1u);
	EXPECT_EQ(shadowLogCount(), logsBeforeRepeat);

	EXPECT_EQ(newHorizonsShroud::shadowAssaultDefenseIgnorePercent(
		attackerSideHero, freshDefenderTarget, BattleSide::ATTACKER), newHorizonsShroud::SHADOW_ASSAULT_DEFENSE_IGNORE_PERCENT);
	const auto freshRange = battle()->calculateDmgRange(
		BattleAttackInfo(freshTargetFlanker, freshDefenderTarget, 0, false)).damage;
	ASSERT_GT(freshRange.max, 0);
	ASSERT_TRUE(attack(freshTargetFlanker, freshDefenderTarget->getPosition()));
	EXPECT_TRUE(hasMarker(freshDefenderTarget, BattleSide::ATTACKER))
		<< "A different enemy target retains its own first-flank allowance";
	EXPECT_EQ(newHorizonsShroud::shadowAssaultDefenseIgnorePercent(
		attackerSideHero, freshDefenderTarget, BattleSide::ATTACKER), 0);
	EXPECT_EQ(freshDefenderTarget->getDefense(false), freshTargetDefense);

	EXPECT_EQ(newHorizonsShroud::shadowAssaultDefenseIgnorePercent(
		defenderSideHero, attackerTarget, BattleSide::DEFENDER), newHorizonsShroud::SHADOW_ASSAULT_DEFENSE_IGNORE_PERCENT);
	const auto opposingSideRange = battle()->calculateDmgRange(
		BattleAttackInfo(defenderSideFlanker, attackerTarget, 0, false)).damage;
	ASSERT_GT(opposingSideRange.max, 0);
	ASSERT_TRUE(attack(defenderSideFlanker, attackerTarget->getPosition()));
	EXPECT_TRUE(hasMarker(attackerTarget, BattleSide::DEFENDER));
	EXPECT_FALSE(hasMarker(attackerTarget, BattleSide::ATTACKER));
	EXPECT_EQ(markerCount(attackerTarget, BattleSide::DEFENDER), 1u);
	EXPECT_EQ(newHorizonsShroud::shadowAssaultDefenseIgnorePercent(
		defenderSideHero, attackerTarget, BattleSide::DEFENDER), 0);
	EXPECT_EQ(attackerTarget->getDefense(false), attackerTargetDefense);
	EXPECT_EQ(shadowLogCount(), logsBeforeFirst + 3u)
		<< "Each new (attacking side, target) pair spends exactly once";

	const auto attackerMarker = newHorizonsShroud::shadowAssaultSpentMarker(BattleSide::ATTACKER);
	const auto defenderMarker = newHorizonsShroud::shadowAssaultSpentMarker(BattleSide::DEFENDER);
	EXPECT_NE(attackerMarker.stacking, defenderMarker.stacking)
		<< "The saved marker identity includes the attacking side";
	expectMarkerRoundTrip(defenderTarget, BattleSide::ATTACKER);
	expectMarkerRoundTrip(attackerTarget, BattleSide::DEFENDER);
	EXPECT_TRUE(attackerTarget->alive());
	EXPECT_TRUE(defenderTarget->alive());
	EXPECT_TRUE(freshDefenderTarget->alive());
}
