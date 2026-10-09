/*
 * NewHorizonsSharedPurposeTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include <vcmi/Environment.h>

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CBattleInfoEssentials.h"
#include "../../../lib/battle/HeroCommand.h"
#include "../../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsPurify.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <string>

namespace
{
constexpr auto divineMandateSkill = "new-horizons:divineMandate";
constexpr auto sharedPurposePerk = "new-horizons:divineMandate.sharedPurpose";


}

class NewHorizonsSharedPurposeTest : public HeroCommandFixture
{
protected:
	CStack * friendly = nullptr;
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
		const auto & perks = perkRules["skills"][std::string(divineMandateSkill)]["perks"].Vector();
		const auto found = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
		{
			return perk["id"].String() == sharedPurposePerk;
		});
		if(found == perks.end() || (*found)["effect"]["status"].String() != "active")
			throw std::runtime_error("Shared Purpose must be shipped active for principal tests");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	SecondarySkill divineMandate() const
	{
		const int decoded = SecondarySkill::decode(divineMandateSkill);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void selectSharedPurpose(CGHeroInstance * hero, MasteryLevel::Type rank)
	{
		hero->setSecSkillLevel(divineMandate(), rank, ChangeValueMode::ABSOLUTE);
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
					&& candidate.selection.perkId == sharedPurposePerk;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(divineMandateSkill, sharedPurposePerk));
			return;
		}

		FAIL() << sharedPurposePerk << " never appeared in a legal Divine Mandate perk offer";
	}

	void prepare(MasteryLevel::Type rank = MasteryLevel::BASIC, bool selectPerk = true,
		int32_t normal = -1, int32_t buffer = 0, bool ordinaryFriendly = false)
	{
		startGame();
		ASSERT_EQ(attackerSideHero->getFactionID(), FactionID::CASTLE);
		if(selectPerk)
			selectSharedPurpose(attackerSideHero, rank);
		else
			attackerSideHero->setSecSkillLevel(divineMandate(), rank, ChangeValueMode::ABSOLUTE);

		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::BLESS));
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::MAGIC_ARROW));
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::decode("new-horizons:massBless")));
		const int32_t capacity = attackerSideHero->manaLimit();
		ASSERT_GT(capacity, 0);
		attackerSideHero->initializeSpellPoints(normal < 0 ? capacity : normal, buffer);

		if(ordinaryFriendly)
		{
			attackerSideHero->clearSlots();
			ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 100));
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
		{
			if(ordinaryFriendly && unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0))
				friendly = const_cast<CStack *>(dynamic_cast<const CStack *>(unit));
			else
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		}
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);

		if(ordinaryFriendly)
		{
			ASSERT_NE(friendly, nullptr);
			ASSERT_EQ(friendly->unitSlot(), SlotID(0));
			ASSERT_FALSE(friendly->isSummoned());
			friendly->setPosition(BattleHex(leftHex));
		}
		else
			friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();
		activate(friendly);
		ASSERT_EQ(battle()->battleActiveUnit(), friendly);
		ASSERT_EQ(battle()->battleGetOwner(friendly), PlayerColor(0));
	}

	void activate(const CStack * stack)
	{
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = stack->unitId();
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}

	bool issue(HeroCommand command)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeHeroCommand(BattleSide::ATTACKER, command));
	}

	bool cast(SpellID spell, const CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool hasPurpose(const battle::Unit * unit) const
	{
		return unit && unit->hasBonus(CSelector(newHorizonsDivineMandate::isSharedPurposeMoraleBonus));
	}

	void addCurse(CStack * unit)
	{
		Bonus curse(BonusDuration::N_TURNS, BonusType::ALWAYS_MINIMUM_DAMAGE,
			BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::CURSE)));
		curse.turnsRemain = 3;
		SetStackEffect effect;
		effect.battleID = BattleID(0);
		effect.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{curse});
		gameHandler->sendAndApply(effect);
	}

	bool hasCurse(const CStack * unit) const
	{
		return !newHorizonsPurify::spellEffectGroupBonuses(unit, SpellID(SpellID::CURSE)).empty();
	}

	bool purify(CStack * selected)
	{
		attackerSideHero->addSpellToSpellbook(newHorizonsPurify::spellID());
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = newHorizonsPurify::spellID();
		action.aimToHex(selected->getPosition());
		action.spellPurifyChoices = {{static_cast<int32_t>(selected->unitId()), SpellID(SpellID::CURSE)}};
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	void makeCorpse()
	{
		auto state = friendly->acquireState();
		auto damage = state->getAvailableHealth();
		state->damage(damage);
		BattleUnitsChanged change;
		change.battleID = BattleID(0);
		change.changedStacks.emplace_back(friendly->unitId(), UnitChanges::EOperation::UPDATE);
		change.changedStacks.back().data = state->save();
		change.changedStacks.back().healthDelta = -damage;
		gameHandler->sendAndApply(change);
		ASSERT_FALSE(friendly->alive());
	}

	void learnResurrection()
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::WISDOM),
			MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::RESURRECTION));
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	}

	int32_t spellCost(SpellID spell) const
	{
		const auto * spellData = spell.toSpell();
		if(!spellData)
			return 0;
		return battle()->battleGetSpellCost(spellData, attackerSideHero);
	}
};


TEST_F(NewHorizonsSharedPurposeTest, ActualOrderThenLightGrantsOnlyCompletedOverlap)
{
	prepare();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_FALSE(hasPurpose(friendly));
	ASSERT_TRUE(cast(SpellID(SpellID::BLESS), friendly));
	EXPECT_TRUE(hasPurpose(friendly));
	EXPECT_FALSE(hasPurpose(enemy));
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 1);
	EXPECT_FALSE(cast(SpellID(SpellID::BLESS), friendly));
	EXPECT_EQ(friendly->getBonuses(CSelector(newHorizonsDivineMandate::isSharedPurposeMoraleBonus))->size(), 1u);
}

TEST_F(NewHorizonsSharedPurposeTest, ActualLightThenOrderUsesOriginalRecipientEvenAfterOtherActions)
{
	prepare();
	ASSERT_TRUE(cast(SpellID(SpellID::BLESS), friendly));
	EXPECT_FALSE(hasPurpose(friendly));
	activate(enemy);
	activate(friendly);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(hasPurpose(friendly));
}

TEST_F(NewHorizonsSharedPurposeTest, OrderThenResurrectionGrantsAfterActualRestoration)
{
	prepare(MasteryLevel::BASIC, true, -1, 0, true);
	learnResurrection();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	makeCorpse();
	auto * other = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(81), 50);
	activate(other);
	ASSERT_TRUE(cast(SpellID(SpellID::RESURRECTION), friendly));
	ASSERT_TRUE(friendly->alive());
	EXPECT_TRUE(hasPurpose(friendly));
	EXPECT_FALSE(hasPurpose(other));
}

TEST_F(NewHorizonsSharedPurposeTest, FirstResurrectionCapturesCorpseForLaterOrderOverlap)
{
	prepare(MasteryLevel::BASIC, true, -1, 0, true);
	learnResurrection();
	makeCorpse();
	auto * other = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(81), 50);
	activate(other);
	ASSERT_TRUE(cast(SpellID(SpellID::RESURRECTION), friendly));
	ASSERT_TRUE(friendly->alive());
	EXPECT_FALSE(hasPurpose(friendly));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(hasPurpose(friendly));
}

TEST_F(NewHorizonsSharedPurposeTest, DifferentFriendlyRecipientsDoNotQualify)
{
	prepare();
	auto * guard = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(81), 50);
	auto * ward = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(82), 50);
	ASSERT_NE(guard, nullptr);
	ASSERT_NE(ward, nullptr);
	ASSERT_TRUE(cast(SpellID(SpellID::BLESS), friendly));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			guard->unitId(), ward->unitId())));
	EXPECT_FALSE(hasPurpose(friendly));
	EXPECT_FALSE(hasPurpose(guard));
	EXPECT_FALSE(hasPurpose(ward));
}

TEST_F(NewHorizonsSharedPurposeTest, UnselectedCompletedPairRetainsOrdinaryDivineMandate)
{
	prepare(MasteryLevel::BASIC, false);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_TRUE(cast(SpellID(SpellID::BLESS), friendly));
	EXPECT_FALSE(hasPurpose(friendly));
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 1);
}

TEST_F(NewHorizonsSharedPurposeTest, ActualMassLightCapturesAllEligibleRecipientsButNotImmunity)
{
	prepare();
	const SecondarySkill light(SecondarySkill::decode("new-horizons:lightMagic"));
	attackerSideHero->setSecSkillLevel(light, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:lightMagic", "new-horizons:lightMagic.benediction"});
	attackerSideHero->setSecSkillLevel(light, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:lightMagic", "new-horizons:lightMagic.litany"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.litany"));
	auto * other = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(81), 50);
	auto * immune = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(82), 50);
	const SpellID mass(SpellID::decode("new-horizons:massBless"));
	ASSERT_NE(mass, SpellID::NONE);
	immune->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(mass)));
	ASSERT_TRUE(cast(mass, friendly));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(hasPurpose(friendly));
	EXPECT_TRUE(hasPurpose(other));
	EXPECT_FALSE(hasPurpose(immune));
	EXPECT_FALSE(hasPurpose(enemy));
}

TEST_F(NewHorizonsSharedPurposeTest, CounterspelledLightCompletesPairButEarnsNoMorale)
{
	prepare();
	defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	defenderSideHero->initializeSpellPoints(defenderSideHero->manaLimit(), 0);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	ASSERT_TRUE(cast(SpellID(SpellID::BLESS), friendly));
	EXPECT_FALSE(hasPurpose(friendly));
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 1);
}

TEST_F(NewHorizonsSharedPurposeTest, PurifyThenOrderUsesActuallyRemovedSelectedGroupsNotWholeArea)
{
	prepare();
	auto * unselected = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex - 1), 50);
	addCurse(friendly);
	addCurse(unselected);
	ASSERT_TRUE(purify(friendly));
	EXPECT_FALSE(hasCurse(friendly));
	EXPECT_TRUE(hasCurse(unselected));
	EXPECT_FALSE(hasPurpose(friendly));
	const auto & ledger = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto pending = std::find_if(ledger.grants.begin(), ledger.grants.end(), [](const auto & grant)
	{
		return grant.source == HeroActionAllowanceState::GrantSource::DIVINE_MANDATE;
	});
	ASSERT_NE(pending, ledger.grants.end());
	EXPECT_EQ(pending->divineMandateRecipients, std::vector<uint32_t>{friendly->unitId()});
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(hasPurpose(friendly));
	EXPECT_FALSE(hasPurpose(unselected));
}

TEST_F(NewHorizonsSharedPurposeTest, OrderThenPurifyPublishesAfterSelectedRemovalWithoutAreaFalsePositive)
{
	prepare();
	auto * unselected = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex - 1), 50);
	addCurse(friendly);
	addCurse(unselected);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_TRUE(purify(friendly));
	EXPECT_FALSE(hasCurse(friendly));
	EXPECT_TRUE(hasCurse(unselected));
	EXPECT_TRUE(hasPurpose(friendly));
	EXPECT_FALSE(hasPurpose(unselected));
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 1);
}

TEST_F(NewHorizonsSharedPurposeTest, BenefitExpiresAtNextCreatureActivationNotAtRoundBoundary)
{
	prepare();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_TRUE(cast(SpellID(SpellID::BLESS), friendly));
	ASSERT_TRUE(hasPurpose(friendly));
	advanceRound();
	EXPECT_TRUE(hasPurpose(friendly));
	activate(friendly);
	EXPECT_FALSE(hasPurpose(friendly));
}

TEST_F(NewHorizonsSharedPurposeTest, ExpiredUnusedPairDoesNotOverlapNewRoundOrder)
{
	prepare();
	ASSERT_TRUE(cast(SpellID(SpellID::BLESS), friendly));
	ASSERT_TRUE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).pendingFollowup);
	advanceRound();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_FALSE(hasPurpose(friendly));
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 0);
	ASSERT_TRUE(cast(SpellID(SpellID::BLESS), friendly));
	EXPECT_TRUE(hasPurpose(friendly));
}

namespace
{
class SharedPurposeEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit SharedPurposeEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

TEST_F(NewHorizonsSharedPurposeTest, DetachedAcceptedPairMirrorsLiveWithoutChangingParentOrSibling)
{
	prepare();
	ASSERT_TRUE(cast(SpellID(SpellID::BLESS), friendly));
	SharedPurposeEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto prepared = child->prepareHeroOrderAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(prepared);
	ASSERT_TRUE(child->beginProjectedHeroAction(BattleSide::ATTACKER, *prepared));
	ASSERT_TRUE(child->projectAcceptedHeroOrder(BattleSide::ATTACKER, HeroCommand::CHARGE, {}, *prepared));
	EXPECT_TRUE(hasPurpose(child->battleGetUnitByID(friendly->unitId())));
	EXPECT_FALSE(hasPurpose(parent->battleGetUnitByID(friendly->unitId())));
	EXPECT_FALSE(hasPurpose(sibling->battleGetUnitByID(friendly->unitId())));
	EXPECT_FALSE(hasPurpose(friendly));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(hasPurpose(friendly));
	EXPECT_EQ(child->battleGetMorale(child->battleGetUnitByID(friendly->unitId())), battle()->battleGetMorale(friendly));
}

TEST_F(NewHorizonsSharedPurposeTest, ExistingPurposeParentChildActivationKeepsSiblingParentAndLiveIndependent)
{
	prepare();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_TRUE(cast(SpellID(SpellID::BLESS), friendly));
	ASSERT_TRUE(hasPurpose(friendly));
	SharedPurposeEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	ASSERT_TRUE(hasPurpose(parent->getForUpdate(friendly->unitId()).get()));
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	ASSERT_TRUE(hasPurpose(child->getForUpdate(friendly->unitId()).get()));
	ASSERT_TRUE(hasPurpose(sibling->getForUpdate(friendly->unitId()).get()));
	child->nextTurn(friendly->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(hasPurpose(child->battleGetUnitByID(friendly->unitId())));
	EXPECT_TRUE(hasPurpose(sibling->battleGetUnitByID(friendly->unitId())));
	EXPECT_TRUE(hasPurpose(parent->battleGetUnitByID(friendly->unitId())));
	EXPECT_TRUE(hasPurpose(friendly));
	activate(friendly);
	EXPECT_FALSE(hasPurpose(friendly));
	EXPECT_TRUE(hasPurpose(parent->battleGetUnitByID(friendly->unitId())));
	EXPECT_TRUE(hasPurpose(sibling->battleGetUnitByID(friendly->unitId())));
}

TEST(NewHorizonsSharedPurposeProtocolTest, GrantReceiptRoundTripLegacyDefaultsAndPreprefixRejection)
{
	using Ledger = HeroActionAllowanceState;
	Ledger ledger;
	ledger.resetForRound(1);
	const auto selected = ledger.eligibleAllowance(Ledger::ActionKind::SPELL, 1);
	ASSERT_TRUE(selected);
	const auto receipt = ledger.consumeAllowance(selected->grantId, Ledger::ActionKind::SPELL, 1);
	ASSERT_TRUE(receipt);
	EXPECT_TRUE(DivineMandateTransition::applyAcceptedAction(ledger, *receipt, 1, true, 1, {2, 4}).empty());
	CMemorySerializer current;
	current.oser.version = current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & ledger);
	Ledger restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored, ledger);
	const auto order = restored.eligibleAllowance(Ledger::ActionKind::ORDER, 1);
	ASSERT_TRUE(order);
	const auto second = restored.consumeAllowance(order->grantId, Ledger::ActionKind::ORDER, 1);
	ASSERT_TRUE(second);
	EXPECT_EQ(DivineMandateTransition::applyAcceptedAction(restored, *second, 1, false, 1, {4, 5}),
		std::vector<uint32_t>({4}));
	EXPECT_EQ(restored.divineMandateCompletedPairs, 1);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_VETERAN_COHESION;
	EXPECT_THROW(old.oser & ledger, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	CGameState callback;
	callback.preInit(LIBRARY);
	BattleStart start;
	start.battleID = BattleID(0);
	start.info = std::make_unique<BattleInfo>(&callback);
	start.info->getSide(BattleSide::ATTACKER).heroActionAllowances = ledger;
	CMemorySerializer outer;
	outer.oser.version = ESerializationVersion::NEW_HORIZONS_VETERAN_COHESION;
	EXPECT_THROW(outer.oser & start, std::runtime_error);
	EXPECT_TRUE(outer.extractBuffer().empty());
	ledger.grants.front().divineMandateRecipients.clear();
	CMemorySerializer legacy;
	legacy.oser.version = legacy.iser.version = ESerializationVersion::NEW_HORIZONS_VETERAN_COHESION;
	ASSERT_NO_THROW(legacy.oser & ledger);
	Ledger plain;
	ASSERT_NO_THROW(legacy.iser & plain);
	EXPECT_TRUE(plain.grants.front().divineMandateRecipients.empty());
	auto malformed = ledger;
	malformed.grants.front().divineMandateRecipients = {4, 2};
	EXPECT_THROW(malformed.validateShape(), std::runtime_error);
	malformed.grants.front().divineMandateRecipients = {2, 2};
	EXPECT_THROW(malformed.validateShape(), std::runtime_error);
	malformed.grants.front().divineMandateRecipients = {HeroOrderState::INVALID_UNIT_ID};
	EXPECT_THROW(malformed.validateShape(), std::runtime_error);
	BattleDivineMandateRecipientsChanged capture;
	capture.battleID = BattleID(0);
	capture.side = BattleSide::ATTACKER;
	capture.pendingGrantId = 2;
	capture.originalAction = {1, Ledger::ActionKind::SPELL, Ledger::AllowanceKind::HERO, Ledger::GrantSource::ROUND, 1};
	capture.recipients = {2, 4};
	CMemorySerializer captureCurrent;
	captureCurrent.oser.version = captureCurrent.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(captureCurrent.oser & capture);
	BattleDivineMandateRecipientsChanged restoredCapture;
	ASSERT_NO_THROW(captureCurrent.iser & restoredCapture);
	EXPECT_EQ(restoredCapture.originalAction, capture.originalAction);
	EXPECT_EQ(restoredCapture.recipients, capture.recipients);
	EXPECT_EQ(restoredCapture.pendingGrantId, capture.pendingGrantId);
	CMemorySerializer captureOld;
	captureOld.oser.version = ESerializationVersion::NEW_HORIZONS_VETERAN_COHESION;
	EXPECT_THROW(captureOld.oser & capture, std::runtime_error);
	EXPECT_TRUE(captureOld.extractBuffer().empty());
	capture.recipients = {};
	EXPECT_THROW(capture.validateShape(), std::runtime_error);
	capture.recipients = {2, 2};
	EXPECT_THROW(capture.validateShape(), std::runtime_error);
	capture.recipients = {HeroOrderState::INVALID_UNIT_ID};
	EXPECT_THROW(capture.validateShape(), std::runtime_error);
}
