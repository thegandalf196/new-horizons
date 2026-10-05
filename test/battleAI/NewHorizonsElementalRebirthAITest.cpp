/*
 * NewHorizonsElementalRebirthAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CStack.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsElementalRebirth.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/Problem.h"

#include <algorithm>
#include <memory>

namespace
{
class ElementalRebirthEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit ElementalRebirthEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class ElementalRebirthCallback final : public CBattleCallback
{
public:
	explicit ElementalRebirthCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
};

HeroTypeID heroType(const char * identifier)
{
	const int decoded = HeroTypeID::decode(identifier);
	if(decoded < 0)
		throw std::runtime_error(std::string("Missing hero in Elemental Rebirth fixture: ") + identifier);
	return HeroTypeID(decoded);
}

CreatureID creature(const char * identifier)
{
	const int decoded = CreatureID::decode(identifier);
	if(decoded < 0)
		throw std::runtime_error(std::string("Missing creature in Elemental Rebirth fixture: ") + identifier);
	return CreatureID(decoded);
}

const battle::Unit * unitAt(const CBattleInfoCallback & battle, BattleSide side, SlotID slot)
{
	for(const auto * unit : battle.battleGetAllUnits(false))
		if(unit && unit->unitSide() == side && unit->unitSlot() == slot)
			return unit;
	return nullptr;
}

std::vector<const battle::Unit *> rebirthSpawns(const HypotheticBattle & battle)
{
	std::vector<const battle::Unit *> result;
	for(const auto unitId : battle.getElementalRebirthSpawnUnitIds())
		if(const auto * unit = battle.battleGetUnitByID(unitId))
			result.push_back(unit);
	return result;
}
}

class NewHorizonsElementalRebirthAITest : public HeroCommandFixture
{
protected:
	static constexpr auto REBIRTH_SKILL = "new-horizons:elementalRebirth";
	CGHeroInstance * confluxHero = nullptr;
	const battle::Unit * source = nullptr;
	const battle::Unit * survivingAlly = nullptr;
	const battle::Unit * opposingPikemen = nullptr;
	std::shared_ptr<ElementalRebirthEnvironment> environment;
	std::shared_ptr<ElementalRebirthCallback> callback;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the separate New Horizons native profile";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare()
	{
		startGame();
		attackerSideHero->clearSlots();
		defenderSideHero->clearSlots();
		defenderSideHero->setHeroType(heroType("core:brissa"));
		confluxHero = defenderSideHero;
		ASSERT_EQ(confluxHero->getFactionID(), FactionID::CONFLUX);

		const int rebirthSkillId = SecondarySkill::decode(REBIRTH_SKILL);
		ASSERT_GE(rebirthSkillId, 0);
		confluxHero->setSecSkillLevel(SecondarySkill(rebirthSkillId), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(confluxHero->usesNewHorizonsMovement());
		const auto sourceProfile = newHorizonsElementalRebirth::activeProfile(confluxHero);
		ASSERT_TRUE(sourceProfile.has_value());
		ASSERT_EQ(sourceProfile->rank, 1);

		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creature("core:pikeman"), 100));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creature("core:peasant"), 1));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), creature("core:peasant"), 1000));
		giveArtifact(confluxHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		confluxHero->removeAllSpells();
		confluxHero->addSpellToSpellbook(SpellID(SpellID::FIREBALL));
		confluxHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1, ChangeValueMode::ABSOLUTE);
		confluxHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(confluxHero, 100);

		startBattle();
		source = unitAt(*battle(), BattleSide::DEFENDER, SlotID(0));
		survivingAlly = unitAt(*battle(), BattleSide::DEFENDER, SlotID(1));
		opposingPikemen = unitAt(*battle(), BattleSide::ATTACKER, SlotID(0));
		ASSERT_NE(source, nullptr);
		ASSERT_NE(survivingAlly, nullptr);
		ASSERT_NE(opposingPikemen, nullptr);
		ASSERT_GT(source->getBattleStartMaximumAggregateHP(), 0);
		beginCombat();

		callback = std::make_shared<ElementalRebirthCallback>(battle()->getSidePlayer(BattleSide::DEFENDER));
		callback->onBattleStarted(battle());
		ASSERT_EQ(callback->getBattle(BattleID(0))->battleGetFightingHero(BattleSide::DEFENDER), confluxHero);
		environment = std::make_shared<ElementalRebirthEnvironment>(gameState());
	}
};

TEST_F(NewHorizonsElementalRebirthAITest, ProjectsSpellKilledSourceAsTemporaryStackAtExactBattleStartFraction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto originalCount = battle()->battleGetAllUnits(false).size();
	const auto originalHealth = source->getAvailableHealth();
	const auto sourceId = source->unitId();

	auto projected = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	const auto * projectedSource = projected->battleGetUnitByID(sourceId);
	ASSERT_NE(projectedSource, nullptr);
	ASSERT_TRUE(newHorizonsElementalRebirth::isEligibleSource(*projectedSource));
	ASSERT_EQ(projectedSource->getBattleStartMaximumAggregateHP(), source->getBattleStartMaximumAggregateHP());
	ASSERT_GT(projectedSource->getBattleStartMaximumAggregateHP(), 0);
	const auto * visibleSourceHero = projected->battleGetFightingHero(projectedSource->unitSide());
	ASSERT_EQ(visibleSourceHero, confluxHero);
	const auto projectedProfile = newHorizonsElementalRebirth::activeProfile(visibleSourceHero);
	ASSERT_TRUE(projectedProfile.has_value());
	ASSERT_EQ(projectedProfile->rank, 1);
	const auto snapshot = projected->captureElementalRebirthSource(*projectedSource);
	ASSERT_TRUE(snapshot.has_value());
	ASSERT_EQ(snapshot->profile.rank, 1);
	ASSERT_EQ(snapshot->battleStartMaximumAggregateHP, source->getBattleStartMaximumAggregateHP());
	const auto expectedHP = newHorizonsElementalRebirth::targetHP(*snapshot);
	const auto legalPool = newHorizonsElementalRebirth::legalCandidatePool(
		projected->getCreatureCategoryRules(), projected->getAccessibility(projectedSource),
		snapshot->corpsePosition, snapshot->side);
	ASSERT_FALSE(legalPool.empty());

	const auto fireball = SpellID(SpellID::FIREBALL);
	const auto * spell = fireball.toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(projected.get(), confluxHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_NE(mechanics, nullptr);
	const spells::Target target{spells::Destination(snapshot->corpsePosition)};
	spells::detail::ProblemImpl castProblem;
	const bool canCast = mechanics->canBeCastAt(target, castProblem);
	std::vector<std::string> castProblems;
	castProblem.getAll(castProblems);
	ASSERT_TRUE(canCast) << ::testing::PrintToString(castProblems);
	mechanics->castEval(projected->getServerCallback(), target);
	EXPECT_FALSE(projected->battleGetUnitByID(sourceId)->alive());
	EXPECT_TRUE(projected->battleGetUnitByID(survivingAlly->unitId())->alive())
		<< "The adjacent collateral stack has enough units to survive this low-power Fireball";

	const auto spawns = rebirthSpawns(*projected);
	ASSERT_EQ(spawns.size(), 1u);
	const auto * reborn = spawns.front();
	EXPECT_TRUE(reborn->isSummoned());
	EXPECT_FALSE(reborn->isGhost());
	EXPECT_EQ(reborn->unitSide(), BattleSide::DEFENDER);
	EXPECT_EQ(reborn->getPosition(), snapshot->corpsePosition);
	EXPECT_EQ(reborn->getAvailableHealth(), expectedHP)
		<< "The private cast projection must preserve the exact floored battle-start HP pool";
	EXPECT_TRUE(vstd::contains(legalPool, reborn->creatureId()));
	EXPECT_FALSE(projected->captureElementalRebirthSource(*reborn).has_value())
		<< "A temporary Rebirth stack must not recursively become a source";
	EXPECT_EQ(battle()->battleGetAllUnits(false).size(), originalCount)
		<< "Detached spell projection must not add a live battle stack";
	EXPECT_EQ(source->getAvailableHealth(), originalHealth)
		<< "Detached spell projection must not injure the live source";
}

TEST_F(NewHorizonsElementalRebirthAITest, ReplacementChangesPhysicalExchangeScoreInsteadOfBeingDroppedAsMagicalSummon)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto sourceId = source->unitId();
	const auto attackerId = opposingPikemen->unitId();
	const auto liveSourceHealth = source->getAvailableHealth();

	auto rebirthProjection = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache rebirthCache;
	BattleExchangeVariant rebirthExchange;
	rebirthExchange.trackAttack(rebirthProjection->getForUpdate(attackerId),
		rebirthProjection->getForUpdate(sourceId), false, false, rebirthCache, rebirthProjection,
		false, false);
	ASSERT_FALSE(rebirthProjection->battleGetUnitByID(sourceId)->alive())
		<< "The physical projection must cross the destruction transition for its source";
	const auto rebirthScore = rebirthExchange.getScore().enemyDamageReduce
		- rebirthExchange.getScore().ourDamageReduce;
	const auto spawns = rebirthSpawns(*rebirthProjection);
	ASSERT_EQ(spawns.size(), 1u);
	ASSERT_FALSE(rebirthProjection->battleGetUnitByID(sourceId)->alive());
	EXPECT_EQ(source->getAvailableHealth(), liveSourceHealth)
		<< "The selected physical branch must not mutate the authoritative battle";

	const auto * reborn = spawns.front();
	const auto enemies = rebirthProjection->battleGetUnitsIf([&](const battle::Unit * unit)
	{
		return unit && unit->alive() && !rebirthProjection->battleMatchOwner(reborn, unit);
	});
	ASSERT_FALSE(enemies.empty());
	const auto fullSpawnHP = static_cast<int64_t>(reborn->getCount()) * reborn->getMaxHealth();
	ASSERT_GT(fullSpawnHP, 0);
	const float expectedReplacementValue = static_cast<float>(rebirthCache.getOriginalDamage(
		reborn, enemies.front(), rebirthProjection)) * static_cast<float>(reborn->getAvailableHealth())
		/ static_cast<float>(fullSpawnHP);

	// The same legal enemy attack without the Conflux rank is the control. Its direct
	// damage score remains, while the own-side replacement improves our branch score.
	confluxHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(REBIRTH_SKILL)),
		MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	auto baselineProjection = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache baselineCache;
	BattleExchangeVariant baselineExchange;
	baselineExchange.trackAttack(baselineProjection->getForUpdate(attackerId),
		baselineProjection->getForUpdate(sourceId), false, false, baselineCache, baselineProjection,
		false, false);
	const auto baselineScore = baselineExchange.getScore().enemyDamageReduce
		- baselineExchange.getScore().ourDamageReduce;
	EXPECT_TRUE(baselineProjection->getElementalRebirthSpawnUnitIds().empty());
	EXPECT_GT(expectedReplacementValue, 0.0f);
	EXPECT_GT(rebirthScore, baselineScore)
		<< "Our replacement must improve this branch's score, even for a tiny HP pool";
	EXPECT_NEAR(rebirthScore - baselineScore, expectedReplacementValue,
		std::max(1.0f, expectedReplacementValue * 0.02f))
		<< "The projected replacement must contribute exactly once in the physical DPS score scale";
}
