/*
 * NewHorizonsBattlefieldMedicTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "../StdInc.h"
#include "battles/HeroCommandFixture.h"
#include "../SpellPointTestUtils.h"

#include "../../AI/BattleAI/BattleAI.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/BattleChanges.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"
#include "../../server/queries/BattleQueries.h"
#include "../../server/queries/QueriesProcessor.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vcmi/Environment.h>

namespace
{
constexpr auto skillId = "new-horizons:warMachines";
constexpr auto medicId = "new-horizons:warMachines.battlefieldMedic";
constexpr int32_t initialCount = 100;

class MedicEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit MedicEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class NewHorizonsBattlefieldMedicTest : public HeroCommandFixture
{
protected:
	const CStack * target = nullptr;
	const CStack * tent = nullptr;
	CreatureID targetCreature;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		auto & entries = perks["skills"][skillId]["perks"].Vector();
		const auto found = std::ranges::find_if(entries, [](const auto & value)
		{
			return value["id"].String() == medicId;
		});
		if(found == entries.end())
			throw std::runtime_error("Missing Battlefield Medic registry entry");
		const auto status = (*found)["effect"]["status"].String();
		RecordProperty("battlefield_medic_registry_status", status);
		RecordProperty("battlefield_medic_private_override", status == "planned" ? "true" : "false");
		if(status == "planned")
			(*found)["effect"]["status"].String() = "active";
		else if(status != "active")
			throw std::runtime_error("Battlefield Medic must be planned or active");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perks));
	}

	void acceptPerk(const char * id)
	{
		const auto ranks = [this](const std::string & skill) { return attackerSideHero->getPerkSkillRank(skill); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = attackerSideHero->getPerkState().prepareOffer(ranks, seed);
			const auto choice = std::ranges::find_if(offer, [id](const auto & value)
			{
				return value.selection.skillId == skillId && value.selection.perkId == id;
			});
			if(choice == offer.end())
				continue;
			gameHandler->levelUpHero(attackerSideHero, offer, std::distance(offer.begin(), choice), seed, false);
			ASSERT_TRUE(attackerSideHero->hasActivePerk(skillId, id));
			return;
		}
		FAIL() << "Missing legal War Machines offer: " << id;
	}

	void prepare(bool medic = true, const std::string & creature = "core:pikeman")
	{
		startGame();
		const auto skill = SecondarySkill(SecondarySkill::decode(skillId));
		ASSERT_TRUE(attackerSideHero->getPerkState().canAdvanceSkillNormally(skillId, attackerSideHero->getSecSkillLevel(skill)));
		gameHandler->levelUpHero(attackerSideHero, skill, false);
		ASSERT_EQ(attackerSideHero->getPerkSkillRank(skillId), MasteryLevel::BASIC);
		ASSERT_NO_FATAL_FAILURE(acceptPerk("new-horizons:warMachines.surgeon"));
		ASSERT_TRUE(attackerSideHero->getPerkState().canAdvanceSkillNormally(skillId, attackerSideHero->getSecSkillLevel(skill)));
		gameHandler->levelUpHero(attackerSideHero, skill, false);
		ASSERT_EQ(attackerSideHero->getPerkSkillRank(skillId), MasteryLevel::ADVANCED);
		ASSERT_NO_FATAL_FAILURE(acceptPerk(medic ? medicId : "new-horizons:warMachines.piercingBolts"));
		EXPECT_EQ(attackerSideHero->hasActivePerk(skillId, medicId), medic);
		giveArtifact(attackerSideHero, ArtifactID::FIRST_AID_TENT, ArtifactPosition::MACH3);
		attackerSideHero->clearSlots();
		defenderSideHero->clearSlots();
		targetCreature = creatureByName(creature);
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), targetCreature, initialCount));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 1));
		startBattle();
		const auto troops = battle()->battleGetStacksIf([this](const CStack * stack)
		{
			return stack->unitSide() == BattleSide::ATTACKER && stack->creatureId() == targetCreature;
		});
		const auto tents = battle()->battleGetStacksIf([](const CStack * stack)
		{
			return stack->unitSide() == BattleSide::ATTACKER && stack->isFirstAidTent();
		});
		ASSERT_EQ(troops.size(), 1u);
		ASSERT_EQ(tents.size(), 1u);
		target = troops.front();
		tent = tents.front();
		beginCombat();
	}

	void injure(int64_t amount, bool unusable = false)
	{
		auto state = target->acquireState();
		state->damage(amount, unusable, battle::DamageProvenance::PHYSICAL_CREATURE);
		UnitChanges change(target->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = -amount;
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		changed.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(changed);
		ASSERT_TRUE(target->alive());
	}

	void advanceUntilTent()
	{
		for(size_t i = 0; i < battle()->stacks.size() * 4; ++i)
		{
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			if(active == tent)
				return;
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(active), BattleAction::makeDefend(active)));
		}
		FAIL() << "Tent never reached its authoritative activation";
	}

	void healAndCheckPreview()
	{
		ASSERT_NO_FATAL_FAILURE(advanceUntilTent());
		const auto preview = battle()->battleGetFirstAidHealingPreview(tent, target);
		const auto health = target->getAvailableHealth();
		const auto count = target->getCount();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeHeal(tent, target)));
		EXPECT_EQ(target->getAvailableHealth() - health, preview.totalHealedHP());
		EXPECT_EQ(target->getCount() - count, preview.restoredCount);
		EXPECT_LE(target->getCount(), initialCount);
	}

	void finishAndCheckArmy(int32_t expectedCount)
	{
		auto query = std::make_shared<CBattleQuery>(gameHandler.get(), battle());
		gameHandler->queries->addQuery(query);
		gameHandler->battles->cheatBattleVictory(PlayerColor(0));
		ASSERT_TRUE(query->result.has_value());
		for(const auto player : {PlayerColor(0), PlayerColor(1)})
		{
			const auto dialog = gameHandler->queries->topQuery(player);
			if(dialog && dialog->getType() == QueryType::BattleDialog)
			{
				ASSERT_TRUE(gameHandler->queryReply(dialog->queryID, 0, player));
			}
		}
		ASSERT_TRUE(attackerSideHero->hasStackAtSlot(SlotID(0)));
		EXPECT_EQ(attackerSideHero->getStack(SlotID(0)).getCount(), expectedCount);
	}
};

TEST_F(NewHorizonsBattlefieldMedicTest, NormalHealThenHalfCalculatedOutputRestoresPermanentCasualties)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto hp = target->getMaxHealth();
	ASSERT_NO_FATAL_FAILURE(injure(40 * hp + 3));
	const auto output = battle()->battleGetFirstAidHealingOutput(tent);
	const auto budget = battle()->battleGetBattlefieldMedicRestorationBudget(tent, target);
	EXPECT_EQ(budget, output / 2);
	const auto preview = battle()->battleGetFirstAidHealingPreview(tent, target);
	EXPECT_EQ(preview.survivorHealedHP, 3);
	EXPECT_EQ(preview.restoredHP, std::min<int64_t>(budget, 40 * hp));
	EXPECT_GT(preview.restoredCount, 0);
	ASSERT_NO_FATAL_FAILURE(healAndCheckPreview());
	EXPECT_EQ(target->health.getResurrected(), 0);
	const auto permanentCount = target->getCount();
	ASSERT_NO_FATAL_FAILURE(finishAndCheckArmy(permanentCount));
}

TEST_F(NewHorizonsBattlefieldMedicTest, NoPerkHealsSurvivorsButCannotRestoreOrTargetCasualtyOnlyStack)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	const auto hp = target->getMaxHealth();
	ASSERT_NO_FATAL_FAILURE(injure(40 * hp));
	EXPECT_FALSE(battle()->battleCanHealWithFirstAidTent(tent, target));
	EXPECT_EQ(battle()->battleGetFirstAidHealingPreview(tent, target).totalHealedHP(), 0);
	ASSERT_NO_FATAL_FAILURE(injure(3));
	const auto count = target->getCount();
	EXPECT_EQ(battle()->battleGetBattlefieldMedicRestorationBudget(tent, target), 0);
	ASSERT_NO_FATAL_FAILURE(healAndCheckPreview());
	EXPECT_EQ(target->getCount(), count);
	ASSERT_NO_FATAL_FAILURE(finishAndCheckArmy(count));
}

TEST_F(NewHorizonsBattlefieldMedicTest, AiChoosesCasualtyOnlyStackAndAuthoritativeTentAcceptsItsAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(injure(40 * target->getMaxHealth()));
	ASSERT_FALSE(target->canBeHealed());
	ASSERT_TRUE(battle()->battleCanHealWithFirstAidTent(tent, target));
	const auto preview = battle()->battleGetFirstAidHealingPreview(tent, target);
	EXPECT_EQ(preview.survivorHealedHP, 0);
	EXPECT_GT(preview.restoredHP, 0);
	ASSERT_NO_FATAL_FAILURE(advanceUntilTent());
	auto environment = std::make_shared<MedicEnvironment>(gameState());
	auto callback = std::make_shared<CBattleCallback>(PlayerColor(0), nullptr);
	callback->onBattleStarted(battle());
	CBattleAI ai;
	ai.initBattleInterface(environment, callback);
	const auto action = ai.useHealingTent(BattleID(0), tent);
	ASSERT_EQ(action.actionType, EActionType::STACK_HEAL);
	const auto aim = action.getTarget(battle());
	ASSERT_EQ(aim.size(), 1u);
	EXPECT_EQ(aim.front().unitValue, target);
	const auto health = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(target->getAvailableHealth() - health, preview.restoredHP);
}

TEST_F(NewHorizonsBattlefieldMedicTest, RestorationCapsAtBattleStartAndCannotRecoverDestroyedRemains)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto hp = target->getMaxHealth();
	ASSERT_NO_FATAL_FAILURE(injure(3 * hp, true));
	ASSERT_EQ(target->health.getUnusableRemains(), 3);
	ASSERT_NO_FATAL_FAILURE(injure(hp + 3));
	const auto preview = battle()->battleGetFirstAidHealingPreview(tent, target);
	EXPECT_EQ(preview.survivorHealedHP, 3);
	EXPECT_EQ(preview.restoredHP, hp);
	EXPECT_EQ(preview.restoredCount, 1);
	ASSERT_NO_FATAL_FAILURE(healAndCheckPreview());
	EXPECT_EQ(target->getCount(), initialCount - 3);
	EXPECT_EQ(target->health.getUnusableRemains(), 3);
	ASSERT_NO_FATAL_FAILURE(finishAndCheckArmy(initialCount - 3));
}

TEST_F(NewHorizonsBattlefieldMedicTest, NoCasualtiesDoesNotConvertRestorationBudgetIntoExtraSurvivorHealing)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(injure(3));
	const auto preview = battle()->battleGetFirstAidHealingPreview(tent, target);
	EXPECT_EQ(preview.survivorHealedHP, 3);
	EXPECT_EQ(preview.restoredHP, 0);
	EXPECT_EQ(preview.restoredCount, 0);
	ASSERT_NO_FATAL_FAILURE(healAndCheckPreview());
	EXPECT_EQ(target->getCount(), initialCount);
}

TEST_F(NewHorizonsBattlefieldMedicTest, RestorationPoolFirstFinishesAnOrdinaryHealingUnfilledSurvivorWound)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, "core:azureDragon"));
	const auto hp = target->getMaxHealth();
	const auto output = battle()->battleGetFirstAidHealingOutput(tent);
	ASSERT_GT(output, 3);
	ASSERT_LT(output + 3, hp);
	ASSERT_NO_FATAL_FAILURE(injure(2 * hp + output + 3));
	const auto count = target->getCount();
	const auto preview = battle()->battleGetFirstAidHealingPreview(tent, target);
	EXPECT_EQ(preview.survivorHealedHP, output);
	EXPECT_EQ(preview.restoredHP, output / 2);
	EXPECT_EQ(preview.restoredCount, 1)
		<< "RESURRECT's pool first heals the remaining3HP survivor wound, then restores a partial casualty";
	ASSERT_NO_FATAL_FAILURE(healAndCheckPreview());
	EXPECT_EQ(target->getCount(), count + 1);
	EXPECT_EQ(target->getFirstHPleft(), output / 2 - 3);
	ASSERT_NO_FATAL_FAILURE(finishAndCheckArmy(count + 1));
}

TEST_F(NewHorizonsBattlefieldMedicTest, MedicPermanentRestorationDoesNotMakeEarlierReanimateBodiesPermanent)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = SpellID(SpellID::decode(std::string(newHorizonsMagic::SHADOW_REANIMATE_SPELL)));
	ASSERT_NE(spell, SpellID::NONE);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(spell);
	const auto shadow = SecondarySkill::decode(std::string(newHorizonsMagic::SHADOW_MAGIC_SKILL));
	ASSERT_GE(shadow, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(shadow), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	ASSERT_NO_FATAL_FAILURE(injure(55 * target->getMaxHealth()));
	const auto permanentBefore = target->getCount();
	ASSERT_NO_FATAL_FAILURE(advanceUntilTent());
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell;
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	const auto temporary = target->health.getResurrected();
	ASSERT_GT(temporary, 0);
	EXPECT_EQ(target->getCount(), permanentBefore + temporary);
	const auto preview = battle()->battleGetFirstAidHealingPreview(tent, target);
	ASSERT_GT(preview.restoredCount, 0);
	ASSERT_NO_FATAL_FAILURE(healAndCheckPreview());
	EXPECT_EQ(target->health.getResurrected(), temporary)
		<< "The Tent's permanent pool must not promote an earlier temporary Re-animate cohort";
	const auto expectedPermanent = permanentBefore + preview.restoredCount;
	EXPECT_EQ(target->getCount(), expectedPermanent + temporary);
	ASSERT_NO_FATAL_FAILURE(finishAndCheckArmy(expectedPermanent));
}
