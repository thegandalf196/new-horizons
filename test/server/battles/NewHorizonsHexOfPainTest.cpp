/*
 * NewHorizonsHexOfPainTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"

#include "../../../lib/GameSettings.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/constants/StringConstants.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/scripting/ScriptService.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

#include <algorithm>
#include <stdexcept>

namespace
{
constexpr auto spellKey = "new-horizons:hexOfPain";
constexpr auto combatEventKey = "core:hexOfPain";
constexpr auto spellEffectKey = "core:hexOfPainEffect";
constexpr auto shadowMagicSkillKey = "new-horizons:shadowMagic";
constexpr auto painweaverPerkKey = "new-horizons:shadowMagic.painweaver";

SpellID hexOfPainSpell()
{
	return SpellID(SpellID::decode(spellKey));
}

bool activatePainweaverInFixture(JsonNode & rules)
{
	auto & perks = rules["skills"][shadowMagicSkillKey]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
	{
		return perk["id"].String() == painweaverPerkKey;
	});
	if(found == perks.end())
		return false;

	(*found)["effect"]["status"].String() = "active";
	return true;
}

class NewHorizonsHexOfPainTest : public HeroCommandFixture
{
protected:
	SpellID spell = SpellID::NONE;
	ScriptID combatEvent;
	ScriptID spellEffect;
	bool useV2MagicRules = false;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";

		spell = hexOfPainSpell();
		ASSERT_NE(spell, SpellID::NONE);
		const auto event = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "script", std::string(combatEventKey));
		ASSERT_TRUE(event.has_value());
		combatEvent = ScriptID(*event);
		const auto effect = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "script", std::string(spellEffectKey));
		ASSERT_TRUE(effect.has_value());
		spellEffect = ScriptID(*effect);
		EXPECT_EQ(LIBRARY->scriptTypes()->getById(combatEvent).kind, ScriptKind::COMBAT_EVENT);
		EXPECT_EQ(LIBRARY->scriptTypes()->getById(spellEffect).kind, ScriptKind::SPELL_EFFECT);
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(!activatePainweaverInFixture(perkRules))
			throw std::runtime_error("Missing Painweaver from the New Horizons perk registry");
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));

		auto magicRules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		ASSERT_FALSE(magicRules["spells"][spellKey].isNull());
		if(useV2MagicRules)
		{
			magicRules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
			magicRules.Struct().erase("schoolRankPowerCoefficientPercent");
			magicRules.Struct().erase("spellcraftEfficiencyPercent");
			for(auto & [name, spellRow] : magicRules["spells"].Struct())
			{
				(void)name;
				spellRow.Struct().erase("selectedPlacement");
			}
		}
		newHorizonsMagic::validateRules(magicRules);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);
	}

	bool configureCaster(CGHeroInstance * hero, int32_t spellPower,
		MasteryLevel::Type shadowRank = MasteryLevel::NONE,
	MasteryLevel::Type spellcraftRank = MasteryLevel::NONE)
	{
		const auto shadowMagic = SecondarySkill::decode(shadowMagicSkillKey);
		const auto spellcraft = SecondarySkill::decode(std::string(newHorizonsMagic::SPELLCRAFT_SKILL));
		if(shadowMagic < 0 || spellcraft < 0)
			return false;
		hero->setSecSkillLevel(SecondarySkill(shadowMagic), shadowRank, ChangeValueMode::ABSOLUTE);
		hero->setSecSkillLevel(SecondarySkill(spellcraft), spellcraftRank, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		hero->addSpellToSpellbook(spell);
		setTestSpellPointTotal(hero, 1000);
		return true;
	}

	void acceptPainweaverFromBasicOffer(CGHeroInstance * hero)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offers.begin(), offers.end(), [](const auto & offer)
			{
				return offer.selection.skillId == shadowMagicSkillKey
					&& offer.selection.perkId == painweaverPerkKey;
			});
			if(selected == offers.end())
				continue;

			ASSERT_EQ(selected->requiredRank, static_cast<int>(MasteryLevel::BASIC));
			const auto choice = static_cast<size_t>(std::distance(offers.begin(), selected));
			gameHandler->levelUpHero(hero, offers, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(shadowMagicSkillKey, painweaverPerkKey));
			return;
		}
		FAIL() << "Painweaver was not available as a Basic New Horizons perk offer";
	}

	std::vector<const Bonus *> hexBonuses(const CStack * unit) const
	{
		std::vector<const Bonus *> result;
		for(const auto & bonus : unit->getExportedBonusList())
		{
			if(bonus->type == BonusType::COMBAT_EVENT_TRIGGER
				&& bonus->subtype == BonusSubtypeID(combatEvent)
				&& bonus->source == BonusSource::SPELL_EFFECT
				&& bonus->sid == BonusSourceID(spell))
				result.push_back(bonus.get());
		}
		return result;
	}

	void expectHex(CStack * unit, int32_t flatDamage, BattleSide casterSide) const
	{
		const auto bonuses = hexBonuses(unit);
		ASSERT_EQ(bonuses.size(), 1u);
		const auto * bonus = bonuses.front();
		EXPECT_EQ(bonus->val, flatDamage);
		EXPECT_EQ(bonus->duration, BonusDuration::N_TURNS);
		EXPECT_EQ(bonus->turnsRemain, 3);
		ASSERT_NE(bonus->parameters, nullptr);
		const auto parameters = bonus->parameters->toCustom<JsonNode>();
		EXPECT_EQ(parameters["damageSharePercent"].Integer(), 10);
		EXPECT_EQ(parameters["casterSide"].Integer(), static_cast<int32_t>(casterSide));
	}
};
}

TEST_F(NewHorizonsHexOfPainTest, V3SnapshotsSchoolAndSpellcraftScalingOnThePowerTerm)
{
	startGame();
	ASSERT_TRUE(configureCaster(attackerSideHero, 10, MasteryLevel::BASIC, MasteryLevel::BASIC));
	startBattle();
	auto * cursed = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	ASSERT_NE(cursed, nullptr);
	battle()->nextRound();

	ASSERT_TRUE(castOn(attackerSideHero, spell, cursed));
	// floor(0.7 * 10 * 115% * 110%) = 8; the fixed 15-point base is unscaled.
	expectHex(cursed, 23, BattleSide::ATTACKER);
	EXPECT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(),
		newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION);

	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
	ASSERT_NE(friendly, nullptr);
	EXPECT_FALSE(castOn(attackerSideHero, spell, friendly));
	EXPECT_TRUE(hexBonuses(friendly).empty());
}

TEST_F(NewHorizonsHexOfPainTest, UnselectedPainweaverLeavesTheV3PowerTermUnchanged)
{
	startGame();
	ASSERT_TRUE(configureCaster(attackerSideHero, 10, MasteryLevel::BASIC));
	ASSERT_FALSE(attackerSideHero->hasActivePerk(shadowMagicSkillKey, painweaverPerkKey));
	startBattle();
	auto * cursed = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	ASSERT_NE(cursed, nullptr);
	battle()->nextRound();

	ASSERT_TRUE(castOn(attackerSideHero, spell, cursed));
	expectHex(cursed, 23, BattleSide::ATTACKER);
}

TEST_F(NewHorizonsHexOfPainTest, AcceptedBasicOfferScalesTheV3PowerTermOnceAndKeepsTheTenPercentShare)
{
	startGame();
	ASSERT_TRUE(configureCaster(defenderSideHero, 10, MasteryLevel::BASIC));
	acceptPainweaverFromBasicOffer(defenderSideHero);
	startBattle();
	auto * cursed = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	ASSERT_NE(cursed, nullptr);
	ASSERT_NE(target, nullptr);
	blockRetaliation(cursed);
	battle()->nextRound();

	ASSERT_TRUE(castOn(defenderSideHero, spell, cursed));
	// Basic Shadow Magic contributes 115%; Painweaver adds 20% to that component:
	// floor(0.7 * 10 * 115% * 120%) = 9, added to the fixed 15-point base.
	expectHex(cursed, 24, BattleSide::DEFENDER);
	const int64_t storedSnapshot = hexBonuses(cursed).front()->val;
	ASSERT_EQ(storedSnapshot, 24);

	defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	const auto targetBefore = target->getAvailableHealth();
	const auto cursedBefore = cursed->getAvailableHealth();
	server.attacks.clear();
	server.injuries.clear();
	forceMaximumDamage(cursed);
	ASSERT_TRUE(attack(cursed, target->getPosition()));

	const auto attackResult = std::find_if(server.attacks.begin(), server.attacks.end(), [cursed](const auto & result)
	{
		return result.stackAttacking == cursed->unitId() && !result.counter();
	});
	ASSERT_NE(attackResult, server.attacks.end());
	int64_t actualDamage = 0;
	for(const auto & hit : attackResult->bsa)
		if(hit.stackAttacked == target->unitId())
			actualDamage += hit.damageAmount;
	ASSERT_GT(actualDamage, 10);
	EXPECT_EQ(targetBefore - target->getAvailableHealth(), actualDamage);

	int64_t reactiveDamage = -1;
	for(const auto & injury : server.injuries)
	{
		for(const auto & stack : injury.stacks)
		{
			if(stack.stackAttacked == cursed->unitId() && stack.attackerID == target->unitId())
				reactiveDamage = stack.damageAmount;
		}
	}
	EXPECT_EQ(reactiveDamage, storedSnapshot + actualDamage / 10);
	EXPECT_EQ(cursedBefore - cursed->getAvailableHealth(), reactiveDamage);
}

TEST_F(NewHorizonsHexOfPainTest, PainweaverDoesNotIncreaseTheFixedBaseAtZeroSpellPower)
{
	startGame();
	ASSERT_TRUE(configureCaster(attackerSideHero, 0, MasteryLevel::BASIC));
	acceptPainweaverFromBasicOffer(attackerSideHero);
	startBattle();
	auto * cursed = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	ASSERT_NE(cursed, nullptr);
	battle()->nextRound();

	ASSERT_TRUE(castOn(attackerSideHero, spell, cursed));
	expectHex(cursed, 15, BattleSide::ATTACKER);
}

TEST_F(NewHorizonsHexOfPainTest, SavedV2UsesTheUnrankedSpellPowerCoefficient)
{
	useV2MagicRules = true;
	startGame();
	ASSERT_TRUE(configureCaster(attackerSideHero, 10, MasteryLevel::EXPERT, MasteryLevel::EXPERT));
	startBattle();
	auto * cursed = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	ASSERT_NE(cursed, nullptr);
	battle()->nextRound();

	ASSERT_TRUE(castOn(attackerSideHero, spell, cursed));
	EXPECT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(),
		newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION);
	EXPECT_FALSE(battle()->getMagicRules().Struct().contains("schoolRankPowerCoefficientPercent"));
	expectHex(cursed, 22, BattleSide::ATTACKER);
}

TEST_F(NewHorizonsHexOfPainTest, SelectedPainweaverIsIgnoredBySavedV2Rules)
{
	useV2MagicRules = true;
	startGame();
	ASSERT_TRUE(configureCaster(attackerSideHero, 10, MasteryLevel::BASIC));
	acceptPainweaverFromBasicOffer(attackerSideHero);
	startBattle();
	auto * cursed = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	ASSERT_NE(cursed, nullptr);
	battle()->nextRound();

	ASSERT_TRUE(castOn(attackerSideHero, spell, cursed));
	EXPECT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(),
		newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION);
	expectHex(cursed, 22, BattleSide::ATTACKER);
}

TEST_F(NewHorizonsHexOfPainTest, AttackUsesActualDamageClippedToTheTargetHealth)
{
	startGame();
	ASSERT_TRUE(configureCaster(defenderSideHero, 0));
	startBattle();
	auto * cursed = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
	auto * oneHealthTarget = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(rightHex), 1);
	ASSERT_NE(cursed, nullptr);
	ASSERT_NE(oneHealthTarget, nullptr);
	battle()->nextRound();
	ASSERT_TRUE(castOn(defenderSideHero, spell, cursed));
	expectHex(cursed, 15, BattleSide::DEFENDER);

	blockRetaliation(oneHealthTarget);
	forceMaximumDamage(cursed);
	const auto healthBefore = cursed->getAvailableHealth();
	server.injuries.clear();
	ASSERT_TRUE(attack(cursed, oneHealthTarget->getPosition()));
	EXPECT_FALSE(oneHealthTarget->alive());
	EXPECT_EQ(server.attacks.size(), 1u);
	ASSERT_FALSE(server.injuries.empty());

	const auto & reactiveInjury = server.injuries.back().stacks;
	ASSERT_EQ(reactiveInjury.size(), 1u);
	EXPECT_EQ(reactiveInjury.front().stackAttacked, cursed->unitId());
	EXPECT_EQ(reactiveInjury.front().attackerID, oneHealthTarget->unitId());
	EXPECT_EQ(reactiveInjury.front().damageAmount, 15);
	EXPECT_EQ(healthBefore - cursed->getAvailableHealth(), 15);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Hex of Pain") != std::string::npos;
	})) << testing::PrintToString(server.battleLogLines);
}

TEST_F(NewHorizonsHexOfPainTest, RetaliationTriggersItsOwnReactiveShadowDamage)
{
	startGame();
	ASSERT_TRUE(configureCaster(attackerSideHero, 0));
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
	auto * cursed = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 101);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(cursed, nullptr);
	battle()->nextRound();
	ASSERT_TRUE(castOn(attackerSideHero, spell, cursed));
	expectHex(cursed, 15, BattleSide::ATTACKER);
	forceMaximumDamage(attacker);
	forceMaximumDamage(cursed);

	server.attacks.clear();
	server.injuries.clear();
	ASSERT_TRUE(attack(attacker, cursed->getPosition()));
	EXPECT_EQ(server.attacks.size(), 2u);
	const auto counter = std::ranges::find_if(server.attacks, [](const BattleAttack & attack)
	{
		return attack.counter();
	});
	ASSERT_NE(counter, server.attacks.end());
	int64_t counterDamage = 0;
	for(const auto & hit : counter->bsa)
	{
		if(hit.stackAttacked == attacker->unitId())
			counterDamage += hit.damageAmount;
	}
	ASSERT_GT(counterDamage, 0);
	ASSERT_FALSE(server.injuries.empty());

	const auto & reactiveInjury = server.injuries.back().stacks;
	ASSERT_EQ(reactiveInjury.size(), 1u);
	EXPECT_EQ(reactiveInjury.front().stackAttacked, cursed->unitId());
	EXPECT_EQ(reactiveInjury.front().attackerID, attacker->unitId());
	EXPECT_EQ(reactiveInjury.front().damageAmount, 15 + counterDamage / 10); // integer division floors the 10% share
}
