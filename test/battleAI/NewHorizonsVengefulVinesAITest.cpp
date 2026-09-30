/*
 * NewHorizonsVengefulVinesAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/spells/NewHorizonsVengefulVines.h"
#include "../../server/CGameHandler.h"

#include <map>
#include <set>

namespace
{
SpellID vengefulVinesSpell()
{
	return SpellID(SpellID::decode(std::string(newHorizonsVengefulVines::SPELL_KEY)));
}

class VengefulVinesEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit VengefulVinesEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class VengefulVinesCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	VengefulVinesCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

bool sameLocationTarget(const spells::Target & lhs, const spells::Target & rhs)
{
	if(lhs.size() != rhs.size())
		return false;

	for(size_t index = 0; index < lhs.size(); ++index)
		if(lhs[index].unitValue || rhs[index].unitValue || lhs[index].hexValue != rhs[index].hexValue)
			return false;

	return true;
}

bool intersects(const BattleHexArray & footprint, const battle::Unit * unit)
{
	if(!unit)
		return false;

	return std::ranges::any_of(footprint, [unit](const BattleHex & hex)
	{
		return unit->coversPos(hex);
	});
}
}

class NewHorizonsVengefulVinesAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * friendlyOnPath = nullptr;
	CStack * wideEnemy = nullptr;
	CStack * otherEnemy = nullptr;
	BattleHex origin = BattleHex(8, 5);
	BattleHex::EDir direction = BattleHex::RIGHT;
	BattleHexArray expectedFootprint;
	std::shared_ptr<VengefulVinesEnvironment> environment;
	std::shared_ptr<VengefulVinesCallback> callback;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void prepare()
	{
		useCommands = true;
		startGame();

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		const auto spell = vengefulVinesSpell();
		ASSERT_NE(spell, SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);

		startBattle();
		beginCombat();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		expectedFootprint = newHorizonsVengefulVines::footprint(origin, direction);
		ASSERT_EQ(expectedFootprint.size(), 6u);
		ASSERT_EQ(origin.cloneInDirection(direction, false), expectedFootprint[1]);

		active = addStack(BattleSide::ATTACKER,
			creatureByName("core:pikeman"), BattleHex(3, 9), 1);
		friendlyOnPath = addStack(BattleSide::ATTACKER,
			creatureByName("core:pikeman"), expectedFootprint[3], 20);
		wideEnemy = addStack(BattleSide::DEFENDER,
			creatureByName("core:griffin"), expectedFootprint[0], 20);
		otherEnemy = addStack(BattleSide::DEFENDER,
			creatureByName("core:ogre"), expectedFootprint[5], 20);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(friendlyOnPath, nullptr);
		ASSERT_NE(wideEnemy, nullptr);
		ASSERT_NE(otherEnemy, nullptr);
		ASSERT_TRUE(wideEnemy->doubleWide());
		ASSERT_TRUE(wideEnemy->getHexes().contains(expectedFootprint[0]));
		ASSERT_TRUE(wideEnemy->getHexes().contains(expectedFootprint[1]));
		ASSERT_TRUE(friendlyOnPath->getHexes().contains(expectedFootprint[3]));

		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);

		callback = std::make_shared<VengefulVinesCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<VengefulVinesEnvironment>(gameState());
	}
};

TEST_F(NewHorizonsVengefulVinesAITest, EnumeratesLegalPathsAndAIProjectionMatchesResolvedDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = vengefulVinesSpell();
	const auto * vines = spell.toSpell();
	ASSERT_NE(vines, nullptr);
	ASSERT_EQ(attackerSideHero->getEffectPowerDivisor(vines), 10);

	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto wideHealthBefore = wideEnemy->getAvailableHealth();
	const auto otherHealthBefore = otherEnemy->getAvailableHealth();
	const auto friendlyHealthBefore = friendlyOnPath->getAvailableHealth();
	const auto widePathOverlap = std::count_if(expectedFootprint.begin(), expectedFootprint.end(),
		[this](const BattleHex & hex) { return wideEnemy->coversPos(hex); });
	ASSERT_EQ(widePathOverlap, 2)
		<< "The test footprint intersects both hexes of this double-wide stack";

	HypotheticBattle targetSnapshot(environment.get(), callback->getBattle(BattleID(0)));
	spells::BattleCast targetPreview(&targetSnapshot, attackerSideHero, spells::Mode::HERO, vines);
	const auto targetMechanics = vines->battleMechanics(&targetPreview);
	ASSERT_EQ(targetMechanics->getEffectValue(), 130);
	ASSERT_EQ(targetMechanics->getTargetTypes(),
		(std::vector<spells::AimType>{spells::AimType::LOCATION, spells::AimType::LOCATION}));
	const auto viableTargets = SpellTargetEvaluator::getViableTargets(targetMechanics.get());
	ASSERT_FALSE(viableTargets.empty());

	const spells::Target knownLegalTarget{
		spells::Destination(expectedFootprint[0]), spells::Destination(expectedFootprint[1])};
	bool foundKnownTarget = false;
	std::set<std::pair<int, int>> uniqueLocationPairs;
	for(const auto & target : viableTargets)
	{
		ASSERT_EQ(target.size(), 2u);
		ASSERT_EQ(target[0].unitValue, nullptr);
		ASSERT_EQ(target[1].unitValue, nullptr);
		const auto footprint = newHorizonsVengefulVines::footprint(target);
		ASSERT_EQ(footprint.size(), 6u);
		spells::detail::ProblemImpl problem;
		EXPECT_TRUE(targetMechanics->canBeCastAt(target, problem));
		EXPECT_TRUE(intersects(footprint, wideEnemy) || intersects(footprint, otherEnemy))
			<< "Vines AI candidates should be filtered before legality checks when no enemy is hit";
		EXPECT_TRUE(uniqueLocationPairs.emplace(target[0].hexValue.toInt(),
			target[1].hexValue.toInt()).second);
		foundKnownTarget |= sameLocationTarget(target, knownLegalTarget);
	}
	ASSERT_TRUE(foundKnownTarget);

	// The known cast crosses both body hexes of one Griffin, but the effect and
	// its generic damage projection must apply once to the stack.
	const auto knownFootprint = newHorizonsVengefulVines::footprint(knownLegalTarget);
	ASSERT_EQ(knownFootprint.size(), 6u);
	ASSERT_TRUE(targetMechanics->canBeCastAt(knownLegalTarget));
	targetMechanics->castEval(targetSnapshot.getServerCallback(), knownLegalTarget);
	const auto * projectedWide = targetSnapshot.battleGetUnitByID(wideEnemy->unitId());
	const auto * projectedOther = targetSnapshot.battleGetUnitByID(otherEnemy->unitId());
	ASSERT_NE(projectedWide, nullptr);
	ASSERT_NE(projectedOther, nullptr);
	EXPECT_EQ(wideHealthBefore - projectedWide->getAvailableHealth(), 130);
	EXPECT_EQ(otherHealthBefore - projectedOther->getAvailableHealth(), 130);
	EXPECT_EQ(friendlyHealthBefore,
		targetSnapshot.battleGetUnitByID(friendlyOnPath->unitId())->getAvailableHealth());
	EXPECT_EQ(wideEnemy->getAvailableHealth(), wideHealthBefore)
		<< "The detached target projection must not modify the live battle";

	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
	ASSERT_EQ(action.spell, spell);
	const auto aiTarget = action.getTarget(battle());
	ASSERT_EQ(aiTarget.size(), 2u);
	ASSERT_TRUE(std::any_of(viableTargets.begin(), viableTargets.end(), [&](const spells::Target & target)
	{
		return sameLocationTarget(target, aiTarget);
	})) << "BattleEvaluator should retain the full selected origin/orientation pair";

	HypotheticBattle aiProjection(environment.get(), callback->getBattle(BattleID(0)));
	spells::BattleCast aiPreview(&aiProjection, attackerSideHero, spells::Mode::HERO, vines);
	const auto aiMechanics = vines->battleMechanics(&aiPreview);
	ASSERT_TRUE(aiMechanics->canBeCastAt(aiTarget));
	aiMechanics->castEval(aiProjection.getServerCallback(), aiTarget);
	std::map<uint32_t, int64_t> projectedHealth;
	for(const auto * enemy : {wideEnemy, otherEnemy})
	{
		const auto * projectedEnemy = aiProjection.battleGetUnitByID(enemy->unitId());
		ASSERT_NE(projectedEnemy, nullptr);
		projectedHealth.emplace(enemy->unitId(), projectedEnemy->getAvailableHealth());
	}
	const auto * projectedFriendly = aiProjection.battleGetUnitByID(friendlyOnPath->unitId());
	ASSERT_NE(projectedFriendly, nullptr);
	EXPECT_EQ(projectedFriendly->getAvailableHealth(), friendlyHealthBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(wideEnemy->getAvailableHealth(), wideHealthBefore);
	EXPECT_EQ(otherEnemy->getAvailableHealth(), otherHealthBefore);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_LT(attackerSideHero->getManaAvailable(), manaBefore);
	for(const auto * enemy : {wideEnemy, otherEnemy})
		EXPECT_EQ(enemy->getAvailableHealth(), projectedHealth.at(enemy->unitId()))
			<< "The actual cast should match BattleAI's hypothetical damage forecast";
	EXPECT_EQ(friendlyOnPath->getAvailableHealth(), friendlyHealthBefore)
		<< "Vengeful Vines must not damage a friendly stack inside the footprint";
}
