/*
 * NewHorizonsHolyArmorAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"

namespace
{
constexpr auto holyArmorKey = "new-horizons:holyArmor";

SpellID holyArmorSpell()
{
	return SpellID(SpellID::decode(holyArmorKey));
}

class HolyArmorEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit HolyArmorEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class HolyArmorCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	HolyArmorCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};
}

class NewHorizonsHolyArmorAITest : public HeroCommandFixture
{
protected:
	CStack * protectedStack = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<HolyArmorEnvironment> environment;
	std::shared_ptr<HolyArmorCallback> callback;

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

	void prepare(bool visibleMagicalCaster)
	{
		useCommands = false;
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		const auto spell = holyArmorSpell();
		ASSERT_NE(spell, SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		// Keep the concealed hero's book valuable so this fixture would expose an
		// information leak if Holy Armor started reading opposing hero details.
		defenderSideHero->addSpellToSpellbook(SpellID::IMPLOSION);
		defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1000, ChangeValueMode::ABSOLUTE);

		startBattle();
		beginCombat();
		protectedStack = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 20);
		enemy = addStack(BattleSide::DEFENDER,
			creatureByName(visibleMagicalCaster ? "core:imp" : "core:titan"), BattleHex(12, 5), 20);
		ASSERT_NE(protectedStack, nullptr);
		ASSERT_NE(enemy, nullptr);

		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit != protectedStack && unit != enemy)
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		if(visibleMagicalCaster)
		{
			enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::SPELLCASTER, BonusSource::OTHER, 1, BonusSourceID(),
				BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))));
			enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));
			enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::CREATURE_SPELL_POWER, BonusSource::OTHER, 1000, BonusSourceID()));
			ASSERT_TRUE(enemy->canCast());
			const auto * creatureSpell = SpellID(SpellID::MAGIC_ARROW).toSpell();
			ASSERT_NE(creatureSpell, nullptr);
			ASSERT_TRUE(creatureSpell->isCombat());
			ASSERT_TRUE(creatureSpell->isMagical());
			ASSERT_TRUE(creatureSpell->isDamage());
			ASSERT_TRUE(creatureSpell->canBeCast(
				battle(), spells::Mode::CREATURE_ACTIVE, enemy));
			ASSERT_GT(creatureSpell->calculateDamage(enemy), 0);
		}
		else
		{
			ASSERT_TRUE(battle()->battleCanShoot(enemy, protectedStack->getPosition()));
		}

		Bonus immobilized;
		immobilized.type = BonusType::STACKS_SPEED;
		immobilized.duration = BonusDuration::ONE_BATTLE;
		immobilized.val = -protectedStack->getMovementRange();
		protectedStack->addNewBonus(std::make_shared<Bonus>(immobilized));

		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = protectedStack->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);

		callback = std::make_shared<HolyArmorCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<HolyArmorEnvironment>(gameState());
	}

	bool attemptHolyArmor()
	{
		BattleEvaluator evaluator(environment, callback, protectedStack, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		evaluator.selectStackAction(protectedStack);
		if(!evaluator.canCastSpell())
		{
			ADD_FAILURE() << "The test hero must have a legal hero action before evaluating Holy Armor";
			return false;
		}
		return evaluator.attemptCastingSpell(protectedStack);
	}
};

TEST_F(NewHorizonsHolyArmorAITest, ValuesVisibleMagicalCreatureThreatAndProjectsWithoutLiveMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto spell = holyArmorSpell();
	const auto healthBefore = protectedStack->getAvailableHealth();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto * holyArmor = spell.toSpell();
	ASSERT_NE(holyArmor, nullptr);

	ASSERT_TRUE(attemptHolyArmor());
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	EXPECT_EQ(action.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(action.spell, spell);
	const auto target = action.getTarget(battle());
	ASSERT_EQ(target.size(), 1u);
	ASSERT_NE(target.front().unitValue, nullptr);
	EXPECT_EQ(target.front().unitValue->unitId(), protectedStack->unitId());
	EXPECT_EQ(target.front().unitValue->unitSide(), BattleSide::ATTACKER);
	EXPECT_EQ(protectedStack->getAvailableHealth(), healthBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_FALSE(protectedStack->hasBonus(Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(spell)).And(
			Selector::type()(BonusType::SPELL_DAMAGE_REDUCTION))));
	EXPECT_EQ(callback->getBattle(BattleID(0))->battleGetFightingHero(BattleSide::DEFENDER), nullptr)
		<< "The AI must value only the visible creature caster, not the concealed opposing hero";
}

TEST_F(NewHorizonsHolyArmorAITest, DoesNotValuePhysicalThreatOrConcealedEnemyHeroSpellbook)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	const auto aiBattle = callback->getBattle(BattleID(0));
	ASSERT_NE(aiBattle, nullptr);
	EXPECT_EQ(aiBattle->battleGetFightingHero(BattleSide::DEFENDER), nullptr);
	const auto manaBefore = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(attemptHolyArmor());
	EXPECT_TRUE(callback->submitted.empty());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_FALSE(protectedStack->hasBonus(Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(holyArmorSpell())).And(
			Selector::type()(BonusType::SPELL_DAMAGE_REDUCTION))));
}
