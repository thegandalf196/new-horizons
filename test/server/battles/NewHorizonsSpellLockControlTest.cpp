/*
 * NewHorizonsSpellLockControlTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../lib/spells/Problem.h"
#include <vcmi/Environment.h>
#include "../../../AI/BattleAI/HypotheticBattle.h"

namespace
{
class LockControlEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit LockControlEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsSpellLockControlTest : public HeroCommandFixture
{
protected:
	CStack * controlled = nullptr;
	SpellID puppet() const { return SpellID(SpellID::decode(std::string(newHorizonsPuppetMaster::SPELL_ID))); }
	SpellID lock() const { return SpellID(SpellID::decode(std::string(newHorizonsSorcery::SPELL_LOCK_SPELL))); }

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	BonusList effects(const battle::Unit * unit, SpellID spell) const
	{
		return *unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	}

	void prepareReverseControl()
	{
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		startGame();
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			hero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
			hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
			for(const auto spell : {SpellID(SpellID::BLESS), SpellID(SpellID::SLOW), puppet(), lock()})
				hero->addSpellToSpellbook(spell);
			setTestSpellPointTotal(hero, 1000);
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		controlled = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(3, 5), 1000);
		ASSERT_NE(controlled, nullptr);
		ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1000), nullptr);
		beginCombat();
		ASSERT_TRUE(castOn(attackerSideHero, SpellID(SpellID::BLESS), controlled));
		ASSERT_TRUE(castOn(defenderSideHero, puppet(), controlled));
		ASSERT_TRUE(newHorizonsPuppetMaster::hasValidControlMarker(*battle(), controlled));
		ASSERT_EQ(controlled->unitSide(), BattleSide::ATTACKER);
		ASSERT_EQ(battle()->battleGetOwner(controlled), attackerSideHero->getOwner());
		ASSERT_EQ(battle()->battleGetActionController(controlled), defenderSideHero->getOwner());
		advanceRound(); // Restore the defender's paid action before casting Slow.
		ASSERT_TRUE(castOn(defenderSideHero, SpellID(SpellID::SLOW), controlled));
		// Bless and Slow coexist; Haste and Slow intentionally counter each other.
		ASSERT_FALSE(effects(controlled, SpellID(SpellID::BLESS)).empty());
		ASSERT_FALSE(effects(controlled, SpellID(SpellID::SLOW)).empty());
	}

	void verifySeal(bool friendly)
	{
		ASSERT_NO_FATAL_FAILURE(prepareReverseControl());
		auto * caster = friendly ? attackerSideHero : defenderSideHero;
		// The ally has not spent this round's action. The controlling enemy
		// just cast Slow and needs a new round; its two-round Slow then has one.
		if(!friendly)
			advanceRound();
		ASSERT_FALSE(effects(controlled, SpellID(SpellID::BLESS)).empty());
		ASSERT_FALSE(effects(controlled, SpellID(SpellID::SLOW)).empty());
		ASSERT_TRUE(newHorizonsPuppetMaster::hasValidControlMarker(*battle(), controlled));
		ASSERT_EQ(battle()->battleCanCastSpell(caster, spells::Mode::HERO), ESpellCastProblem::OK);
		const auto preservedSpell = SpellID(friendly ? SpellID::BLESS : SpellID::SLOW);
		const auto removedSpell = SpellID(friendly ? SpellID::SLOW : SpellID::BLESS);
		const auto original = effects(controlled, preservedSpell);
		ASSERT_FALSE(original.empty());
		const auto duration = original.front()->turnsRemain;
		const auto health = controlled->getAvailableHealth();
		const auto state = controlled->save();
		auto environment = std::make_shared<LockControlEnvironment>(gameState());
		auto parent = std::make_shared<HypotheticBattle>(environment.get(),
			std::make_shared<CPlayerBattleCallback>(battle(), caster->getOwner()));
		auto parentUnit = parent->getForUpdate(controlled->unitId());
		auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
		auto siblingUnit = sibling->getForUpdate(controlled->unitId());
		auto childUnit = child->getForUpdate(controlled->unitId());
		spells::BattleCast cast(child.get(), caster, spells::Mode::HERO, lock().toSpell());
		const spells::Target target{spells::Destination(childUnit.get())};
		spells::detail::ProblemImpl problem;
		const auto legal = lock().toSpell()->battleMechanics(&cast)->canBeCastAt(target, problem);
		std::vector<std::string> messages;
		problem.getAll(messages);
		ASSERT_TRUE(legal) << (messages.empty() ? "no diagnostic" : messages.front());
		cast.castEval(child->getServerCallback(), target);
		EXPECT_TRUE(effects(childUnit.get(), removedSpell).empty());
		ASSERT_FALSE(effects(childUnit.get(), preservedSpell).empty());
		EXPECT_EQ(effects(childUnit.get(), preservedSpell).front()->turnsRemain, duration);
		EXPECT_EQ(newHorizonsSorcery::spellLockPreservesBeneficial(*childUnit), std::optional<bool>(friendly));
		EXPECT_EQ(newHorizonsPuppetMaster::hasControlMarker(childUnit.get()), !friendly);
		for(const auto * unchanged : {parentUnit.get(), siblingUnit.get()})
		{
			EXPECT_FALSE(newHorizonsSorcery::isSpellLocked(*unchanged));
			EXPECT_FALSE(effects(unchanged, removedSpell).empty());
			ASSERT_FALSE(effects(unchanged, preservedSpell).empty());
			EXPECT_EQ(effects(unchanged, preservedSpell).front()->turnsRemain, duration);
			EXPECT_TRUE(newHorizonsPuppetMaster::hasControlMarker(unchanged));
		}
		EXPECT_EQ(controlled->save(), state);
		EXPECT_FALSE(newHorizonsSorcery::isSpellLocked(*controlled));
		const auto mana = caster->getManaAvailable();
		ASSERT_TRUE(castOn(caster, lock(), controlled));
		EXPECT_LT(caster->getManaAvailable(), mana);
		EXPECT_EQ(controlled->getAvailableHealth(), health);
		EXPECT_TRUE(effects(controlled, removedSpell).empty());
		ASSERT_FALSE(effects(controlled, preservedSpell).empty());
		EXPECT_EQ(effects(controlled, preservedSpell).front()->turnsRemain, duration);
		EXPECT_EQ(newHorizonsSorcery::spellLockPreservesBeneficial(*controlled), std::optional<bool>(friendly));
		EXPECT_EQ(newHorizonsPuppetMaster::hasControlMarker(controlled), !friendly);
		EXPECT_EQ(battle()->battleGetOwner(controlled), attackerSideHero->getOwner());
		EXPECT_EQ(battle()->battleGetActionController(controlled), friendly ? attackerSideHero->getOwner() : defenderSideHero->getOwner());
	}
};
}

TEST_F(NewHorizonsSpellLockControlTest, OriginalAttackerAllySealCleansesSlowAndReversePuppetButPreservesBless)
{
	ASSERT_NO_FATAL_FAILURE(verifySeal(true));
}

TEST_F(NewHorizonsSpellLockControlTest, ReversePuppetControllerEnemySealCleansesBlessButPreservesSlowAndControl)
{
	ASSERT_NO_FATAL_FAILURE(verifySeal(false));
}
