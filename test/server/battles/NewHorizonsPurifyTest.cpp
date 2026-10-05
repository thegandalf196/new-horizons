/*
 * NewHorizonsPurifyTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/CBattleInfoEssentials.h"
#include "../../../lib/battle/HeroCommand.h"
#include "../../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../../lib/battle/PhysicalAffliction.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsPurify.h"

namespace
{
constexpr auto lightMagicSkill = "new-horizons:lightMagic";
constexpr auto purifierPerk = "new-horizons:lightMagic.purifier";
constexpr auto divineMandateSkill = "new-horizons:divineMandate";
constexpr auto sacredCommandPerk = "new-horizons:divineMandate.sacredCommand";
constexpr auto purifyingMandatePerk = "new-horizons:divineMandate.purifyingMandate";

SpellID purifySpell()
{
	return SpellID(SpellID::decode(std::string(newHorizonsPurify::SPELL_ID)));
}

Bonus spellEffect(SpellID source, BonusType type, int value,
	BonusSubtypeID subtype = BonusSubtypeID())
{
	Bonus result(BonusDuration::N_TURNS, type, BonusSource::SPELL_EFFECT, value,
		BonusSourceID(source), subtype);
	result.turnsRemain = 3;
	return result;
}

class NewHorizonsPurifyTest : public HeroCommandFixture
{
protected:
	int magicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		if(magicVersion != newHorizonsMagic::CURRENT_RULESET_VERSION)
		{
			rules["rulesetVersion"].Integer() = magicVersion;
			rules.Struct().erase("schoolRankPowerCoefficientPercent");
			rules.Struct().erase("spellcraftEfficiencyPercent");
			rules["spells"]["core:quicksand"].Struct().erase("selectedPlacement");
			rules["spells"]["core:earthquake"].Struct().erase("earthquake");
			for(auto & [identity, row] : rules["spells"].Struct())
			{
				(void)identity;
				row.Struct().erase("heroAccess");
				row.Struct().erase("restoration");
				row.Struct().erase("structures");
				if(row.Struct().contains("variant"))
				{
					row.Struct().erase("variant");
					row["active"].Bool() = false;
				}
			}
		}
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void acceptPerkThroughOffer(CGHeroInstance * hero, const std::string_view skillId,
		const std::string_view perkId)
	{
		const auto rankLookup = [hero](const std::string & queriedSkillId)
		{
			return hero->getPerkSkillRank(queriedSkillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [&](const auto & candidate)
			{
				return candidate.selection.skillId == skillId && candidate.selection.perkId == perkId;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(std::string(skillId), std::string(perkId)));
			return;
		}

		FAIL() << perkId << " never appeared in a legal perk offer";
	}

	void selectPurifyingMandate()
	{
		const int skill = SecondarySkill::decode(divineMandateSkill);
		ASSERT_GE(skill, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(skill), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(attackerSideHero, divineMandateSkill, sacredCommandPerk);
		attackerSideHero->setSecSkillLevel(SecondarySkill(skill), MasteryLevel::ADVANCED,
			ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(attackerSideHero, divineMandateSkill, purifyingMandatePerk);
	}

	void prepare(int32_t spellPower = 0, bool purifier = false, bool purifyingMandate = false)
	{
		startGame();
		const int lightMagic = SecondarySkill::decode(lightMagicSkill);
		ASSERT_GE(lightMagic, 0);
		if(purifier)
		{
			attackerSideHero->setSecSkillLevel(SecondarySkill(lightMagic), MasteryLevel::BASIC,
				ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({lightMagicSkill, "new-horizons:lightMagic.healer"});
			attackerSideHero->setSecSkillLevel(SecondarySkill(lightMagic), MasteryLevel::ADVANCED,
				ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({lightMagicSkill, purifierPerk});
		}
		if(purifyingMandate)
			selectPurifyingMandate();
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(purifySpell());
		setTestSpellPointTotal(attackerSideHero, 100);
		startBattle();
		removeDeployedUnits();
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 20);
		outside = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(8, 5), 20);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 20);
		beginCombat();
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(outside, nullptr);
		ASSERT_NE(enemy, nullptr);
		ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), magicVersion);
		ASSERT_EQ(newHorizonsPurify::spellID(), purifySpell());
		ASSERT_EQ(attackerSideHero->hasActivePerk(lightMagicSkill, purifierPerk), purifier);
		ASSERT_EQ(newHorizonsDivineMandate::hasPurifyingMandatePerk(attackerSideHero), purifyingMandate);
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	void addSourceGroup(CStack * unit, SpellID source, int firstValue = -2, bool secondBonus = false)
	{
		std::vector<Bonus> bonuses{spellEffect(source, BonusType::STACKS_SPEED, firstValue)};
		if(secondBonus)
			bonuses.push_back(spellEffect(source, BonusType::PRIMARY_SKILL, -1,
				BonusSubtypeID(PrimarySkill::ATTACK)));
		SetStackEffect effects;
		effects.battleID = BattleID(0);
		effects.toAdd.emplace_back(unit->unitId(), std::move(bonuses));
		gameHandler->sendAndApply(effects);
	}

	void addPhysicalPoison(CStack * unit)
	{
		auto state = unit->acquireState();
		state->physicalPoisonBaseDamage = 20;
		state->physicalPoisonActivationsRemaining = 3;
		state->physicalPoisonSourceStackId = 27;
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		UnitChanges change(unit->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}

	void addMarkedAffliction(CStack * unit, const std::string & kind, const int64_t applicationOrder,
		const int32_t sourceNumber)
	{
		const BonusSourceID sourceID{BonusCustomSource(sourceNumber)};
		Bonus marker(BonusDuration::PERMANENT, BonusType::PHYSICAL_AFFLICTION,
			BonusSource::OTHER, 0, sourceID);
		JsonNode parameters;
		parameters["kind"].String() = kind;
		parameters["applicationOrder"].Integer() = applicationOrder;
		marker.parameters = std::make_shared<BonusParameters>(parameters);

		Bonus effect(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, -1, sourceID);
		SetStackEffect add;
		add.battleID = BattleID(0);
		add.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{effect, marker});
		gameHandler->sendAndApply(add);
	}

	bool hasAffliction(const CStack * unit, const std::string & kind) const
	{
		const auto afflictions = physicalAfflictions::enumerate(*unit);
		return std::any_of(afflictions.begin(), afflictions.end(), [&](const auto & affliction)
		{
			return affliction.kind == kind;
		});
	}

	BattleAction action(BattleHex center, std::vector<std::pair<int32_t, SpellID>> choices = {}) const
	{
		BattleAction result;
		result.actionType = EActionType::HERO_SPELL;
		result.side = BattleSide::ATTACKER;
		result.spell = purifySpell();
		result.aimToHex(center);
		result.spellPurifyChoices = std::move(choices);
		return result;
	}

	bool cast(std::vector<std::pair<int32_t, SpellID>> choices = {}, BattleHex center = BattleHex(3, 5))
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			action(center, std::move(choices)));
	}

	bool hasEffect(const CStack * unit, SpellID source) const
	{
		const auto bonuses = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(source)));
		return bonuses && !bonuses->empty();
	}

	CStack * friendly = nullptr;
	CStack * outside = nullptr;
	CStack * enemy = nullptr;
};
}

TEST(NewHorizonsPurifyHelpersTest, SpellPowerCapChangesAt120)
{
	EXPECT_EQ(newHorizonsPurify::maximumSpellEffectChoices(0), 1);
	EXPECT_EQ(newHorizonsPurify::maximumSpellEffectChoices(119), 1);
	EXPECT_EQ(newHorizonsPurify::maximumSpellEffectChoices(120), 2);
	EXPECT_EQ(newHorizonsPurify::maximumSpellEffectChoices(1000), 2);
}

TEST_F(NewHorizonsPurifyTest, SavedV2RulesDoNotEnablePurify)
{
	magicVersion = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(newHorizonsPurify::enabled(battle()->getMagicRules(), purifySpell()));
	EXPECT_FALSE(cast());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 100);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
}

TEST_F(NewHorizonsPurifyTest, SelectionIsRadiusLimitedAndRemovesOneCompleteSourceGroup)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const BattleHex center(3, 5);
	addSourceGroup(friendly, SpellID(SpellID::CURSE), -2, true);
	addSourceGroup(friendly, SpellID(SpellID::SLOW));
	addSourceGroup(friendly, SpellID(SpellID::BLESS), 2);
	addSourceGroup(outside, SpellID(SpellID::CURSE));

	const auto eligible = newHorizonsPurify::eligibleStacks(*battle(), BattleSide::ATTACKER,
		center, 0, false);
	const auto inRange = std::find_if(eligible.begin(), eligible.end(), [this](const auto & stack)
	{
		return stack.unitId == static_cast<int32_t>(friendly->unitId());
	});
	ASSERT_NE(inRange, eligible.end());
	EXPECT_EQ(inRange->maximumSpellEffectChoices, 1);
	EXPECT_TRUE(vstd::contains(inRange->spellEffectGroups, SpellID(SpellID::CURSE)));
	EXPECT_TRUE(vstd::contains(inRange->spellEffectGroups, SpellID(SpellID::SLOW)));
	EXPECT_FALSE(vstd::contains(inRange->spellEffectGroups, SpellID(SpellID::BLESS)));
	EXPECT_EQ(std::find_if(eligible.begin(), eligible.end(), [this](const auto & stack)
	{
		return stack.unitId == static_cast<int32_t>(outside->unitId());
	}), eligible.end());

	const auto manaBefore = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(cast({{static_cast<int32_t>(outside->unitId()), SpellID(SpellID::CURSE)}}));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(hasEffect(friendly, SpellID(SpellID::CURSE)));
	EXPECT_TRUE(hasEffect(outside, SpellID(SpellID::CURSE)));

	ASSERT_TRUE(cast({{static_cast<int32_t>(friendly->unitId()), SpellID(SpellID::CURSE)}}));
	EXPECT_FALSE(hasEffect(friendly, SpellID(SpellID::CURSE)))
		<< "Both bonuses with one selected source spell are removed as one group";
	EXPECT_TRUE(hasEffect(friendly, SpellID(SpellID::SLOW)));
	EXPECT_TRUE(hasEffect(friendly, SpellID(SpellID::BLESS)));
	EXPECT_TRUE(hasEffect(outside, SpellID(SpellID::CURSE)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 15);
}

TEST_F(NewHorizonsPurifyTest, RejectsNoopForgedPositiveAndOverCapChoicesBeforeSpending)
{
	ASSERT_NO_FATAL_FAILURE(prepare(119));
	addSourceGroup(friendly, SpellID(SpellID::CURSE));
	addSourceGroup(friendly, SpellID(SpellID::SLOW));
	addSourceGroup(friendly, SpellID(SpellID::BLESS), 2);
	const auto manaBefore = attackerSideHero->getManaAvailable();

	EXPECT_FALSE(cast());
	EXPECT_FALSE(cast({{static_cast<int32_t>(friendly->unitId()), SpellID(SpellID::BLESS)}}));
	EXPECT_FALSE(cast({{static_cast<int32_t>(friendly->unitId()), SpellID(SpellID::CURSE)},
		{static_cast<int32_t>(friendly->unitId()), SpellID(SpellID::CURSE)}}));
	EXPECT_FALSE(cast({{static_cast<int32_t>(friendly->unitId()), SpellID(SpellID::CURSE)},
		{static_cast<int32_t>(friendly->unitId()), SpellID(SpellID::SLOW)}}));
	EXPECT_FALSE(cast({{static_cast<int32_t>(enemy->unitId()), SpellID(SpellID::CURSE)}}));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	EXPECT_TRUE(hasEffect(friendly, SpellID(SpellID::CURSE)));
	EXPECT_TRUE(hasEffect(friendly, SpellID(SpellID::SLOW)));
	EXPECT_TRUE(hasEffect(friendly, SpellID(SpellID::BLESS)));

	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 120, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(cast({{static_cast<int32_t>(friendly->unitId()), SpellID(SpellID::CURSE)},
		{static_cast<int32_t>(friendly->unitId()), SpellID(SpellID::SLOW)}}));
	EXPECT_FALSE(hasEffect(friendly, SpellID(SpellID::CURSE)));
	EXPECT_FALSE(hasEffect(friendly, SpellID(SpellID::SLOW)));
	EXPECT_TRUE(hasEffect(friendly, SpellID(SpellID::BLESS)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 15);
}

TEST_F(NewHorizonsPurifyTest, DoesNotRemoveOrderSourcedEffects)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addSourceGroup(friendly, SpellID(SpellID::CURSE));
	Bonus orderBonus(BonusDuration::N_TURNS, BonusType::ADDITIONAL_RETALIATION,
		BonusSource::HERO_COMMAND, 1,
		BonusSourceID(BonusCustomSource(static_cast<int32_t>(HeroCommand::RIPOSTE))));
	orderBonus.turnsRemain = 1;
	SetStackEffect orderEffect;
	orderEffect.battleID = BattleID(0);
	orderEffect.toAdd.emplace_back(friendly->unitId(), std::vector<Bonus>{orderBonus});
	gameHandler->sendAndApply(orderEffect);
	const auto orderBonuses = friendly->getBonusesFrom(BonusSource::HERO_COMMAND);
	ASSERT_NE(orderBonuses, nullptr);
	ASSERT_EQ(orderBonuses->size(), 1u);

	ASSERT_TRUE(cast({{static_cast<int32_t>(friendly->unitId()), SpellID(SpellID::CURSE)}}));
	EXPECT_FALSE(hasEffect(friendly, SpellID(SpellID::CURSE)));
	const auto remainingOrderBonuses = friendly->getBonusesFrom(BonusSource::HERO_COMMAND);
	ASSERT_NE(remainingOrderBonuses, nullptr);
	ASSERT_EQ(remainingOrderBonuses->size(), 1u);
	EXPECT_EQ(remainingOrderBonuses->front()->sid,
		BonusSourceID(BonusCustomSource(static_cast<int32_t>(HeroCommand::RIPOSTE))));
}

TEST_F(NewHorizonsPurifyTest, PhysicalPoisonIsASelectableBaseEffectWithoutPurifier)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addPhysicalPoison(friendly);
	battle::CUnitStateDetached projected(friendly, friendly);
	projected = *friendly;
	EXPECT_TRUE(newHorizonsPurify::hasPhysicalPoison(&projected));
	EXPECT_TRUE(newHorizonsPurify::clearPhysicalPoison(&projected));
	EXPECT_FALSE(newHorizonsPurify::hasPhysicalPoison(&projected));
	EXPECT_GT(friendly->physicalPoisonActivationsRemaining, 0)
		<< "Projection cleanup must leave the authoritative stack untouched";

	const auto eligible = newHorizonsPurify::eligibleStacks(*battle(), BattleSide::ATTACKER,
		BattleHex(3, 5), 0, false);
	ASSERT_EQ(eligible.size(), 1u);
	EXPECT_TRUE(eligible.front().physicalPoison);
	EXPECT_FALSE(eligible.front().physicalPoisonAutomaticallyCleared);
	ASSERT_TRUE(cast({{static_cast<int32_t>(friendly->unitId()),
		newHorizonsPurify::physicalPoisonChoiceID()}}));
	EXPECT_EQ(friendly->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(friendly->physicalPoisonActivationsRemaining, 0);
	EXPECT_EQ(friendly->physicalPoisonSourceStackId, -1);
}

TEST_F(NewHorizonsPurifyTest, PurifierAutomaticallyClearsPhysicalPoisonInAdditionToSelectedGroups)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, true));
	addPhysicalPoison(friendly);
	addSourceGroup(friendly, SpellID(SpellID::CURSE));
	const auto withPurifier = newHorizonsPurify::eligibleStacks(*battle(), BattleSide::ATTACKER,
		BattleHex(3, 5), 0, true);
	ASSERT_EQ(withPurifier.size(), 1u);
	EXPECT_TRUE(withPurifier.front().physicalPoison);
	EXPECT_TRUE(withPurifier.front().physicalPoisonAutomaticallyCleared);
	EXPECT_EQ(withPurifier.front().maximumSpellEffectChoices, 1);
	ASSERT_TRUE(cast({{static_cast<int32_t>(friendly->unitId()), SpellID(SpellID::CURSE)}}));
	EXPECT_EQ(friendly->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(friendly->physicalPoisonActivationsRemaining, 0);
	EXPECT_EQ(friendly->physicalPoisonSourceStackId, -1);
	EXPECT_FALSE(hasEffect(friendly, SpellID(SpellID::CURSE)));
}

TEST_F(NewHorizonsPurifyTest, DivineMandateOrderFollowupPurifyRemovesOneStoredPoisonAffliction)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, false, true));
	addSourceGroup(friendly, SpellID(SpellID::CURSE));
	addPhysicalPoison(friendly);
	addMarkedAffliction(friendly, "disease", 1, 41001);
	addMarkedAffliction(friendly, "bleeding", 2, 41002);

	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto allowance = battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, purifySpell());
	ASSERT_TRUE(allowance.has_value());
	EXPECT_EQ(allowance->allowance, HeroActionAllowanceState::AllowanceKind::SPELL);
	EXPECT_EQ(allowance->source, HeroActionAllowanceState::GrantSource::DIVINE_MANDATE);
	ASSERT_TRUE(cast({{static_cast<int32_t>(friendly->unitId()), SpellID(SpellID::CURSE)}}));

	EXPECT_FALSE(hasEffect(friendly, SpellID(SpellID::CURSE)));
	EXPECT_EQ(friendly->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(friendly->physicalPoisonActivationsRemaining, 0);
	EXPECT_TRUE(hasAffliction(friendly, "disease"));
	EXPECT_TRUE(hasAffliction(friendly, "bleeding"));
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 1);
}

TEST_F(NewHorizonsPurifyTest, PurifierCleanupPrecedesMandateDiseaseRemoval)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, true, true));
	addSourceGroup(friendly, SpellID(SpellID::CURSE));
	addPhysicalPoison(friendly);
	addMarkedAffliction(friendly, "disease", 1, 42001);
	addMarkedAffliction(friendly, "bleeding", 2, 42002);

	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_TRUE(cast({{static_cast<int32_t>(friendly->unitId()), SpellID(SpellID::CURSE)}}));

	EXPECT_FALSE(hasEffect(friendly, SpellID(SpellID::CURSE)));
	EXPECT_EQ(friendly->physicalPoisonBaseDamage, 0)
		<< "Purifier's existing automatic Poison cleanup resolves before the Mandate bonus";
	EXPECT_FALSE(hasAffliction(friendly, "disease"));
	EXPECT_TRUE(hasAffliction(friendly, "bleeding"));
	EXPECT_EQ(physicalAfflictions::enumerate(*friendly).size(), 1u);
}

TEST_F(NewHorizonsPurifyTest, OrdinaryInitialHeroSpellDoesNotReceivePurifyingMandateBonus)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, false, true));
	addSourceGroup(friendly, SpellID(SpellID::CURSE));
	addPhysicalPoison(friendly);
	addMarkedAffliction(friendly, "disease", 1, 43001);

	const auto initialAllowance = battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, purifySpell());
	ASSERT_TRUE(initialAllowance.has_value());
	EXPECT_EQ(initialAllowance->allowance, HeroActionAllowanceState::AllowanceKind::HERO);
	EXPECT_NE(initialAllowance->source, HeroActionAllowanceState::GrantSource::DIVINE_MANDATE);
	ASSERT_TRUE(cast({{static_cast<int32_t>(friendly->unitId()), SpellID(SpellID::CURSE)}}));

	EXPECT_FALSE(hasEffect(friendly, SpellID(SpellID::CURSE)));
	EXPECT_EQ(friendly->physicalPoisonBaseDamage, 20);
	EXPECT_TRUE(hasAffliction(friendly, "disease"));
	const auto status = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
	EXPECT_EQ(status.completedPairs, 0);
	ASSERT_TRUE(status.pendingFollowup.has_value());
	EXPECT_EQ(status.pendingFollowup->source, HeroActionAllowanceState::GrantSource::DIVINE_MANDATE);
	EXPECT_EQ(status.pendingFollowup->allowance, HeroActionAllowanceState::AllowanceKind::ORDER);
}

TEST_F(NewHorizonsPurifyTest, PhysicalOnlyMandatePurifyDoesNotTriggerAnExtraAfflictionRemoval)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, false, true));
	addPhysicalPoison(friendly);
	addMarkedAffliction(friendly, "disease", 1, 44001);

	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto allowanceBefore = battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, purifySpell());
	ASSERT_TRUE(allowanceBefore.has_value());
	EXPECT_EQ(allowanceBefore->source, HeroActionAllowanceState::GrantSource::DIVINE_MANDATE);
	EXPECT_FALSE(cast());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_GT(friendly->physicalPoisonActivationsRemaining, 0);
	const auto allowanceAfterNoOp = battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, purifySpell());
	ASSERT_TRUE(allowanceAfterNoOp.has_value());
	EXPECT_EQ(allowanceAfterNoOp->source, HeroActionAllowanceState::GrantSource::DIVINE_MANDATE);

	ASSERT_TRUE(cast({{static_cast<int32_t>(friendly->unitId()),
		newHorizonsPurify::physicalPoisonChoiceID()}}));
	EXPECT_EQ(friendly->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(friendly->physicalPoisonActivationsRemaining, 0);
	EXPECT_TRUE(hasAffliction(friendly, "disease"));
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 1);
}

TEST_F(NewHorizonsPurifyTest, MandateDiseaseGroupRemovalDoesNotUnlockAnotherAfflictionRemoval)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, false, true));
	// Synthetic legacy source groups keep this trigger regression deterministic;
	// core Disease is also recognized as a physical affliction by the shared helper.
	addSourceGroup(friendly, SpellID(SpellID::DISEASE));
	addMarkedAffliction(friendly, "bleeding", 1, 45001);

	ASSERT_TRUE(hasEffect(friendly, SpellID(SpellID::DISEASE)));
	const auto afflictionsBefore = physicalAfflictions::enumerate(*friendly);
	ASSERT_EQ(afflictionsBefore.size(), 2u);
	EXPECT_TRUE(std::ranges::any_of(afflictionsBefore, [](const auto & affliction)
	{
		return affliction.kind == "disease" && affliction.source == BonusSource::SPELL_EFFECT;
	}));
	EXPECT_TRUE(hasAffliction(friendly, "bleeding"));

	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto allowance = battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, purifySpell());
	ASSERT_TRUE(allowance.has_value());
	EXPECT_EQ(allowance->source, HeroActionAllowanceState::GrantSource::DIVINE_MANDATE);
	ASSERT_TRUE(cast({{static_cast<int32_t>(friendly->unitId()), SpellID(SpellID::DISEASE)}}));

	EXPECT_FALSE(hasEffect(friendly, SpellID(SpellID::DISEASE)));
	EXPECT_TRUE(hasAffliction(friendly, "bleeding"));
	EXPECT_EQ(physicalAfflictions::enumerate(*friendly).size(), 1u)
		<< "Removing a legacy physical Disease group alone is not a magical-effect trigger";
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 1);
}

TEST(NewHorizonsPurifyHelpersTest, ActionRoundTripsAndOlderProtocolRejectsOnlyPopulatedPurifyChoices)
{
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = purifySpell();
	action.aimToHex(BattleHex(3, 5));
	action.spellPurifyChoices = {{12, SpellID(SpellID::CURSE)},
		{12, newHorizonsPurify::physicalPoisonChoiceID()}};

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & action);
	BattleAction restored;
	ASSERT_NO_THROW(current.iser & restored);
	ASSERT_EQ(restored.spellPurifyChoices, action.spellPurifyChoices);
	ASSERT_EQ(restored.target.size(), 1u);
	EXPECT_EQ(restored.target.front().hexValue, BattleHex(3, 5));

	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_SHADOW_GIFT;
	EXPECT_THROW(legacy.oser & action, std::runtime_error);
	EXPECT_TRUE(legacy.extractBuffer().empty());

	BattleAction ordinary;
	ordinary.actionType = EActionType::HERO_SPELL;
	ordinary.side = BattleSide::ATTACKER;
	ordinary.spell = SpellID(SpellID::CURE);
	ordinary.aimToHex(BattleHex::INVALID);
	CMemorySerializer oldOrdinary;
	oldOrdinary.oser.version = ESerializationVersion::NEW_HORIZONS_SHADOW_GIFT;
	EXPECT_NO_THROW(oldOrdinary.oser & ordinary);
}
