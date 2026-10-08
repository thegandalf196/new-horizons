/*
 * NewHorizonsCabirRepairTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of the license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleUnitTurnReason.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../lib/spells/effects/Effect.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr std::string_view REPAIR_SPELL_KEY = "new-horizons:cabirRepair";
constexpr int32_t MASTER_CABIR_COUNT = 10;

SpellID repairSpell()
{
	return SpellID(SpellID::decode(std::string(REPAIR_SPELL_KEY)));
}

CreatureID creature(std::string_view identifier)
{
	return CreatureID(CreatureID::decode(std::string(identifier)));
}

class NewHorizonsCabirRepairTest : public HeroCommandFixture
{
protected:
	CStack * caster = nullptr;
	CStack * stoneGolem = nullptr;
	CStack * ironGolem = nullptr;
	CStack * stoneGargoyle = nullptr;
	CStack * obsidianGargoyle = nullptr;
	CStack * hostileGolem = nullptr;
	CStack * unrelated = nullptr;
	CStack * deadGolem = nullptr;
	CStack * unusableGargoyle = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the activated New Horizons preset";
		ASSERT_NE(repairSpell(), SpellID::NONE);
		ASSERT_NE(repairSpell().toSpell(), nullptr);
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare()
	{
		startGame();
		// Repair deliberately rejects summoned stacks; BattleTestFixture::addStack
		// gives synthetic units the summoned placeholder slot, so use real armies.
		attackerSideHero->clearSlots();
		defenderSideHero->clearSlots();
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creature("core:masterGremlin"), MASTER_CABIR_COUNT));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(1), creature("core:stoneGolem"), 5));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(2), creature("core:ironGolem"), 5));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(3), creature("core:stoneGargoyle"), 5));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(4), creature("core:obsidianGargoyle"), 5));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(5), creature("core:pikeman"), 5));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(6), creature("core:stoneGolem"), 2));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creature("core:ironGolem"), 5));
		startBattle();

		caster = findArmyStack(BattleSide::ATTACKER, SlotID(0));
		stoneGolem = findArmyStack(BattleSide::ATTACKER, SlotID(1));
		ironGolem = findArmyStack(BattleSide::ATTACKER, SlotID(2));
		stoneGargoyle = findArmyStack(BattleSide::ATTACKER, SlotID(3));
		obsidianGargoyle = findArmyStack(BattleSide::ATTACKER, SlotID(4));
		unrelated = findArmyStack(BattleSide::ATTACKER, SlotID(5));
		deadGolem = findArmyStack(BattleSide::ATTACKER, SlotID(6));
		hostileGolem = findArmyStack(BattleSide::DEFENDER, SlotID(0));

		ASSERT_NE(caster, nullptr);
		ASSERT_NE(stoneGolem, nullptr);
		ASSERT_NE(ironGolem, nullptr);
		ASSERT_NE(stoneGargoyle, nullptr);
		ASSERT_NE(obsidianGargoyle, nullptr);
		ASSERT_NE(hostileGolem, nullptr);
		ASSERT_NE(unrelated, nullptr);
		ASSERT_NE(deadGolem, nullptr);

		beginCombat();
		activate(caster);
		ASSERT_EQ(battle()->battleGetActionController(caster), PlayerColor(0));
		ASSERT_TRUE(caster->canCast());
		ASSERT_EQ(caster->casts.available(), 1);
	}

	CStack * findArmyStack(BattleSide side, SlotID slot) const
	{
		const auto matches = battle()->battleGetStacksIf([side, slot](const CStack * stack)
		{
			return stack->unitSide() == side && stack->unitSlot() == slot;
		});
		if(matches.size() != 1)
		{
			ADD_FAILURE() << "Expected one original army stack in slot " << slot.getNum()
				<< " on side " << static_cast<int>(side) << ", found " << matches.size();
			return nullptr;
		}
		return battle()->getStack(matches.front()->unitId());
	}

	void activate(CStack * stack)
	{
		BattleSetActiveStack pack;
		pack.battleID = BattleID(0);
		pack.stack = stack->unitId();
		pack.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(pack);
		ASSERT_EQ(battle()->battleActiveUnit(), stack);
	}

	void damageStack(CStack * stack, int64_t damage, bool destroyRemains = false)
	{
		auto state = stack->acquireState();
		state->damage(damage, destroyRemains);
		UnitChanges update(stack->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		update.healthDelta = -damage;
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);
	}

	void markPhantom(CStack * stack)
	{
		auto state = stack->acquireState();
		state->summoned = true;
		state->initializePhantomProfile(state->getMaxHealth(),
			newHorizonsSorcery::PHANTOM_ARMY_DURATION_ROUNDS);
		UnitChanges update(stack->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);
	}

	spells::effects::SpellEffectValue healthForecast(const CStack * target,
		const spells::Mechanics * spellMechanics) const
	{
		spells::Target aim;
		aim.emplace_back(target);
		const auto spellTarget = spellMechanics->canonicalizeTarget(aim);
		spells::effects::SpellEffectValue total;
		spellMechanics->forEachEffect([&](const spells::effects::Effect & effect)
		{
			const auto affected = effect.transformTarget(spellMechanics, aim, spellTarget);
			total += effect.getHealthChange(spellMechanics, affected);
			return false;
		});
		return total;
	}

	bool cast(CStack * target)
	{
		const spells::Target aim{spells::Destination(target)};
		const auto action = BattleAction::makeCreatureSpellcast(caster, aim, repairSpell());
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
};
}

TEST_F(NewHorizonsCabirRepairTest, RepairToolbarRegistersExistingCureSymbolAtItsOwnEffectSlot)
{
	// Provisional repair presentation reuses the original Cure symbol. It must
	// register at Repair's assigned ID+1, not request that missing native frame.
	const auto id = repairSpell();
	const auto * spell = id.toSpell();
	const auto expectedIcon = "SPELLINT.def:0:" + std::to_string(SpellID(SpellID::CURE).getNum() + 1);
	EXPECT_EQ(spell->getIconEffect(), expectedIcon);
	bool effectRegistered = false;
	spell->registerIcons([&](size_t index, size_t group, const std::string & listName, const std::string & imageName)
	{
		if(listName != "SPELLINT")
			return;
		effectRegistered = true;
		EXPECT_EQ(index, id.getNum() + 1);
		EXPECT_EQ(group, 0);
		EXPECT_EQ(imageName, expectedIcon);
	});
	EXPECT_TRUE(effectRegistered);
}

TEST_F(NewHorizonsCabirRepairTest, AcceptedCastPermanentlyRepairsGolemAndMatchesGenericHealthForecast)
{
	prepare();
	const auto * spell = repairSpell().toSpell();
	spells::BattleCast battleCast(battle(), caster, spells::Mode::CREATURE_ACTIVE, spell);
	const auto spellMechanics = spell->battleMechanics(&battleCast);
	ASSERT_NE(spellMechanics, nullptr);
	EXPECT_EQ(spellMechanics->getEffectValue(), MASTER_CABIR_COUNT * 10)
		<< "the authored specific spell power gives ten repair HP per surviving Cabir Master";

	damageStack(stoneGolem, stoneGolem->getMaxHealth() + 10);
	ASSERT_EQ(stoneGolem->getUnusableRemains(), 0);
	const auto healthBefore = stoneGolem->getAvailableHealth();
	const auto countBefore = stoneGolem->getCount();
	const auto forecast = healthForecast(stoneGolem, spellMechanics.get());
	ASSERT_GT(forecast.hpDelta, 0);
	ASSERT_GT(forecast.unitsDelta, 0);

	const auto castsBefore = caster->casts.available();
	const auto activeBefore = battle()->battleActiveUnit();
	ASSERT_TRUE(cast(stoneGolem));

	EXPECT_EQ(stoneGolem->getAvailableHealth() - healthBefore, forecast.hpDelta)
		<< "the generic mechanics health estimate matches the authoritative repair";
	EXPECT_EQ(stoneGolem->getCount() - countBefore, forecast.unitsDelta);
	EXPECT_EQ(stoneGolem->getCount(), 5)
		<< "permanent Repair restores an eligible casualty rather than adding a temporary body";
	EXPECT_EQ(stoneGolem->health.getResurrected(), 0);
	EXPECT_EQ(caster->casts.available(), castsBefore - 1);
	EXPECT_TRUE(caster->castSpellThisTurn);
	EXPECT_NE(battle()->battleActiveUnit(), activeBefore)
		<< "an accepted creature spell consumes the stack's ordinary activation";

	EXPECT_FALSE(caster->canCast()) << "the spent cast is unavailable for another activation";
	EXPECT_EQ(caster->casts.available(), 0);
	activate(caster);
	damageStack(ironGolem, 1);
	const auto exhaustedTargetHealth = ironGolem->getAvailableHealth();
	const auto actionsBeforeExhaustedRequest = server.startedActions.size();
	EXPECT_FALSE(cast(ironGolem)) << "the authoritative action path rejects a depleted CASTS allowance";
	EXPECT_EQ(ironGolem->getAvailableHealth(), exhaustedTargetHealth);
	EXPECT_EQ(caster->casts.available(), 0);
	EXPECT_EQ(server.startedActions.size(), actionsBeforeExhaustedRequest);
	EXPECT_EQ(battle()->battleActiveUnit(), caster);
}

TEST_F(NewHorizonsCabirRepairTest, OnlyLivingEligibleGolemAndGargoyleStacksCanBeSelected)
{
	prepare();
	for(auto * stack : {stoneGolem, ironGolem, stoneGargoyle, obsidianGargoyle})
		damageStack(stack, 1);
	damageStack(hostileGolem, 1);
	damageStack(unrelated, 1);
	damageStack(deadGolem, deadGolem->getTotalHealth());
	ASSERT_TRUE(deadGolem->isDead());

	const auto * spell = repairSpell().toSpell();
	spells::BattleCast battleCast(battle(), caster, spells::Mode::CREATURE_ACTIVE, spell);
	const auto spellMechanics = spell->battleMechanics(&battleCast);
	ASSERT_NE(spellMechanics, nullptr);
	for(const auto * stack : {stoneGolem, ironGolem, stoneGargoyle, obsidianGargoyle})
		EXPECT_TRUE(spellMechanics->canBeCastAt(spells::Target{spells::Destination(stack)}))
			<< stack->getDescription() << " is in the authored repair whitelist";

	for(auto * stack : {hostileGolem, unrelated, deadGolem})
	{
		const auto healthBefore = stack->getAvailableHealth();
		const auto actionsBefore = server.startedActions.size();
		EXPECT_FALSE(spellMechanics->canBeCastAt(spells::Target{spells::Destination(stack)}))
			<< stack->getDescription() << " is not a valid repair target";
		EXPECT_FALSE(cast(stack)) << "the authoritative unit-spell action rejects this invalid target";
		EXPECT_EQ(stack->getAvailableHealth(), healthBefore);
		EXPECT_EQ(caster->casts.available(), 1);
		EXPECT_FALSE(caster->castSpellThisTurn);
		EXPECT_EQ(battle()->battleActiveUnit(), caster);
		EXPECT_EQ(server.startedActions.size(), actionsBefore)
			<< "an invalid repair target is rejected before publishing a normal activation";
	}
}

TEST_F(NewHorizonsCabirRepairTest, RejectsFriendlyStackWithOnlyUnusableCasualty)
{
	startGame();
	attackerSideHero->clearSlots();
	defenderSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creature("core:masterGremlin"), MASTER_CABIR_COUNT));
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(1), creature("core:obsidianGargoyle"), 2));
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creature("core:pikeman"), 1));
	startBattle();
	caster = findArmyStack(BattleSide::ATTACKER, SlotID(0));
	unusableGargoyle = findArmyStack(BattleSide::ATTACKER, SlotID(1));
	ASSERT_NE(caster, nullptr);
	ASSERT_NE(unusableGargoyle, nullptr);
	beginCombat();
	activate(caster);
	ASSERT_TRUE(caster->canCast());

	damageStack(unusableGargoyle, unusableGargoyle->getMaxHealth(), true);
	ASSERT_FALSE(unusableGargoyle->isDead());
	ASSERT_EQ(unusableGargoyle->getUnusableRemains(), 1);
	const auto healthBefore = unusableGargoyle->getAvailableHealth();
	const auto actionsBefore = server.startedActions.size();
	spells::BattleCast battleCast(battle(), caster, spells::Mode::CREATURE_ACTIVE, repairSpell().toSpell());
	const auto spellMechanics = repairSpell().toSpell()->battleMechanics(&battleCast);
	ASSERT_NE(spellMechanics, nullptr);
	EXPECT_FALSE(spellMechanics->canBeCastAt(
		spells::Target{spells::Destination(unusableGargoyle)}));
	EXPECT_FALSE(cast(unusableGargoyle));
	EXPECT_EQ(unusableGargoyle->getAvailableHealth(), healthBefore);
	EXPECT_EQ(caster->casts.available(), 1);
	EXPECT_FALSE(caster->castSpellThisTurn);
	EXPECT_EQ(battle()->battleActiveUnit(), caster);
	EXPECT_EQ(server.startedActions.size(), actionsBefore);
}

TEST_F(NewHorizonsCabirRepairTest, PhantomCasterHealsWoundsButCannotRestoreGolemCasualties)
{
	prepare();
	markPhantom(caster);
	damageStack(stoneGolem, stoneGolem->getMaxHealth() + 10);
	const auto healthBefore = stoneGolem->getAvailableHealth();
	const auto countBefore = stoneGolem->getCount();

	ASSERT_TRUE(cast(stoneGolem));
	EXPECT_EQ(stoneGolem->getAvailableHealth() - healthBefore, 10)
		<< "a phantom caster inherits heal-only limits while still repairing survivor wounds";
	EXPECT_EQ(stoneGolem->getCount(), countBefore)
		<< "phantom Repair does not resurrect permanently lost Golems";
	EXPECT_EQ(stoneGolem->health.getResurrected(), 0);
}
