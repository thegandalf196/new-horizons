/*
 * NewHorizonsHeavenlyGaleAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/CSpell.h"

namespace
{
constexpr auto heavenlyGaleKey = "new-horizons:heavenlyGale";
constexpr int heavenlyGaleReductionBasisPoints = 6500;

SpellID heavenlyGaleSpell()
{
	return SpellID(SpellID::decode(heavenlyGaleKey));
}

class HeavenlyGaleEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit HeavenlyGaleEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class HeavenlyGaleCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	HeavenlyGaleCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

enum class ThreatKind
{
	MELEE,
	PHYSICAL_SHOOTER,
	SPELL_LIKE_SHOOTER,
	SIEGE_SHOOTER
};
}

class NewHorizonsHeavenlyGaleAITest : public HeroCommandFixture
{
protected:
	CStack * protectedStack = nullptr;
	CStack * protectedStackTwo = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<HeavenlyGaleEnvironment> environment;
	std::shared_ptr<HeavenlyGaleCallback> callback;

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

	void prepare(ThreatKind threat, bool existingFullGale = false)
	{
		useCommands = false;
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		const auto spell = heavenlyGaleSpell();
		ASSERT_NE(spell, SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		beginCombat();
		protectedStack = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 20);
		protectedStackTwo = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(3, 8), 20);
		const char * enemyCreature = "core:pikeman";
		int enemyCount = 100;
		switch(threat)
		{
		case ThreatKind::MELEE:
			enemyCreature = "core:pikeman";
			break;
		case ThreatKind::PHYSICAL_SHOOTER:
			enemyCreature = "core:titan";
			break;
		case ThreatKind::SPELL_LIKE_SHOOTER:
			enemyCreature = "core:magog";
			break;
		case ThreatKind::SIEGE_SHOOTER:
			enemyCreature = "core:ballista";
			enemyCount = 20;
			break;
		}
		enemy = addStack(BattleSide::DEFENDER, creatureByName(enemyCreature), BattleHex(12, 5), enemyCount);
		ASSERT_NE(protectedStack, nullptr);
		ASSERT_NE(protectedStackTwo, nullptr);
		ASSERT_NE(enemy, nullptr);

		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(true))
			if(unit != protectedStack && unit != protectedStackTwo && unit != enemy)
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		if(threat == ThreatKind::MELEE)
			EXPECT_FALSE(battle()->battleCanShoot(enemy, protectedStack->getPosition()));
		else
			ASSERT_TRUE(battle()->battleCanShoot(enemy, protectedStack->getPosition()));
		if(threat == ThreatKind::SPELL_LIKE_SHOOTER)
			ASSERT_TRUE(enemy->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
		if(threat == ThreatKind::SIEGE_SHOOTER)
			ASSERT_TRUE(enemy->hasBonusOfType(BonusType::SIEGE_WEAPON));

		Bonus immobilized;
		immobilized.type = BonusType::STACKS_SPEED;
		immobilized.duration = BonusDuration::ONE_BATTLE;
		immobilized.val = -protectedStack->getMovementRange();
		protectedStack->addNewBonus(std::make_shared<Bonus>(immobilized));

		if(existingFullGale)
		{
			for(auto * ally : {protectedStack, protectedStackTwo})
			{
				auto marker = std::make_shared<Bonus>(BonusDuration::N_TURNS,
					BonusType::HEAVENLY_GALE, BonusSource::SPELL_EFFECT,
					heavenlyGaleReductionBasisPoints, BonusSourceID(spell));
				marker->turnsRemain = 2;
				ally->addNewBonus(marker);
			}
		}

		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = protectedStack->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);

		callback = std::make_shared<HeavenlyGaleCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<HeavenlyGaleEnvironment>(gameState());
	}

	bool attemptHeavenlyGale()
	{
		BattleEvaluator evaluator(environment, callback, protectedStack, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		evaluator.selectStackAction(protectedStack);
		if(!evaluator.canCastSpell())
		{
			ADD_FAILURE() << "The test hero must have a legal hero action before evaluating Heavenly Gale";
			return false;
		}
		return evaluator.attemptCastingSpell(protectedStack);
	}
};

TEST_F(NewHorizonsHeavenlyGaleAITest, ChoosesArmyWideProtectionAgainstPhysicalShotsWithoutMutatingLiveState)
{
	ASSERT_NO_FATAL_FAILURE(prepare(ThreatKind::PHYSICAL_SHOOTER));
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto spell = heavenlyGaleSpell();
	const auto * heavenlyGale = spell.toSpell();
	ASSERT_NE(heavenlyGale, nullptr);
	spells::BattleCast preview(battle(), attackerSideHero, spells::Mode::HERO, heavenlyGale);
	const auto mechanics = heavenlyGale->battleMechanics(&preview);
	const auto viableTargets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_EQ(viableTargets.size(), 1u);
	EXPECT_TRUE(viableTargets.front().empty())
		<< "A range-X mass spell has one no-location candidate, not a selected unit target";

	ASSERT_TRUE(attemptHeavenlyGale());
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	EXPECT_EQ(action.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(action.spell, spell);
	ASSERT_EQ(action.target.size(), 1u);
	EXPECT_FALSE(action.target.front().hexValue.isValid());
	const auto submittedTarget = action.getTarget(battle());
	ASSERT_EQ(submittedTarget.size(), 1u);
	EXPECT_EQ(submittedTarget.front().unitValue, nullptr)
		<< "The server's NO_LOCATION wire sentinel carries no explicit unit target";
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	for(const auto * ally : {protectedStack, protectedStackTwo})
		EXPECT_FALSE(ally->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(spell)).And(Selector::type()(BonusType::HEAVENLY_GALE))))
			<< "BattleAI must score detached army-wide projection without applying it to live state";
}

TEST_F(NewHorizonsHeavenlyGaleAITest, DoesNotCastWhenVisibleThreatIsMeleeOnly)
{
	ASSERT_NO_FATAL_FAILURE(prepare(ThreatKind::MELEE));
	EXPECT_FALSE(attemptHeavenlyGale());
	EXPECT_TRUE(callback->submitted.empty());
}

TEST_F(NewHorizonsHeavenlyGaleAITest, DoesNotCastAgainstSpellLikeProjectileThreat)
{
	ASSERT_NO_FATAL_FAILURE(prepare(ThreatKind::SPELL_LIKE_SHOOTER));
	EXPECT_FALSE(attemptHeavenlyGale());
	EXPECT_TRUE(callback->submitted.empty());
}

TEST_F(NewHorizonsHeavenlyGaleAITest, IncludesPhysicalSiegeWeaponShots)
{
	ASSERT_NO_FATAL_FAILURE(prepare(ThreatKind::SIEGE_SHOOTER));
	EXPECT_TRUE(attemptHeavenlyGale());
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(callback->submitted.front().spell, heavenlyGaleSpell());
}

TEST_F(NewHorizonsHeavenlyGaleAITest, DoesNotRefreshAnEqualFullArmyWideEffect)
{
	ASSERT_NO_FATAL_FAILURE(prepare(ThreatKind::PHYSICAL_SHOOTER, true));
	EXPECT_FALSE(attemptHeavenlyGale());
	EXPECT_TRUE(callback->submitted.empty());
	for(const auto * ally : {protectedStack, protectedStackTwo})
	{
		const auto marker = ally->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(heavenlyGaleSpell())).And(Selector::type()(BonusType::HEAVENLY_GALE)));
		ASSERT_EQ(marker->size(), 1u);
		EXPECT_EQ(marker->front()->val, heavenlyGaleReductionBasisPoints);
		EXPECT_EQ(marker->front()->turnsRemain, 2);
	}
}
