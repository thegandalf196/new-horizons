/*
 * NewHorizonsAmbusherTest.cpp, part of VCMI engine
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
class AmbusherEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit AmbusherEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsAmbusherTest : public BattleTestFixture
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
			if(perk["id"].String() != newHorizonsShroud::AMBUSHER_PERK_ID)
				continue;
			const auto status = perk["effect"]["status"].String();
			if(status == "active")
				return;
			if(status != "planned")
				throw std::runtime_error("Ambusher must be planned or active in canonical content");
			perk["effect"]["status"].String() = "active";
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(rules));
			return;
		}
		throw std::runtime_error("Missing Ambusher registry entry");
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

	void selectAmbusher(CGHeroInstance * hero)
	{
		EXPECT_FALSE(newHorizonsShroud::hasAmbusher(hero));
		hero->setSecSkillLevel(shroudSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(offerContains(hero, newHorizonsShroud::AMBUSHER_PERK_ID));
		acceptPerk(hero, newHorizonsShroud::AMBUSHER_PERK_ID);
		ASSERT_TRUE(newHorizonsShroud::hasAmbusher(hero));
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

	TConstBonusListPtr ambusherMarkers(const battle::Unit * unit) const
	{
		return unit->getAllBonuses(CSelector(newHorizonsShroud::isAmbusherSpentMarker));
	}

	std::size_t ambusherMarkerCount(const battle::Unit * unit) const
	{
		const auto bonuses = ambusherMarkers(unit);
		return bonuses ? bonuses->size() : 0;
	}

	bool hasAmbusherMarker(const battle::Unit * unit) const
	{
		return ambusherMarkerCount(unit) != 0;
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

	bool shoot(const CStack * shooter, const CStack * target)
	{
		battle()->activeStack = shooter->unitId();
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(shooter->unitSide()), BattleAction::makeShotAttack(shooter, target));
	}

	std::vector<int64_t> acceptedPrimaryDamageFrom(
		std::size_t beginIndex, const CStack * attacker, const CStack * target) const
	{
		std::vector<int64_t> result;
		for(auto it = server.attacks.begin() + static_cast<std::ptrdiff_t>(beginIndex); it != server.attacks.end(); ++it)
		{
			if(it->counter() || it->stackAttacking != attacker->unitId())
				continue;
			const auto hit = std::ranges::find(it->bsa, target->unitId(), &BattleStackAttacked::stackAttacked);
			if(hit != it->bsa.end())
				result.push_back(hit->damageAmount);
		}
		return result;
	}
};
}

TEST_F(NewHorizonsAmbusherTest, LegalBasicPerkSpendsOnFirstFlankPerStackAndProjectsToAI)
{
	startGameWithDungeonHero();
	selectAmbusher(attackerSideHero);
	const auto angel = creatureByName("core:angel");
	// A bounded extra ordinary strike lets this one action prove that the marker
	// suppresses the second flanking bonus before the next creature activation.
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::HERO_GRANTS_ATTACKS, BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(angel)));
	startBattle();
	removeStartingUnits();
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(81), 10000);
	auto * front = addStack(BattleSide::ATTACKER, angel, BattleHex(80), 10);
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 10);
	auto * firstFlanker = addStack(BattleSide::ATTACKER, angel, BattleHex(82), 10);
	auto * reserve = addStack(BattleSide::ATTACKER, creatureByName("core:zombie"), BattleHex(4, 9), 1);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(front, nullptr);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(firstFlanker, nullptr);
	ASSERT_NE(reserve, nullptr);
	std::vector<CStack *> flankers{firstFlanker};
	for(const auto & hex : target->getSurroundingHexes())
	{
		if(!hex.isAvailable() || battle()->battleGetStackByPos(hex))
			continue;
		auto * candidate = addStack(BattleSide::ATTACKER, angel, hex, 10);
		ASSERT_NE(candidate, nullptr);
		if(battle()->battleIsShroudFlankingAttack(BattleAttackInfo(candidate, target, 0, false)))
			flankers.push_back(candidate);
		if(flankers.size() == 2)
			break;
	}
	ASSERT_EQ(flankers.size(), 2u) << "The fixture needs two independent legal rear-facing stacks";
	auto * secondFlanker = flankers[1];
	for(auto * stack : {front, shooter, firstFlanker, secondFlanker})
		blockRetaliation(stack);
	beginCombat();
	advanceUntilActive(target);
	ASSERT_TRUE(act(target, BattleAction::makeDefend(target)));

	EXPECT_FALSE(battle()->battleIsShroudFlankingAttack(BattleAttackInfo(front, target, 0, false)));
	EXPECT_FALSE(battle()->battleIsShroudFlankingAttack(BattleAttackInfo(shooter, target, 0, true)));
	ASSERT_TRUE(battle()->battleIsShroudFlankingAttack(BattleAttackInfo(firstFlanker, target, 0, false)));
	ASSERT_TRUE(battle()->battleIsShroudFlankingAttack(BattleAttackInfo(secondFlanker, target, 0, false)));
	EXPECT_EQ(newHorizonsShroud::ambusherDamagePercent(attackerSideHero, firstFlanker),
		newHorizonsShroud::AMBUSHER_DAMAGE_PERCENT);
	EXPECT_EQ(newHorizonsShroud::ambusherDamagePercent(attackerSideHero, secondFlanker),
		newHorizonsShroud::AMBUSHER_DAMAGE_PERCENT);

	AmbusherEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	BattleAttackInfo candidateAttack(model->getForUpdate(firstFlanker->unitId()).get(),
		model->getForUpdate(target->unitId()).get(), 0, false);
	const auto preview = AttackPossibility::evaluate(candidateAttack, firstFlanker->getPosition(), cache, model);
	ASSERT_NE(preview.effectPreview, nullptr);
	auto projectedFirst = preview.effectPreview->getForUpdate(firstFlanker->unitId());
	ASSERT_NE(projectedFirst, nullptr);
	EXPECT_TRUE(hasAmbusherMarker(projectedFirst.get()));
	EXPECT_EQ(ambusherMarkerCount(projectedFirst.get()), 1u);
	EXPECT_EQ(newHorizonsShroud::ambusherDamagePercent(attackerSideHero, projectedFirst.get()), 0);
	EXPECT_FALSE(hasAmbusherMarker(model->getForUpdate(firstFlanker->unitId()).get()));
	EXPECT_FALSE(hasAmbusherMarker(firstFlanker));

	auto selected = std::make_shared<HypotheticBattle>(&environment, model);
	BattleExchangeVariant replay;
	replay.trackAttack(preview, selected, cache);
	EXPECT_TRUE(hasAmbusherMarker(selected->getForUpdate(firstFlanker->unitId()).get()));
	EXPECT_FALSE(hasAmbusherMarker(model->getForUpdate(firstFlanker->unitId()).get()));
	auto sibling = std::make_shared<HypotheticBattle>(&environment, model);
	EXPECT_FALSE(hasAmbusherMarker(sibling->getForUpdate(firstFlanker->unitId()).get()));

	ASSERT_TRUE(attack(front, target->getPosition()));
	EXPECT_FALSE(hasAmbusherMarker(front)) << "A front-side attack does not spend Ambusher";
	ASSERT_TRUE(shoot(shooter, target));
	EXPECT_FALSE(hasAmbusherMarker(shooter)) << "An ordinary ranged hit does not spend Ambusher";
	ASSERT_TRUE(target->alive());

	const auto firstAttackIndex = server.attacks.size();
	const BattleAttackInfo firstFlankAttack(firstFlanker, target, 0, false);
	const auto firstBonusRange = battle()->calculateDmgRange(firstFlankAttack).damage;
	ASSERT_GT(firstBonusRange.max, 0);
	ASSERT_TRUE(attack(firstFlanker, target->getPosition()));
	ASSERT_TRUE(target->alive());
	EXPECT_TRUE(hasAmbusherMarker(firstFlanker));
	EXPECT_EQ(ambusherMarkerCount(firstFlanker), 1u);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const auto & line)
	{
		return line.find("Ambusher grants ") != std::string::npos
			&& line.find("+20% damage on its first flanking attack this combat.") != std::string::npos;
	})) << ::testing::PrintToString(server.battleLogLines);
	EXPECT_EQ(newHorizonsShroud::ambusherDamagePercent(attackerSideHero, firstFlanker), 0);
	EXPECT_FALSE(hasAmbusherMarker(secondFlanker));
	EXPECT_EQ(newHorizonsShroud::ambusherDamagePercent(attackerSideHero, secondFlanker),
		newHorizonsShroud::AMBUSHER_DAMAGE_PERCENT)
		<< "Spending one stack's Ambusher leaves a different friendly stack's first flank available";
	auto firstStackHits = acceptedPrimaryDamageFrom(firstAttackIndex, firstFlanker, target);
	ASSERT_EQ(firstStackHits.size(), 2u) << "The fixture's accepted extra attack should produce two direct strikes";

	BattleAttackInfo spentFirstFlank(firstFlanker, target, 0, false);
	const auto spentRange = battle()->calculateDmgRange(spentFirstFlank).damage;
	EXPECT_GT(firstBonusRange.max, spentRange.max)
		<< "The accepted first strike gains Ambusher's 20% while its follow-up does not";
	EXPECT_GT(firstStackHits.front(), firstStackHits.back());

	const auto secondAttackIndex = server.attacks.size();
	const BattleAttackInfo secondFirstFlank(secondFlanker, target, 0, false);
	const auto secondBonusRange = battle()->calculateDmgRange(secondFirstFlank).damage;
	ASSERT_GT(secondBonusRange.max, 0);
	ASSERT_TRUE(attack(secondFlanker, target->getPosition()));
	EXPECT_TRUE(hasAmbusherMarker(secondFlanker));
	EXPECT_EQ(ambusherMarkerCount(secondFlanker), 1u);
	EXPECT_EQ(newHorizonsShroud::ambusherDamagePercent(attackerSideHero, secondFlanker), 0);
	auto secondStackHits = acceptedPrimaryDamageFrom(secondAttackIndex, secondFlanker, target);
	ASSERT_EQ(secondStackHits.size(), 2u);
	EXPECT_GT(secondStackHits.front(), secondStackHits.back());
	EXPECT_GT(secondBonusRange.max, battle()->calculateDmgRange(
		BattleAttackInfo(secondFlanker, target, 0, false)).damage.max);

	const auto markerBonuses = ambusherMarkers(firstFlanker);
	ASSERT_NE(markerBonuses, nullptr);
	ASSERT_EQ(markerBonuses->size(), 1u);
	const Bonus realMarker = *markerBonuses->front();
	CMemorySerializer current;
	current.oser & realMarker;
	Bonus restoredMarker;
	current.iser & restoredMarker;
	EXPECT_TRUE(newHorizonsShroud::isAmbusherSpentMarker(&restoredMarker));
	EXPECT_EQ(restoredMarker.duration, realMarker.duration);
	EXPECT_EQ(restoredMarker.type, realMarker.type);
	EXPECT_EQ(restoredMarker.source, realMarker.source);
	EXPECT_EQ(restoredMarker.sid, realMarker.sid);
	EXPECT_EQ(restoredMarker.stacking, realMarker.stacking);
	EXPECT_TRUE(target->alive()) << "A slow reserve and large target keep the combat observation live";
}
