/*
 * NewHorizonsKnightlySequenceTest.cpp, part of VCMI engine
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
#include "../../../lib/battle/SideInBattle.h"
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
constexpr auto knightlySequencePerk = "new-horizons:divineMandate.knightlySequence";
constexpr auto sacredCommandPerk = "new-horizons:divineMandate.sacredCommand";
constexpr auto consecratedCastingPerk = "new-horizons:divineMandate.consecratedCasting";
constexpr auto wisdomSkill = "new-horizons:wisdom";

bool activatePerk(JsonNode & rules, const std::string & perkId)
{
	auto & perks = rules["skills"][std::string(divineMandateSkill)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [&perkId](const JsonNode & perk)
	{
		return perk["id"].String() == perkId;
	});
	if(found == perks.end())
		return false;
	(*found)["effect"]["status"].String() = "active";
	return true;
}

class KnightlySequencePredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit KnightlySequencePredictionEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class NewHorizonsKnightlySequenceTest : public HeroCommandFixture
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
		if(!activatePerk(perkRules, knightlySequencePerk)
			|| !activatePerk(perkRules, sacredCommandPerk)
			|| !activatePerk(perkRules, consecratedCastingPerk))
			throw std::runtime_error("Missing Knightly Sequence or Basic Divine Mandate perk in the registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	SecondarySkill divineMandate() const
	{
		const int decoded = SecondarySkill::decode(divineMandateSkill);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	SecondarySkill wisdom() const
	{
		const int decoded = SecondarySkill::decode(wisdomSkill);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void selectPerk(CGHeroInstance * hero, MasteryLevel::Type rank, const std::string & perkId)
	{
		hero->setSecSkillLevel(divineMandate(), rank, ChangeValueMode::ABSOLUTE);
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [&perkId, rank](const auto & candidate)
			{
				return candidate.selection.skillId == divineMandateSkill
					&& candidate.selection.perkId == perkId
					&& candidate.requiredRank == rank;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(divineMandateSkill, perkId));
			return;
		}

		FAIL() << perkId << " never appeared in a legal " << rank << " Divine Mandate perk offer";
	}

	void prepare(bool selectKnightly, bool selectSacred, int32_t bufferMana = 0)
	{
		startGame();
		ASSERT_EQ(attackerSideHero->getFactionID(), FactionID::CASTLE);
		attackerSideHero->setSecSkillLevel(divineMandate(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(selectSacred)
			selectPerk(attackerSideHero, MasteryLevel::BASIC, sacredCommandPerk);
		else if(selectKnightly)
			selectPerk(attackerSideHero, MasteryLevel::BASIC, consecratedCastingPerk);

		attackerSideHero->setSecSkillLevel(divineMandate(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		if(selectKnightly)
			selectPerk(attackerSideHero, MasteryLevel::ADVANCED, knightlySequencePerk);
		attackerSideHero->setSecSkillLevel(wisdom(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);

		attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 50, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 10, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::BLESS));
		const int32_t manaCapacity = attackerSideHero->manaLimit();
		ASSERT_GT(manaCapacity, 0);
		attackerSideHero->initializeSpellPoints(manaCapacity, bufferMana);

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

	bool issueCharge()
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
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

	int32_t spellCost() const
	{
		return battle()->battleGetSpellCost(SpellID(SpellID::BLESS).toSpell(), attackerSideHero);
	}

	const JsonNode & chargeFormula() const
	{
		return battle()->getHeroCommandRules()["commands"]["charge"]["effects"]["meleeDamagePercent"];
	}

	int64_t chargeDamage() const
	{
		return battle()->calculateDmgRange(BattleAttackInfo(chargingStack, enemy, 3, false)).damage.min;
	}

	int64_t projectedChargeDamage(int32_t sacredPercent, int32_t knightlyPercent)
	{
		auto order = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
		if(!order)
			return -1;
		order->sacredCommandEfficiencyBonusPercent = sacredPercent;
		order->knightlySequenceEfficiencyBonusPercent = knightlyPercent;

		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		KnightlySequencePredictionEnvironment environment(gameState());
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

TEST_F(NewHorizonsKnightlySequenceTest, OrderFirstReducesItsAcceptedLightFollowupCostAfterWisdom)
{
	prepare(true, false, 20);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(divineMandateSkill, knightlySequencePerk));
	EXPECT_EQ(newHorizonsDivineMandate::knightlySequenceOrderBonusPercent(attackerSideHero), 5);
	EXPECT_EQ(newHorizonsDivineMandate::knightlySequenceSpellCostReduction(attackerSideHero), 2);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(wisdom()), MasteryLevel::EXPERT);
	const int32_t normalCost = spellCost();
	ASSERT_GE(normalCost, 3) << "Expert Wisdom is included before Knightly Sequence's reduction";
	const int32_t expectedSequenceCost = std::max(1, normalCost - 2);
	const int32_t normalMana = attackerSideHero->getNormalSpellPoints();
	const int32_t bufferMana = attackerSideHero->getBufferSpellPoints();
	ASSERT_EQ(bufferMana, 20);

	ASSERT_TRUE(issueCharge());
	const auto pending = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
	ASSERT_TRUE(pending.pendingFollowup);
	EXPECT_EQ(pending.pendingFollowup->allowance, HeroActionAllowanceState::AllowanceKind::SPELL);
	const int32_t discountedCost = spellCost();
	EXPECT_EQ(discountedCost, expectedSequenceCost);
	EXPECT_EQ(discountedCost, 1) << "The Wisdom-adjusted Bless cost reaches the final one-Mana floor";
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), normalMana);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferMana);

	ASSERT_TRUE(castBless());
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), normalMana)
		<< "The accepted cast spends Buffer Mana before Normal Mana";
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferMana - discountedCost)
		<< "The actual accepted follow-up pays the shared discounted cost";
	ASSERT_FALSE(server.casts.empty());
	EXPECT_EQ(server.casts.back().announcement.paidHeroManaCost, discountedCost);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).acceptedHeroManaSpent, discountedCost);
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 1);
	const auto order = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(order);
	EXPECT_EQ(order->knightlySequenceEfficiencyBonusPercent, 0)
		<< "An Order performed first does not receive the spell-first efficiency increase";
}

TEST_F(NewHorizonsKnightlySequenceTest, LightFirstKnightlyOrderComposesWithSacredAndMatchesDetachedPrediction)
{
	prepare(true, true);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(divineMandateSkill, sacredCommandPerk));
	ASSERT_TRUE(attackerSideHero->hasActivePerk(divineMandateSkill, knightlySequencePerk));
	EXPECT_EQ(newHorizonsDivineMandate::sacredCommandEfficiencyBonusPercent(attackerSideHero), 10);
	EXPECT_EQ(newHorizonsDivineMandate::knightlySequenceOrderBonusPercent(attackerSideHero), 5);

	const int64_t ordinaryDamage = chargeDamage();
	const int32_t firstSpellCost = spellCost();
	const int32_t initialMana = attackerSideHero->getNormalSpellPoints();
	ASSERT_TRUE(castBless());
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), initialMana - firstSpellCost)
		<< "The initial ordinary Hero Spell is not treated as a discounted follow-up";
	EXPECT_EQ(spellCost(), firstSpellCost)
		<< "A pending Order continuation does not receive a Spell follow-up discount";

	const auto pending = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
	ASSERT_TRUE(pending.pendingFollowup);
	EXPECT_EQ(pending.pendingFollowup->allowance, HeroActionAllowanceState::AllowanceKind::ORDER);
	ASSERT_TRUE(issueCharge());

	const auto order = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(order);
	EXPECT_EQ(order->sacredCommandEfficiencyBonusPercent, 10);
	EXPECT_EQ(order->knightlySequenceEfficiencyBonusPercent, 5);
	EXPECT_EQ(order->divineMandateEfficiencyBonusPercent(), 15);
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 1);

	const int sacredOnlyCoefficient = heroCommands::coefficient(chargeFormula(), *attackerSideHero,
		order->warcastingBonusPercent, order->sacredCommandEfficiencyBonusPercent);
	const int combinedCoefficient = heroCommands::coefficient(chargeFormula(), *attackerSideHero,
		order->warcastingBonusPercent, order->divineMandateEfficiencyBonusPercent());
	EXPECT_GT(combinedCoefficient, sacredOnlyCoefficient)
		<< "Knightly Sequence contributes its distinct five percentage points alongside Sacred Command";

	const int64_t combinedDamage = chargeDamage();
	EXPECT_GT(combinedDamage, ordinaryDamage);
	const int64_t sacredOnlyDamage = projectedChargeDamage(10, 0);
	ASSERT_GE(sacredOnlyDamage, 0);
	EXPECT_GT(combinedDamage, sacredOnlyDamage)
		<< "The accepted Order's Knightly snapshot changes real damage beyond Sacred Command alone";
	EXPECT_EQ(combinedDamage, projectedChargeDamage(10, 5))
		<< "The detached battle predicts the same combined Order effect";
	EXPECT_EQ(battle()->battleGetHeroOrderState(BattleSide::ATTACKER)->knightlySequenceEfficiencyBonusPercent, 5)
		<< "Detached calculations do not mutate the live issued snapshot";

	const auto currentOrders = battle()->battleGetHeroOrderStates(BattleSide::ATTACKER);
	ASSERT_EQ(currentOrders.size(), 1u);
	BattleHeroOrderStateChanged forgedUpdate;
	forgedUpdate.battleID = battle()->getBattleID();
	forgedUpdate.side = BattleSide::ATTACKER;
	forgedUpdate.states = currentOrders;
	forgedUpdate.states->front().knightlySequenceEfficiencyBonusPercent = 0;
	forgedUpdate.state = forgedUpdate.states->back();
	BattleStatePackVisitor visitor(*battle());
	EXPECT_THROW(visitor.visitBattleHeroOrderStateChanged(forgedUpdate), std::runtime_error);
	EXPECT_EQ(battle()->battleGetHeroOrderState(BattleSide::ATTACKER)->knightlySequenceEfficiencyBonusPercent, 5)
		<< "An update cannot rewrite the accepted Knightly Sequence snapshot";

	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	ASSERT_TRUE(restored->getHeroOrderState(BattleSide::ATTACKER));
	EXPECT_EQ(restored->getHeroOrderState(BattleSide::ATTACKER)->divineMandateEfficiencyBonusPercent(), 15)
		<< "The current battle save retains both separately captured efficiency increments";

	constexpr auto previousVersion = ESerializationVersion::NEW_HORIZONS_SACRED_COMMAND;
	CMemorySerializer oldSideWriter;
	oldSideWriter.oser.version = previousVersion;
	EXPECT_THROW(oldSideWriter.oser & battle()->getSide(BattleSide::ATTACKER), std::runtime_error);
	EXPECT_TRUE(oldSideWriter.extractBuffer().empty());

	CMemorySerializer oldBattleWriter;
	oldBattleWriter.oser.version = previousVersion;
	EXPECT_THROW(oldBattleWriter.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(oldBattleWriter.extractBuffer().empty());

	BattleStart startSnapshot;
	startSnapshot.battleID = BattleID(0);
	ASSERT_NO_THROW(startSnapshot.info = CMemorySerializer::deepCopy(*battle(), gameState().get()));
	CMemorySerializer oldStartWriter;
	oldStartWriter.oser.version = previousVersion;
	EXPECT_THROW(oldStartWriter.oser & startSnapshot, std::runtime_error);
	EXPECT_TRUE(oldStartWriter.extractBuffer().empty());

	attackerSideHero->setSecSkillLevel(divineMandate(), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsDivineMandate::knightlySequenceOrderBonusPercent(attackerSideHero), 0);
	EXPECT_EQ(battle()->battleGetHeroOrderState(BattleSide::ATTACKER)->divineMandateEfficiencyBonusPercent(), 15);
	EXPECT_EQ(chargeDamage(), combinedDamage)
		<< "Changing the hero's current skill does not retroactively change the issued Order";

	JsonNode fixedOnlyFormula;
	fixedOnlyFormula["base"].Float() = 37;
	fixedOnlyFormula["attack"].Float() = 0;
	fixedOnlyFormula["defense"].Float() = 0;
	EXPECT_EQ(heroCommands::coefficient(fixedOnlyFormula, *attackerSideHero, 0, 15), 37)
		<< "The combined efficiency applies only to attribute-derived terms, not the fixed base";
}

TEST_F(NewHorizonsKnightlySequenceTest, UnselectedKnightlySequenceLeavesTheDivineMandateSpellCostUnchanged)
{
	prepare(false, false);
	EXPECT_EQ(newHorizonsDivineMandate::knightlySequenceSpellCostReduction(attackerSideHero), 0);
	const int32_t normalCost = spellCost();
	const int32_t initialMana = attackerSideHero->getNormalSpellPoints();
	ASSERT_TRUE(issueCharge());
	ASSERT_TRUE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).pendingFollowup);
	EXPECT_EQ(spellCost(), normalCost);
	ASSERT_TRUE(castBless());
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), initialMana - normalCost);
	ASSERT_FALSE(server.casts.empty());
	EXPECT_EQ(server.casts.back().announcement.paidHeroManaCost, normalCost);
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 1);
	const auto order = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(order);
	EXPECT_EQ(order->knightlySequenceEfficiencyBonusPercent, 0);
}
