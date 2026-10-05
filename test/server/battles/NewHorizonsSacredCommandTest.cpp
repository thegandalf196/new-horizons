/*
 * NewHorizonsSacredCommandTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CBattleInfoEssentials.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/HeroActionAllowanceState.h"
#include "../../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/gameState/GameStatePackVisitor.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include <vcmi/Environment.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

namespace
{
constexpr auto divineMandateSkill = "new-horizons:divineMandate";
constexpr auto sacredCommandPerk = "new-horizons:divineMandate.sacredCommand";

bool activateSacredCommand(JsonNode & rules)
{
	auto & perks = rules["skills"][std::string(divineMandateSkill)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
	{
		return perk["id"].String() == sacredCommandPerk;
	});
	if(found == perks.end())
		return false;
	(*found)["effect"]["status"].String() = "active";
	return true;
}

JsonNode coefficientFormula(double base, double attack, double defense)
{
	JsonNode formula;
	formula["base"].Float() = base;
	formula["attack"].Float() = attack;
	formula["defense"].Float() = defense;
	return formula;
}

class SacredCommandPredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit SacredCommandPredictionEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class NewHorizonsSacredCommandTest : public HeroCommandFixture
{
protected:
	const CStack * friendly = nullptr;
	CStack * chargingStack = nullptr;
	CStack * enemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(!activateSacredCommand(perkRules))
			throw std::runtime_error("Missing Sacred Command from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	SecondarySkill divineMandate() const
	{
		const int decoded = SecondarySkill::decode(divineMandateSkill);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void selectSacredCommand(CGHeroInstance * hero)
	{
		hero->setSecSkillLevel(divineMandate(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [](const auto & candidate)
			{
				return candidate.selection.skillId == divineMandateSkill
					&& candidate.selection.perkId == sacredCommandPerk;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(divineMandateSkill, sacredCommandPerk));
			return;
		}

		FAIL() << sacredCommandPerk << " never appeared in a legal Basic perk offer";
	}

	void prepare(bool selectPerk)
	{
		startGame();
		ASSERT_EQ(attackerSideHero->getFactionID(), FactionID::CASTLE);
		attackerSideHero->setSecSkillLevel(divineMandate(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(selectPerk)
			selectSacredCommand(attackerSideHero);
		else
			ASSERT_FALSE(attackerSideHero->hasActivePerk(divineMandateSkill, sacredCommandPerk));

		attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 50, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 10, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::BLESS));
		setTestSpellPointTotal(attackerSideHero, attackerSideHero->manaLimit());

		startBattle();
		beginCombat();
		const auto * activeUnit = battle()->battleActiveUnit();
		ASSERT_NE(activeUnit, nullptr);
		ASSERT_EQ(battle()->battleGetOwner(activeUnit), PlayerColor(0));

		friendly = battle()->battleGetStackByID(activeUnit->unitId());
		ASSERT_NE(friendly, nullptr);
		chargingStack = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(71), 100);
		ASSERT_NE(chargingStack, nullptr);
		ASSERT_NE(enemy, nullptr);
	}

	bool issue(HeroCommand command)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeHeroCommand(BattleSide::ATTACKER, command));
	}

	bool castBless()
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::BLESS;
		action.aimToUnit(friendly);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	const JsonNode & chargeFormula() const
	{
		return battle()->getHeroCommandRules()["commands"]["charge"]["effects"]["meleeDamagePercent"];
	}

	int64_t chargeDamage() const
	{
		return battle()->calculateDmgRange(BattleAttackInfo(chargingStack, enemy, 3, false)).damage.min;
	}

	int64_t projectedChargeDamage(int32_t sacredCommandPercent)
	{
		auto order = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
		if(!order)
			return -1;
		order->sacredCommandEfficiencyBonusPercent = sacredCommandPercent;

		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		SacredCommandPredictionEnvironment environment(gameState());
		HypotheticBattle projected(&environment, callback);
		projected.setHeroOrderState(BattleSide::ATTACKER, order);
		const auto * projectedAttacker = projected.battleGetUnitByID(chargingStack->unitId());
		const auto * projectedDefender = projected.battleGetUnitByID(enemy->unitId());
		if(!projectedAttacker || !projectedDefender)
			return -1;
		return projected.calculateDmgRange(
			BattleAttackInfo(projectedAttacker, projectedDefender, 3, false)).damage.min;
	}
};

TEST_F(NewHorizonsSacredCommandTest, LightFirstSacredOrderSnapshotsBonusAndChangesItsLaterCombatEffect)
{
	prepare(true);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(divineMandateSkill, sacredCommandPerk));
	EXPECT_EQ(newHorizonsDivineMandate::sacredCommandEfficiencyBonusPercent(attackerSideHero), 10);
	const int64_t ordinaryDamage = chargeDamage();

	ASSERT_TRUE(castBless());
	const auto pending = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
	ASSERT_TRUE(pending.pendingFollowup);
	EXPECT_EQ(pending.pendingFollowup->source, HeroActionAllowanceState::GrantSource::DIVINE_MANDATE);
	EXPECT_EQ(pending.pendingFollowup->allowance, HeroActionAllowanceState::AllowanceKind::ORDER);

	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto order = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(order);
	EXPECT_EQ(order->command, HeroCommand::CHARGE);
	EXPECT_EQ(order->sacredCommandEfficiencyBonusPercent, 10);
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 1);
	EXPECT_FALSE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).pendingFollowup);

	const int ordinaryCoefficient = heroCommands::coefficient(chargeFormula(), *attackerSideHero,
		order->warcastingBonusPercent, 0);
	const int sacredCoefficient = heroCommands::coefficient(chargeFormula(), *attackerSideHero,
		order->warcastingBonusPercent, order->sacredCommandEfficiencyBonusPercent);
	EXPECT_EQ(sacredCoefficient, ordinaryCoefficient + 1);
	const int64_t capturedOrderDamage = chargeDamage();
	EXPECT_GT(capturedOrderDamage, ordinaryDamage)
		<< "The accepted Light-first Divine Order changes the later live Charge damage calculation";

	const int64_t ordinaryOrderDamage = projectedChargeDamage(0);
	ASSERT_GE(ordinaryOrderDamage, 0);
	EXPECT_GT(capturedOrderDamage, ordinaryOrderDamage)
		<< "An AI-style detached battle with the same Order but no Sacred snapshot predicts less damage";
	EXPECT_EQ(battle()->battleGetHeroOrderState(BattleSide::ATTACKER)->sacredCommandEfficiencyBonusPercent, 10)
		<< "The detached comparison does not mutate the live issued Order";

	const auto flatOnly = coefficientFormula(37, 0, 0);
	EXPECT_EQ(heroCommands::coefficient(flatOnly, *attackerSideHero, 0, 0), 37);
	EXPECT_EQ(heroCommands::coefficient(flatOnly, *attackerSideHero, 0, 10), 37)
		<< "Sacred Command scales only Attack/Defense-derived Order components";

	const auto currentOrders = battle()->battleGetHeroOrderStates(BattleSide::ATTACKER);
	ASSERT_EQ(currentOrders.size(), 1u);
	BattleHeroOrderStateChanged forgedUpdate;
	forgedUpdate.battleID = battle()->getBattleID();
	forgedUpdate.side = BattleSide::ATTACKER;
	forgedUpdate.states = currentOrders;
	forgedUpdate.states->front().sacredCommandEfficiencyBonusPercent = 0;
	forgedUpdate.state = forgedUpdate.states->back();
	BattleStatePackVisitor visitor(*battle());
	EXPECT_THROW(visitor.visitBattleHeroOrderStateChanged(forgedUpdate), std::runtime_error);
	EXPECT_EQ(battle()->battleGetHeroOrderState(BattleSide::ATTACKER)->sacredCommandEfficiencyBonusPercent, 10)
		<< "An update cannot rewrite the issued Order's captured efficiency snapshot";

	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	ASSERT_TRUE(restored->getHeroOrderState(BattleSide::ATTACKER));
	EXPECT_EQ(restored->getHeroOrderState(BattleSide::ATTACKER)->sacredCommandEfficiencyBonusPercent, 10)
		<< "The enclosing battle save retains the captured Order descriptor";

	constexpr auto previousVersion = ESerializationVersion::NEW_HORIZONS_ELEMENTAL_REBIRTH;
	CMemorySerializer oldSideWriter;
	oldSideWriter.oser.version = previousVersion;
	EXPECT_THROW(oldSideWriter.oser & battle()->getSide(BattleSide::ATTACKER), std::runtime_error);
	EXPECT_TRUE(oldSideWriter.extractBuffer().empty())
		<< "SideInBattle refuses to discard Sacred Command before emitting bytes";

	CMemorySerializer oldBattleWriter;
	oldBattleWriter.oser.version = previousVersion;
	EXPECT_THROW(oldBattleWriter.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(oldBattleWriter.extractBuffer().empty())
		<< "BattleInfo refuses to discard Sacred Command before emitting bytes";

	BattleStart startSnapshot;
	startSnapshot.battleID = BattleID(0);
	ASSERT_NO_THROW(startSnapshot.info = CMemorySerializer::deepCopy(*battle(), gameState().get()));
	CMemorySerializer oldStartWriter;
	oldStartWriter.oser.version = previousVersion;
	EXPECT_THROW(oldStartWriter.oser & startSnapshot, std::runtime_error);
	EXPECT_TRUE(oldStartWriter.extractBuffer().empty())
		<< "BattleStart refuses to discard Sacred Command before emitting bytes";

	attackerSideHero->setSecSkillLevel(divineMandate(), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsDivineMandate::sacredCommandEfficiencyBonusPercent(attackerSideHero), 0);
	EXPECT_EQ(battle()->battleGetHeroOrderState(BattleSide::ATTACKER)->sacredCommandEfficiencyBonusPercent, 10);
	EXPECT_EQ(chargeDamage(), capturedOrderDamage)
		<< "The spent Order uses its stored value, not a later live perk lookup";
}

TEST_F(NewHorizonsSacredCommandTest, AnOrdinaryHeroOrderIsNotBoostedByTheSelectedPerk)
{
	prepare(true);
	const int64_t ordinaryDamage = chargeDamage();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));

	const auto order = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(order);
	EXPECT_EQ(order->sacredCommandEfficiencyBonusPercent, 0);
	EXPECT_EQ(heroCommands::coefficient(chargeFormula(), *attackerSideHero,
		order->warcastingBonusPercent, order->sacredCommandEfficiencyBonusPercent),
		heroCommands::coefficient(chargeFormula(), *attackerSideHero, order->warcastingBonusPercent, 0));
	EXPECT_TRUE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).pendingFollowup)
		<< "An ordinary Hero Order may open the paired Light Spell opportunity without receiving Sacred Command";
	const int64_t ordinaryOrderDamage = chargeDamage();
	EXPECT_GT(ordinaryOrderDamage, ordinaryDamage)
		<< "The ordinary Order still applies its baseline Charge effect";
	EXPECT_EQ(ordinaryOrderDamage, projectedChargeDamage(0))
		<< "The live unboosted Order matches its ordinary detached-AI calculation";
}

TEST_F(NewHorizonsSacredCommandTest, UnselectedPerkLeavesTheAcceptedLightFirstFollowupUnchanged)
{
	prepare(false);
	EXPECT_EQ(newHorizonsDivineMandate::sacredCommandEfficiencyBonusPercent(attackerSideHero), 0);
	const int64_t ordinaryDamage = chargeDamage();
	ASSERT_TRUE(castBless());
	ASSERT_TRUE(issue(HeroCommand::CHARGE));

	const auto order = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(order);
	EXPECT_EQ(order->sacredCommandEfficiencyBonusPercent, 0);
	EXPECT_EQ(heroCommands::coefficient(chargeFormula(), *attackerSideHero,
		order->warcastingBonusPercent, order->sacredCommandEfficiencyBonusPercent),
		heroCommands::coefficient(chargeFormula(), *attackerSideHero, order->warcastingBonusPercent, 0));
	EXPECT_GT(chargeDamage(), ordinaryDamage)
		<< "The ordinary Charge still applies; the unselected perk adds no extra efficiency";
}
