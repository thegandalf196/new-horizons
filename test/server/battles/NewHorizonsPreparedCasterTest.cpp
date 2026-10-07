/*
 * NewHorizonsPreparedCasterTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../SpellPointTestUtils.h"

#include "../../../lib/GameLibrary.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/entities/hero/CHeroClass.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/constants/NumericConstants.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/SpellCostBreakdown.h"
#include "../../../server/CGameHandler.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

namespace
{
constexpr auto WISDOM_SKILL = "new-horizons:wisdom";
constexpr auto MYSTICISM = "new-horizons:wisdom.mysticism";
constexpr auto PREPARED_CASTER = "new-horizons:wisdom.preparedCaster";
constexpr auto DEEP_KNOWLEDGE = "new-horizons:wisdom.deepKnowledge";
constexpr auto ARCHMAGE = "new-horizons:wisdom.archmage";
constexpr auto HAVOC_MAGIC = "new-horizons:havocMagic";
constexpr auto METAMAGIC_SKILL = "new-horizons:metamagic";
constexpr auto ARCANE_ECONOMY = "new-horizons:metamagic.arcaneEconomy";

class NewHorizonsPreparedCasterTest : public HeroCommandFixture
{
protected:
	bool capturePreparedCasterAsPlanned = false;
	bool captureArchmageAsPlanned = false;
	bool chainLightningCostIsOne = false;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		JsonNode magicRules(JsonPath::builtin("config/newHorizonsMagic"));
		if(chainLightningCostIsOne)
		{
			for(auto & cost : magicRules["spells"]["core:chainLightning"]["costs"].Vector())
				cost.Integer() = 1;
		}
		newHorizonsMagic::validateRules(magicRules);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(capturePreparedCasterAsPlanned || captureArchmageAsPlanned)
		{
			for(auto & perk : perkRules["skills"][WISDOM_SKILL]["perks"].Vector())
			{
				if(perk["id"].String() == PREPARED_CASTER && capturePreparedCasterAsPlanned)
					perk["effect"]["status"].String() = "planned";
				if(perk["id"].String() == ARCHMAGE && captureArchmageAsPlanned)
					perk["effect"]["status"].String() = "planned";
			}
		}
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perkRules);
	}

	void startGameWithWizard()
	{
		const CreatureID token(0);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder
			.size(36, false)
			.name("PreparedCasterTest")
			.playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:solmyr")), PlayerColor(0))
			.heroGarrison({{token, 1}})
			.hero({7, 7, 0}, HeroTypeID(1), PlayerColor(1))
			.heroGarrison({{token, 1}});
		startWithMap(std::move(builder));

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_EQ(attackerSideHero->getHeroClass()->getJsonKey(), "core:wizard");
	}

	SecondarySkill wisdomSkill() const
	{
		const int decoded = SecondarySkill::decode(WISDOM_SKILL);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void grantPreparedCaster(CGHeroInstance * hero, int wisdomRank = MasteryLevel::BASIC)
	{
		hero->setSecSkillLevel(wisdomSkill(), wisdomRank, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({WISDOM_SKILL, PREPARED_CASTER});
		ASSERT_TRUE(hero->hasActivePerk(WISDOM_SKILL, PREPARED_CASTER));
	}

	void grantArcaneEconomy(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(METAMAGIC_SKILL);
		ASSERT_GE(decoded, 0);
		hero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({METAMAGIC_SKILL, ARCANE_ECONOMY});
		ASSERT_TRUE(hero->hasActivePerk(METAMAGIC_SKILL, ARCANE_ECONOMY));
	}

	void grantArchmage(CGHeroInstance * hero, bool includePreparedCaster = false)
	{
		hero->setSecSkillLevel(wisdomSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({WISDOM_SKILL, includePreparedCaster ? PREPARED_CASTER : MYSTICISM});
		hero->setSecSkillLevel(wisdomSkill(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({WISDOM_SKILL, DEEP_KNOWLEDGE});
		hero->setSecSkillLevel(wisdomSkill(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({WISDOM_SKILL, ARCHMAGE});
		ASSERT_TRUE(hero->hasActivePerk(WISDOM_SKILL, ARCHMAGE));
	}

	bool selectPerkThroughNormalOffer(CGHeroInstance * hero, const std::string & perkId)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto candidate = std::find_if(offer.begin(), offer.end(), [&](const auto & entry)
			{
				return entry.selection.skillId == WISDOM_SKILL && entry.selection.perkId == perkId;
			});
			if(candidate == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), candidate));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			return true;
		}
		return false;
	}

	void prepareAttackerSpellbook(CGHeroInstance * hero)
	{
		giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		hero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		setTestSpellPointTotal(hero, 1000);
	}

	void prepareArchmageSpellbook(CGHeroInstance * hero)
	{
		prepareAttackerSpellbook(hero);
		hero->addSpellToSpellbook(SpellID::CHAIN_LIGHTNING);
		hero->addSpellToSpellbook(SpellID::ARMAGEDDON);
		const int decoded = SecondarySkill::decode(HAVOC_MAGIC);
		ASSERT_GE(decoded, 0);
		hero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	}

	SpellCostBreakdown readSpellCostBreakdown(const spells::Spell * spell, int32_t expectedListedCost)
	{
		const auto manaBefore = attackerSideHero->getManaAvailable();
		const bool castCompletedBefore = battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER);
		const bool levelFourCompletedBefore = battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4);
		const bool levelFiveCompletedBefore = battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5);
		const auto breakdown = battle()->battleGetSpellCostBreakdown(spell, attackerSideHero);

		EXPECT_EQ(breakdown.listedCost, expectedListedCost);
		EXPECT_EQ(breakdown.finalCost, battle()->battleGetSpellCost(spell, attackerSideHero));
		EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
		EXPECT_EQ(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER), castCompletedBefore);
		EXPECT_EQ(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4), levelFourCompletedBefore);
		EXPECT_EQ(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5), levelFiveCompletedBefore);

		if(breakdown.stages.empty())
		{
			// An unchanged cost legitimately has no recorded stages.
			EXPECT_EQ(breakdown.finalCost, breakdown.listedCost);
		}
		else
		{
			EXPECT_EQ(breakdown.stages.front().before, breakdown.listedCost);
			for(size_t index = 1; index < breakdown.stages.size(); ++index)
				EXPECT_EQ(breakdown.stages[index].before, breakdown.stages[index - 1].after);
			EXPECT_EQ(breakdown.stages.back().after, breakdown.finalCost);
		}
		return breakdown;
	}

	CStack * prepareBattleWithPreparedCaster(int wisdomRank = MasteryLevel::BASIC, bool includeArcaneEconomy = false)
	{
		startGame();
		grantPreparedCaster(attackerSideHero, wisdomRank);
		if(includeArcaneEconomy)
			grantArcaneEconomy(attackerSideHero);
		prepareAttackerSpellbook(attackerSideHero);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		startBattle();
		auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
		beginCombat();
		return target;
	}

	CStack * prepareBattleWithArchmage(bool includePreparedCaster = false)
	{
		startGame();
		grantArchmage(attackerSideHero, includePreparedCaster);
		prepareArchmageSpellbook(attackerSideHero);
		startBattle();
		auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
		beginCombat();
		return target;
	}

	bool issueMagicArrow(CStack * target, int overcharge = 0)
	{
		return issueHeroSpell(SpellID::MAGIC_ARROW, target, overcharge);
	}

	bool issueHeroSpell(SpellID spell, CStack * target, int overcharge = 0)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.spellOvercharge = overcharge;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool issueGlobalHeroSpell(SpellID spell)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.stackNumber = -1;
		action.aimToHex(BattleHex::INVALID);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(BattleSide::ATTACKER), action);
	}
};
}

TEST_F(NewHorizonsPreparedCasterTest, AcceptedFirstSpellGetsDiscountAndTheGatePersistsAcrossRounds)
{
	auto * target = prepareBattleWithPreparedCaster(MasteryLevel::EXPERT);
	ASSERT_NE(target, nullptr);
	const auto * magicArrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_EQ(attackerSideHero->getListedSpellCost(magicArrow), 4);
	const auto wisdomCost = newHorizonsMagic::wisdomAdjustedCost(
		attackerSideHero->getListedSpellCost(magicArrow), 1, MasteryLevel::EXPERT);
	ASSERT_EQ(wisdomCost, 3);
	const auto breakdown = readSpellCostBreakdown(magicArrow, 4);
	EXPECT_EQ(breakdown.finalCost, 1);
	const auto wisdomStage = std::find_if(breakdown.stages.begin(), breakdown.stages.end(), [](const auto & stage)
	{
		return stage.kind == SpellCostStage::Kind::WISDOM;
	});
	const auto preparedStage = std::find_if(breakdown.stages.begin(), breakdown.stages.end(), [](const auto & stage)
	{
		return stage.kind == SpellCostStage::Kind::PREPARED_CASTER;
	});
	ASSERT_NE(wisdomStage, breakdown.stages.end());
	ASSERT_NE(preparedStage, breakdown.stages.end());
	EXPECT_LT(std::distance(breakdown.stages.begin(), wisdomStage),
		std::distance(breakdown.stages.begin(), preparedStage));
	EXPECT_EQ(wisdomStage->before, 4);
	EXPECT_EQ(wisdomStage->after, 3);
	EXPECT_EQ(preparedStage->after, breakdown.finalCost);
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));

	const auto firstMana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(issueMagicArrow(target, 4));
	EXPECT_EQ(firstMana - attackerSideHero->getManaAvailable(), 5)
		<< "Prepared Caster discounts the Wisdom base to 1; Overcharge still adds all four Mana";
	EXPECT_TRUE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::DEFENDER));

	const auto firstRound = battle()->getRound();
	endRound();
	ASSERT_EQ(battle()->getRound(), firstRound + 1);
	EXPECT_TRUE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleGetSpellCost(SpellID(SpellID::MAGIC_ARROW).toSpell(), attackerSideHero), wisdomCost);
	const auto laterMana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(issueMagicArrow(target));
	EXPECT_EQ(laterMana - attackerSideHero->getManaAvailable(), wisdomCost)
		<< "The battle-long marker survives round transition and no longer discounts later spells";
}

TEST_F(NewHorizonsPreparedCasterTest, AdventureSpellBreakdownIgnoresWisdomAndMetamagicFollowup)
{
	auto * target = prepareBattleWithPreparedCaster(MasteryLevel::EXPERT, true);
	ASSERT_NE(target, nullptr);
	attackerSideHero->addSpellToSpellbook(SpellID(SpellID::TOWN_PORTAL));
	const auto * townPortal = SpellID(SpellID::TOWN_PORTAL).toSpell();
	const auto adventureBreakdown = readSpellCostBreakdown(townPortal,
		attackerSideHero->getListedSpellCost(townPortal));
	const auto adventureFollowupBreakdown = battle()->battleGetSpellCostBreakdown(
		townPortal, attackerSideHero, 1, true);

	EXPECT_TRUE(newHorizonsMagic::isAdventureSpell(attackerSideHero->getMagicRules(), townPortal->getId()));
	EXPECT_EQ(adventureFollowupBreakdown.finalCost, adventureBreakdown.finalCost);
	EXPECT_EQ(adventureFollowupBreakdown.finalCost, battle()->battleGetSpellCost(townPortal, attackerSideHero));
	EXPECT_TRUE(std::none_of(adventureFollowupBreakdown.stages.begin(), adventureFollowupBreakdown.stages.end(),
		[](const auto & stage)
		{
			return stage.kind == SpellCostStage::Kind::WISDOM
				|| stage.kind == SpellCostStage::Kind::METAMAGIC_ARCANE_ECONOMY;
		}));
}

TEST_F(NewHorizonsPreparedCasterTest, WorldSpellCostBreakdownSeparatesAdventureAndWisdomStages)
{
	startGame();
	grantPreparedCaster(attackerSideHero, MasteryLevel::EXPERT);
	grantArcaneEconomy(attackerSideHero);
	prepareAttackerSpellbook(attackerSideHero);
	attackerSideHero->addSpellToSpellbook(SpellID(SpellID::TOWN_PORTAL));

	const auto * townPortal = SpellID(SpellID::TOWN_PORTAL).toSpell();
	const auto * magicArrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto adventure = gameState()->getSpellCostBreakdown(townPortal, attackerSideHero);
	const auto adventureFollowup = gameState()->getSpellCostBreakdown(townPortal, attackerSideHero, true);
	const auto ordinary = gameState()->getSpellCostBreakdown(magicArrow, attackerSideHero);

	EXPECT_TRUE(newHorizonsMagic::isAdventureSpell(attackerSideHero->getMagicRules(), townPortal->getId()));
	EXPECT_EQ(adventure.listedCost, attackerSideHero->getListedSpellCost(townPortal));
	EXPECT_EQ(adventure.finalCost, attackerSideHero->getSpellCost(townPortal));
	EXPECT_EQ(adventureFollowup.finalCost, adventure.finalCost)
		<< "World-map spells do not receive an Arcane Economy follow-up discount";
	EXPECT_TRUE(std::none_of(adventure.stages.begin(), adventure.stages.end(), [](const auto & stage)
	{
		return stage.kind == SpellCostStage::Kind::WISDOM
			|| stage.kind == SpellCostStage::Kind::METAMAGIC_ARCANE_ECONOMY;
	}));
	EXPECT_TRUE(std::none_of(adventureFollowup.stages.begin(), adventureFollowup.stages.end(), [](const auto & stage)
	{
		return stage.kind == SpellCostStage::Kind::WISDOM
			|| stage.kind == SpellCostStage::Kind::METAMAGIC_ARCANE_ECONOMY;
	}));

	EXPECT_EQ(ordinary.listedCost, attackerSideHero->getListedSpellCost(magicArrow));
	EXPECT_EQ(ordinary.finalCost, attackerSideHero->getSpellCost(magicArrow));
	ASSERT_EQ(ordinary.stages.size(), 1u);
	EXPECT_EQ(ordinary.stages.front().kind, SpellCostStage::Kind::WISDOM);
	EXPECT_EQ(ordinary.stages.front().before, ordinary.listedCost);
	EXPECT_EQ(ordinary.stages.front().after, ordinary.finalCost);
	EXPECT_NE(ordinary.stages.front().before, ordinary.stages.front().after);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
}

TEST_F(NewHorizonsPreparedCasterTest, RejectedHeroAndAcceptedCreatureCastsDoNotConsumeTheMarker)
{
	startGame();
	grantPreparedCaster(attackerSideHero);
	prepareAttackerSpellbook(attackerSideHero);
	startBattle();
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(leftHex), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(rightHex), 10);
	auto * creatureCaster = addStack(BattleSide::ATTACKER, creatureByName("core:imp"), BattleHex(leftHex + 2), 1);
	ASSERT_NE(friendly, nullptr);
	ASSERT_NE(enemy, nullptr);
	ASSERT_NE(creatureCaster, nullptr);
	beginCombat();

	EXPECT_FALSE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, friendly));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));

	creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELLCASTER, BonusSource::OTHER, 3, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))));
	creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));
	creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CREATURE_SPELL_POWER, BonusSource::OTHER, 20, BonusSourceID()));

	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	spells::BattleCast creatureCast(battle(), creatureCaster, spells::Mode::CREATURE_ACTIVE, spell);
	spells::Target target{spells::Destination(enemy)};
	const auto mechanics = spell->battleMechanics(&creatureCast);
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	creatureCast.cast(gameHandler->spellEnv.get(), target);
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
}

TEST_F(NewHorizonsPreparedCasterTest, FloorAndBattlefieldCostModifiersApplyAfterThePreparedDiscount)
{
	startGame();
	grantPreparedCaster(attackerSideHero, MasteryLevel::EXPERT);
	startBattle();
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 1);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 1);
	ASSERT_NE(friendly, nullptr);
	ASSERT_NE(enemy, nullptr);

	const auto * chainLightning = SpellID(SpellID::CHAIN_LIGHTNING).toSpell();
	const int wisdomCost = newHorizonsMagic::wisdomAdjustedCost(
		attackerSideHero->getListedSpellCost(chainLightning), 1, MasteryLevel::EXPERT);
	const int preparedCost = std::max(1, wisdomCost - 2);
	friendly->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CHANGES_SPELL_COST_FOR_ALLY, BonusSource::OTHER, 3, BonusSourceID()));
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CHANGES_SPELL_COST_FOR_ENEMY, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(battle()->battleGetSpellCost(chainLightning, attackerSideHero),
		std::max(1, preparedCost - 3 + 1));

	const auto * magicArrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
	friendly->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CHANGES_SPELL_COST_FOR_ALLY, BonusSource::OTHER, 100, BonusSourceID()));
	const auto floorBreakdown = readSpellCostBreakdown(magicArrow,
		attackerSideHero->getListedSpellCost(magicArrow));
	EXPECT_EQ(floorBreakdown.finalCost, 1)
		<< "Prepared Caster and allied battlefield reductions cannot make an ordinary spell free";
	const auto minimumStage = std::find_if(floorBreakdown.stages.begin(), floorBreakdown.stages.end(), [](const auto & stage)
	{
		return stage.kind == SpellCostStage::Kind::MINIMUM_COST;
	});
	ASSERT_NE(minimumStage, floorBreakdown.stages.end());
	EXPECT_LT(minimumStage->before, 1);
	EXPECT_EQ(minimumStage->after, floorBreakdown.finalCost);
}

TEST_F(NewHorizonsPreparedCasterTest, WizardSelectsArchmageThroughTheNormalExpertOffer)
{
	startGameWithWizard();
	for(int index = 0; index < LIBRARY->skillh->size(); ++index)
		attackerSideHero->setSecSkillLevel(SecondarySkill(index), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(wisdomSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(selectPerkThroughNormalOffer(attackerSideHero, PREPARED_CASTER))
		<< "Prepared Caster should be a legal Basic Wisdom perk offer";
	attackerSideHero->setSecSkillLevel(wisdomSkill(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(selectPerkThroughNormalOffer(attackerSideHero, DEEP_KNOWLEDGE))
		<< "Deep Knowledge should be a legal Advanced Wisdom perk offer";
	attackerSideHero->setSecSkillLevel(wisdomSkill(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(selectPerkThroughNormalOffer(attackerSideHero, ARCHMAGE))
		<< "Archmage should be a legal Expert Wisdom perk offer after lower tiers are selected";
	EXPECT_TRUE(attackerSideHero->hasActivePerk(WISDOM_SKILL, ARCHMAGE));
}

TEST_F(NewHorizonsPreparedCasterTest, PlannedArchmageIsInactiveAndDoesNotDiscount)
{
	captureArchmageAsPlanned = true;
	startGameWithWizard();
	for(int index = 0; index < LIBRARY->skillh->size(); ++index)
		attackerSideHero->setSecSkillLevel(SecondarySkill(index), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(wisdomSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(selectPerkThroughNormalOffer(attackerSideHero, MYSTICISM));
	attackerSideHero->setSecSkillLevel(wisdomSkill(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(selectPerkThroughNormalOffer(attackerSideHero, DEEP_KNOWLEDGE));
	attackerSideHero->setSecSkillLevel(wisdomSkill(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);

	const auto & savedPerks = attackerSideHero->getPerkState().rules["skills"][WISDOM_SKILL]["perks"].Vector();
	const auto capturedArchmage = std::find_if(savedPerks.begin(), savedPerks.end(), [](const auto & perk)
	{
		return perk["id"].String() == ARCHMAGE;
	});
	ASSERT_NE(capturedArchmage, savedPerks.end());
	EXPECT_EQ((*capturedArchmage)["effect"]["status"].String(), "planned");
	const auto rankLookup = [this](const std::string & skillId)
	{
		return attackerSideHero->getPerkSkillRank(skillId);
	};
	for(uint64_t seed = 0; seed < 128; ++seed)
	{
		const auto offer = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed);
		EXPECT_TRUE(std::none_of(offer.begin(), offer.end(), [](const auto & candidate)
		{
			return candidate.selection.skillId == WISDOM_SKILL && candidate.selection.perkId == ARCHMAGE;
		}));
	}
	EXPECT_THROW(attackerSideHero->applyPerkSelection({WISDOM_SKILL, ARCHMAGE}), std::runtime_error);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(WISDOM_SKILL, ARCHMAGE));

	prepareArchmageSpellbook(attackerSideHero);
	startBattle();
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	const auto * chainLightning = SpellID(SpellID::CHAIN_LIGHTNING).toSpell();
	const auto wisdomCost = newHorizonsMagic::wisdomAdjustedCost(
		attackerSideHero->getListedSpellCost(chainLightning), 1, MasteryLevel::EXPERT);
	EXPECT_EQ(battle()->battleGetSpellCost(chainLightning, attackerSideHero), wisdomCost);
}

TEST_F(NewHorizonsPreparedCasterTest, CurrentExpertRankIsRequiredAfterArchmageSelection)
{
	prepareBattleWithArchmage();
	const auto * chainLightning = SpellID(SpellID::CHAIN_LIGHTNING).toSpell();
	const auto wisdomCost = newHorizonsMagic::wisdomAdjustedCost(
		attackerSideHero->getListedSpellCost(chainLightning), 1, MasteryLevel::EXPERT);
	EXPECT_TRUE(attackerSideHero->hasActivePerk(WISDOM_SKILL, ARCHMAGE));
	EXPECT_EQ(battle()->battleGetSpellCost(chainLightning, attackerSideHero), std::max(1, wisdomCost - 3));
	attackerSideHero->setSecSkillLevel(wisdomSkill(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(WISDOM_SKILL, ARCHMAGE));
	const auto advancedWisdomCost = newHorizonsMagic::wisdomAdjustedCost(
		attackerSideHero->getListedSpellCost(chainLightning), 1, MasteryLevel::ADVANCED);
	EXPECT_EQ(battle()->battleGetSpellCost(chainLightning, attackerSideHero), advancedWisdomCost);
}

TEST_F(NewHorizonsPreparedCasterTest, LowLevelCastLeavesArchmageForFirstHighLevelCastAndDiscountsStack)
{
	auto * target = prepareBattleWithArchmage(true);
	ASSERT_NE(target, nullptr);
	const auto * chainLightning = SpellID(SpellID::CHAIN_LIGHTNING).toSpell();
	const int wisdomCost = newHorizonsMagic::wisdomAdjustedCost(
		attackerSideHero->getListedSpellCost(chainLightning), 1, MasteryLevel::EXPERT);
	EXPECT_EQ(battle()->battleGetSpellCost(chainLightning, attackerSideHero), std::max(1, wisdomCost - 2 - 3));

	ASSERT_TRUE(issueMagicArrow(target));
	EXPECT_TRUE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 1));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));
	endRound();

	EXPECT_EQ(battle()->battleGetSpellCost(chainLightning, attackerSideHero), std::max(1, wisdomCost - 3));
	const auto manaBefore = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(issueHeroSpell(SpellID::CHAIN_LIGHTNING, target));
	EXPECT_EQ(manaBefore - attackerSideHero->getManaAvailable(), std::max(1, wisdomCost - 3));
	EXPECT_TRUE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));
}

TEST_F(NewHorizonsPreparedCasterTest, FirstHighSpellConsumesSharedLevelFourOrFiveGate)
{
	startGame();
	grantArchmage(attackerSideHero, true);
	prepareArchmageSpellbook(attackerSideHero);
	startBattle();
	auto * durableAttacker = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"),
		BattleHex(leftHex), 1000);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	beginCombat();
	ASSERT_NE(durableAttacker, nullptr);
	ASSERT_NE(target, nullptr);
	const auto * chainLightning = SpellID(SpellID::CHAIN_LIGHTNING).toSpell();
	const auto * armageddon = SpellID(SpellID::ARMAGEDDON).toSpell();
	const int chainWisdomCost = newHorizonsMagic::wisdomAdjustedCost(
		attackerSideHero->getListedSpellCost(chainLightning), 1, MasteryLevel::EXPERT);
	const auto chainBreakdown = readSpellCostBreakdown(chainLightning,
		attackerSideHero->getListedSpellCost(chainLightning));
	EXPECT_EQ(chainBreakdown.finalCost, std::max(1, chainWisdomCost - 2 - 3));
	const auto preparedStage = std::find_if(chainBreakdown.stages.begin(), chainBreakdown.stages.end(), [](const auto & stage)
	{
		return stage.kind == SpellCostStage::Kind::PREPARED_CASTER;
	});
	const auto archmageStage = std::find_if(chainBreakdown.stages.begin(), chainBreakdown.stages.end(), [](const auto & stage)
	{
		return stage.kind == SpellCostStage::Kind::ARCHMAGE;
	});
	ASSERT_NE(preparedStage, chainBreakdown.stages.end());
	ASSERT_NE(archmageStage, chainBreakdown.stages.end());
	EXPECT_LT(std::distance(chainBreakdown.stages.begin(), preparedStage),
		std::distance(chainBreakdown.stages.begin(), archmageStage));
	const auto firstHighMana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(issueHeroSpell(SpellID::CHAIN_LIGHTNING, target));
	EXPECT_EQ(firstHighMana - attackerSideHero->getManaAvailable(), std::max(1, chainWisdomCost - 2 - 3));
	EXPECT_TRUE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));
	ASSERT_TRUE(durableAttacker->alive());
	ASSERT_FALSE(battle()->battleIsFinished().has_value());
	endRound();
	const auto * nextActive = battle()->battleActiveUnit();
	ASSERT_NE(nextActive, nullptr);
	ASSERT_TRUE(nextActive->alive());
	ASSERT_EQ(nextActive->unitSide(), BattleSide::ATTACKER);

	const int armageddonWisdomCost = newHorizonsMagic::wisdomAdjustedCost(
		attackerSideHero->getListedSpellCost(armageddon), 1, MasteryLevel::EXPERT);
	EXPECT_EQ(battle()->battleGetSpellCost(armageddon, attackerSideHero), armageddonWisdomCost);
	const auto manaBefore = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(issueGlobalHeroSpell(SpellID::ARMAGEDDON));
	EXPECT_EQ(manaBefore - attackerSideHero->getManaAvailable(), armageddonWisdomCost);
	EXPECT_TRUE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));
}

TEST_F(NewHorizonsPreparedCasterTest, DeadActiveUnitActionIsRejectedBeforeStartAction)
{
	startGame();
	grantArchmage(attackerSideHero, true);
	prepareArchmageSpellbook(attackerSideHero);
	startBattle();
	ASSERT_NE(addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(leftHex), 1000), nullptr);
	ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000), nullptr);
	beginCombat();

	const auto * firstActive = battle()->battleActiveUnit();
	ASSERT_NE(firstActive, nullptr);
	ASSERT_TRUE(firstActive->alive());
	const auto startActionsBeforeValidRequest = server.startedActions.size();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(firstActive->unitSide()), BattleAction::makeDefend(firstActive)));
	EXPECT_EQ(server.startedActions.size(), startActionsBeforeValidRequest + 1);

	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	ASSERT_TRUE(active->alive());
	const auto activeId = active->unitId();
	const auto deadStackSide = active->unitSide();
	const auto activeOwner = battle()->sideToPlayer(deadStackSide);
	auto deadState = active->acquireState();
	const auto requestedLethalDamage = deadState->getAvailableHealth();
	ASSERT_GT(requestedLethalDamage, 0);
	auto lethalDamage = requestedLethalDamage;
	deadState->damage(lethalDamage);
	BattleUnitsChanged killedActive;
	killedActive.battleID = BattleID(0);
	killedActive.changedStacks.emplace_back(activeId, UnitChanges::EOperation::UPDATE);
	killedActive.changedStacks.back().data = deadState->save();
	killedActive.changedStacks.back().healthDelta = -requestedLethalDamage;
	gameHandler->sendAndApply(killedActive);

	const auto * staleActive = battle()->battleActiveUnit();
	ASSERT_NE(staleActive, nullptr);
	ASSERT_EQ(staleActive->unitId(), activeId);
	ASSERT_FALSE(staleActive->alive());
	ASSERT_FALSE(battle()->battleIsFinished().has_value());
	const auto startActionsBeforeRejectedRequest = server.startedActions.size();
	const auto manaBeforeRejectedRequest = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	for(int32_t spellLevel = 1; spellLevel <= GameConstants::SPELL_LEVELS; ++spellLevel)
		EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, spellLevel));

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), activeOwner,
		BattleAction::makeDefend(staleActive)));
	EXPECT_EQ(server.startedActions.size(), startActionsBeforeRejectedRequest);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeRejectedRequest);
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	for(int32_t spellLevel = 1; spellLevel <= GameConstants::SPELL_LEVELS; ++spellLevel)
		EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, spellLevel));
}

TEST_F(NewHorizonsPreparedCasterTest, RejectedHeroAndAcceptedCreatureHighCastsDoNotConsumeArchmage)
{
	startGame();
	grantArchmage(attackerSideHero);
	prepareArchmageSpellbook(attackerSideHero);
	startBattle();
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(leftHex), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(rightHex), 10);
	auto * creatureCaster = addStack(BattleSide::ATTACKER, creatureByName("core:imp"), BattleHex(leftHex + 2), 1);
	ASSERT_NE(friendly, nullptr);
	ASSERT_NE(enemy, nullptr);
	ASSERT_NE(creatureCaster, nullptr);
	beginCombat();

	EXPECT_FALSE(issueHeroSpell(SpellID::CHAIN_LIGHTNING, friendly));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));

	creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELLCASTER, BonusSource::OTHER, 3, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::CHAIN_LIGHTNING))));
	creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));
	creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CREATURE_SPELL_POWER, BonusSource::OTHER, 20, BonusSourceID()));

	const auto * spell = SpellID(SpellID::CHAIN_LIGHTNING).toSpell();
	spells::BattleCast creatureCast(battle(), creatureCaster, spells::Mode::CREATURE_ACTIVE, spell);
	spells::Target target{spells::Destination(enemy)};
	const auto mechanics = spell->battleMechanics(&creatureCast);
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	creatureCast.cast(gameHandler->spellEnv.get(), target);
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));
	const auto * chainLightning = SpellID(SpellID::CHAIN_LIGHTNING).toSpell();
	const int wisdomCost = newHorizonsMagic::wisdomAdjustedCost(
		attackerSideHero->getListedSpellCost(chainLightning), 1, MasteryLevel::EXPERT);
	EXPECT_EQ(battle()->battleGetSpellCost(chainLightning, attackerSideHero), std::max(1, wisdomCost - 3));
}

TEST_F(NewHorizonsPreparedCasterTest, ArchmageCostReductionNeverMakesAnOrdinarySpellFree)
{
	chainLightningCostIsOne = true;
	auto * target = prepareBattleWithArchmage(true);
	ASSERT_NE(target, nullptr);
	EXPECT_EQ(battle()->battleGetSpellCost(SpellID(SpellID::CHAIN_LIGHTNING).toSpell(), attackerSideHero), 1);
}

TEST_F(NewHorizonsPreparedCasterTest, WizardSelectsPreparedCasterThroughTheNormalOffer)
{
	startGameWithWizard();
	for(int index = 0; index < LIBRARY->skillh->size(); ++index)
		attackerSideHero->setSecSkillLevel(SecondarySkill(index), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(wisdomSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);

	const auto rankLookup = [this](const std::string & skillId)
	{
		return attackerSideHero->getPerkSkillRank(skillId);
	};
	bool selectedThroughOffer = false;
	for(uint64_t seed = 0; seed < 4096 && !selectedThroughOffer; ++seed)
	{
		const auto offer = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed);
		const auto prepared = std::find_if(offer.begin(), offer.end(), [](const auto & candidate)
		{
			return candidate.selection.perkId == PREPARED_CASTER;
		});
		if(prepared == offer.end())
			continue;

		const auto choice = static_cast<size_t>(std::distance(offer.begin(), prepared));
		gameHandler->levelUpHero(attackerSideHero, offer, choice, seed, false);
		selectedThroughOffer = true;
	}
	ASSERT_TRUE(selectedThroughOffer) << "Prepared Caster should be a legal Basic Wisdom perk offer";
	EXPECT_TRUE(attackerSideHero->hasActivePerk(WISDOM_SKILL, PREPARED_CASTER));
}

TEST_F(NewHorizonsPreparedCasterTest, CapturedPlannedSnapshotDoesNotOfferOrActivatePreparedCaster)
{
	capturePreparedCasterAsPlanned = true;
	startGameWithWizard();
	attackerSideHero->setSecSkillLevel(wisdomSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const auto & savedPerks = attackerSideHero->getPerkState().rules["skills"][WISDOM_SKILL]["perks"].Vector();
	const auto capturedPreparedCaster = std::find_if(savedPerks.begin(), savedPerks.end(), [](const auto & perk)
	{
		return perk["id"].String() == PREPARED_CASTER;
	});
	ASSERT_NE(capturedPreparedCaster, savedPerks.end());
	EXPECT_EQ((*capturedPreparedCaster)["effect"]["status"].String(), "planned");

	const auto rankLookup = [this](const std::string & skillId)
	{
		return attackerSideHero->getPerkSkillRank(skillId);
	};
	for(uint64_t seed = 0; seed < 128; ++seed)
	{
		const auto offer = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed);
		EXPECT_TRUE(std::none_of(offer.begin(), offer.end(), [](const auto & candidate)
		{
			return candidate.selection.perkId == PREPARED_CASTER;
		}));
	}
	EXPECT_THROW(attackerSideHero->applyPerkSelection({WISDOM_SKILL, PREPARED_CASTER}), std::runtime_error);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(WISDOM_SKILL, PREPARED_CASTER));

	prepareAttackerSpellbook(attackerSideHero);
	startBattle();
	const auto * magicArrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
	const auto listed = attackerSideHero->getListedSpellCost(magicArrow);
	const auto wisdomCost = newHorizonsMagic::wisdomAdjustedCost(listed, 1, MasteryLevel::BASIC);
	EXPECT_EQ(battle()->battleGetSpellCost(magicArrow, attackerSideHero), wisdomCost);
}

TEST_F(NewHorizonsPreparedCasterTest, CompletedHeroSpellLevelsHaveAppendOnlyBinaryCompatibility)
{
	SideInBattle original(nullptr);
	original.heroSpellCastCompleted = true;
	original.recordCompletedHeroSpellLevel(1);
	original.recordCompletedHeroSpellLevel(4);
	original.recordCompletedHeroSpellLevel(5);

	CMemorySerializer current;
	current.oser.version = current.iser.version = ESerializationVersion::CURRENT;
	current.oser & original;
	SideInBattle restored(nullptr);
	current.iser & restored;
	EXPECT_TRUE(restored.heroSpellCastCompleted);
	EXPECT_TRUE(restored.hasCompletedHeroSpellLevel(1));
	EXPECT_FALSE(restored.hasCompletedHeroSpellLevel(3));
	EXPECT_TRUE(restored.hasCompletedHeroSpellLevel(4));
	EXPECT_TRUE(restored.hasCompletedHeroSpellLevel(5));
	EXPECT_FALSE(restored.hasCompletedHeroSpellLevel(6));

	CMemorySerializer oldDownsave;
	oldDownsave.oser.version = ESerializationVersion::BATTLE_COMPLETED_HERO_SPELL;
	EXPECT_THROW(oldDownsave.oser & original, std::runtime_error);
	EXPECT_TRUE(oldDownsave.extractBuffer().empty());

	CMemorySerializer oldSave;
	oldSave.oser.version = oldSave.iser.version = ESerializationVersion::BATTLE_COMPLETED_HERO_SPELL;
	SideInBattle oldState(nullptr);
	oldState.heroSpellCastCompleted = true;
	oldSave.oser & oldState;
	SideInBattle loadedFromOld(nullptr);
	loadedFromOld.completedHeroSpellLevels = SideInBattle::COMPLETED_HERO_SPELL_LEVELS_MASK;
	oldSave.iser & loadedFromOld;
	EXPECT_TRUE(loadedFromOld.heroSpellCastCompleted);
	EXPECT_EQ(loadedFromOld.completedHeroSpellLevels, 0);
}
