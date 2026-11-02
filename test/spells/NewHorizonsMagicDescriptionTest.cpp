/*
 * NewHorizonsMagicDescriptionTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../mock/mock_IGameInfoCallback.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "HistoricalCrusadeRules.h"

#include <array>
#include <stdexcept>

namespace
{
class DescriptionRulesCallback final : public ::testing::NiceMock<IGameInfoCallbackMock>
{
public:
	JsonNode rules;
	const JsonNode & getMagicRules() const override { return rules; }
};

// Project only fields supported by these old saved contexts, then run the real
// validator. No current definition is mutated and no installed fallback is used.
JsonNode historicalDescriptionRules(int version)
{
	const JsonNode current = historicalCrusadeRules(JsonNode(JsonPath::builtin("config/newHorizonsMagic")), version);
	JsonNode result;
	for(const auto * field : {"schemaVersion", "schools", "schoolSkills", "skillReplacements", "adventureSpells"})
		result[field] = current[field];
	result["rulesetVersion"].Integer() = version;
	result["adventureSpells"]["core:waterWalk"].Struct().erase("requireLegalDayEnd");
	for(const auto & [key, row] : current["spells"].Struct())
	{
		if(version == 1 && key.starts_with("new-horizons:"))
			continue;
		for(const auto * field : {"schools", "level", "costs", "ordinaryAcquisition"})
			if(row.Struct().contains(field))
				result["spells"][key][field] = row[field];
		if(version == 2)
		{
			for(const auto * field : {"directDamage", "active"})
				if(row.Struct().contains(field))
					result["spells"][key][field] = row[field];
			if(row.Struct().contains("variant"))
				result["spells"][key]["active"].Bool() = false;
		}
	}
	newHorizonsMagic::validateRules(result);
	return result;
}

const CSpell * descriptionSpell(const char * key)
{
	const SpellID id(SpellID::decode(key));
	if(!id.hasValue() || !id.toSpell())
		throw std::runtime_error("Description fixture requires registered spell: " + std::string(key));
	return id.toSpell();
}

constexpr auto spellcraftSuffix = "\n\nSpellcraft: 110% of normal strength from Spell Power, applied after School rank.";
}

class NewHorizonsMagicDescriptionTest : public ::testing::Test
{
protected:
	DescriptionRulesCallback callback;
	CGHeroInstance hero{&callback};

	void SetUp() override
	{
		callback.rules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		ASSERT_NO_THROW(newHorizonsMagic::validateRules(callback.rules));
		hero.setHeroType(HeroTypeID(0));
		hero.pushPrimSkill(PrimarySkill::SPELL_POWER, 1);
		ASSERT_NE(hero.getLocalBonus(Selector::typeSubtype(BonusType::PRIMARY_SKILL,
			BonusSubtypeID(PrimarySkill::SPELL_POWER)).And(Selector::sourceType()(BonusSource::HERO_BASE_SKILL))), nullptr);
		ASSERT_EQ(hero.getPrimSkillLevel(PrimarySkill::SPELL_POWER), 1);
		ASSERT_TRUE(hero.getPerkState().selected.empty());
	}

	void rank(const CSpell * spell, int value, int spellcraft = 0)
	{
		hero.secSkills.clear();
		for(const auto skill : newHorizonsMagic::spellSchoolSkills(callback.rules, spell->getId()))
			hero.secSkills.emplace_back(skill, value);
		if(spellcraft)
			hero.secSkills.emplace_back(SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")), spellcraft);
	}

	std::string describe(const CSpell * spell, int level = 0)
	{
		return newHorizonsMagic::spellDescriptionForHero(&hero, spell, level);
	}
};

TEST_F(NewHorizonsMagicDescriptionTest, NullSpellAndNullHeroPreserveExactTranslatedFallbackAtEveryRank)
{
	EXPECT_EQ(describe(nullptr), "");
	EXPECT_EQ(newHorizonsMagic::spellDescriptionForHero(nullptr, nullptr, 0), "");
	for(const auto * key : {"core:cure", "core:slow", "core:dispel", "new-horizons:focusMagic"})
	{
		const auto * spell = descriptionSpell(key);
		for(int level = 0; level <= 3; ++level)
			EXPECT_EQ(newHorizonsMagic::spellDescriptionForHero(nullptr, spell, level), spell->getDescriptionTranslated(level));
	}
}

TEST_F(NewHorizonsMagicDescriptionTest, AbsentAndEmptySavedRulesDoNotUseInstalledDescriptions)
{
	JsonNode empty;
	empty.Struct();
	ASSERT_TRUE(empty.isStruct());
	for(const auto & absent : {JsonNode(), empty})
	{
		callback.rules = absent;
		ASSERT_FALSE(newHorizonsMagic::rulesActive(hero.getMagicRules()));
		for(const auto * key : {"core:cure", "core:slow", "core:chainLightning"})
		{
			const auto * spell = descriptionSpell(key);
			for(int level = 0; level <= 3; ++level)
				EXPECT_EQ(describe(spell, level), spell->getDescriptionTranslated(level));
		}
	}
}

TEST_F(NewHorizonsMagicDescriptionTest, ValidHistoricalV1AndV2KeepOrdinaryCoreTextAtEveryRank)
{
	for(int version : {1, 2})
	{
		ASSERT_NO_THROW(callback.rules = historicalDescriptionRules(version));
		for(const auto * key : {"core:cure", "core:slow", "core:dispel", "core:chainLightning"})
		{
			const auto * spell = descriptionSpell(key);
			for(int level = 0; level <= 3; ++level)
			{
				rank(spell, level);
				EXPECT_EQ(describe(spell, level), spell->getDescriptionTranslated(level)) << key << " saved v" << version;
			}
		}
	}
	callback.rules = historicalDescriptionRules(1);
	const auto * absent = descriptionSpell("new-horizons:focusMagic");
	ASSERT_FALSE(callback.rules["spells"].Struct().contains(absent->getJsonKey()));
	EXPECT_EQ(describe(absent), absent->getDescriptionTranslated(0));
}

TEST_F(NewHorizonsMagicDescriptionTest, CureRankReadoutsAreByteExactIncludingFractionalCoefficients)
{
	const auto * cure = descriptionSpell("core:cure");
	const std::array<std::string, 4> healing{"1.5", "1.725", "1.95", "2.175"};
	const std::array<std::string, 4> rankText{"No School Rank: 100", "Basic School: 115", "Advanced School: 130", "Expert School: 145"};
	for(int level = 0; level <= 3; ++level)
	{
		rank(cure, level);
		const auto expected = "Heal one friendly living stack without restoring dead creatures. If the target has Poison or Disease, choose one physical "
			"affliction to remove. Magical effects remain.\n\nBase healing: 25 + "
			+ healing.at(level) + " × Spell Power HP.\n\n" + rankText.at(level) + "% of normal healing from Spell Power.";
		EXPECT_EQ(describe(cure, level), expected);
	}
}

TEST_F(NewHorizonsMagicDescriptionTest, CureSpellcraftPrecisionAndSuffixOrderAreByteExact)
{
	const auto * cure = descriptionSpell("core:cure");
	rank(cure, 1, 1);
	EXPECT_EQ(describe(cure), std::string("Heal one friendly living stack without restoring dead creatures. If the target has Poison or Disease, choose one physical "
		"affliction to remove. Magical effects remain.\n\nBase healing: 25 + 1.8975 × Spell Power HP.")
		+ spellcraftSuffix + "\n\nBasic School: 115% of normal healing from Spell Power.");
}

TEST_F(NewHorizonsMagicDescriptionTest, MissingCureOptInAndDisabledRosterRetainExactOrdinaryFallback)
{
	const auto * cure = descriptionSpell("core:cure");
	callback.rules["spells"]["core:cure"].Struct().erase("cureAfflictions");
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(callback.rules));
	EXPECT_EQ(describe(cure), cure->getDescriptionTranslated(0));
	const auto * slow = descriptionSpell("core:slow");
	callback.rules["spells"]["core:slow"]["active"].Bool() = false;
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(callback.rules));
	EXPECT_EQ(describe(slow), slow->getDescriptionTranslated(0));
}

TEST_F(NewHorizonsMagicDescriptionTest, SlowAndMassSlowRankReadoutsKeepCapsRoundingAndParagraphOrder)
{
	hero.setPrimarySkill(PrimarySkill::SPELL_POWER, 50, ChangeValueMode::ABSOLUTE);
	const std::array<std::string, 4> names{"No rank", "Basic", "Advanced", "Expert"};
	const std::array<std::string, 4> coefficients{"100", "115", "130", "145"};
	const std::array<std::string, 4> ordinary{"30", "31", "33", "34"};
	const std::array<std::string, 4> mass{"18", "18", "19", "20"};
	for(const bool isMass : {false, true})
	{
		const auto * spell = descriptionSpell(isMass ? "new-horizons:massSlow" : "core:slow");
		for(int level = 0; level <= 3; ++level)
		{
			rank(spell, level);
			std::string expected = isMass ? "Affects every eligible enemy stack. " : "Target one enemy stack. ";
			expected += "Reduces Speed and Initiative equally. Fixed base duration: 2 rounds before Temporalist, "
				"other Spell Duration bonuses, and eligible cast-specific extensions. Current Sorcery rank: " + names.at(level)
				+ " (School factor " + coefficients.at(level) + "%, combined School and Spellcraft factor " + coefficients.at(level)
				+ "%). Speed and Initiative reduction = min(50%, 20% + floor(Spell Power x combined coefficient / 5)); "
				"current reduction before target-specific specialties at Spell Power 50: " + (isMass ? mass.at(level) : ordinary.at(level))
				+ "%. This estimate excludes battle-only Warcasting and the Inferno defender's Brimstone Stormclouds "
				"+20 Spell Power bonus, which can further affect the battle cast.";
			if(isMass)
				expected += " Mass Slow applies 60% of the final ordinary Slow reduction after its cap and target-specific "
					"specialties, rounded toward zero; duration is unchanged. Temporal Field grants this distinct spell, "
					"without a once-per-combat limit. Its listed Mana cost is three times Slow before Wisdom.";
			EXPECT_EQ(describe(spell, level), expected);
		}
	}
}

TEST_F(NewHorizonsMagicDescriptionTest, ImplosionKeepsSixDecimalFormattingAndSchoolSuffix)
{
	const auto * spell = descriptionSpell("core:implosion");
	EXPECT_EQ(describe(spell), "Deals min(30.000000%, 12.000000% + 0.100000% x scaled Spell Power) "
		"of the target's current aggregate Health before magical defenses. School rank scales only the Spell Power term, "
		"not the base or cap. After resolved damage, other living stacks within 3 hexes are pulled one legal hex closer. "
		"Closest stacks move first, then clockwise from northeast. The primary target stays in place; this magical "
		"displacement causes no retaliation.\n\nNo School Rank: 100% of normal damage from Spell Power.");
}

TEST_F(NewHorizonsMagicDescriptionTest, FocusCurrentAndHistoricalProfilesKeepExactValuesInPlainReadout)
{
	const auto * focus = descriptionSpell("new-horizons:focusMagic");
	hero.setPrimarySkill(PrimarySkill::SPELL_POWER, 17, ChangeValueMode::ABSOLUTE);
	rank(focus, 1, 1);
	EXPECT_EQ(describe(focus), focus->getDescriptionTranslated(0)
		+ "\n\nYour hero: each mark lets friendly ranged attacks ignore 5.64% Creature Defense; "
		"3 such marks: 16.92%. Battle bonuses may increase this.");
	ASSERT_NO_THROW(callback.rules = historicalDescriptionRules(2));
	rank(focus, 3);
	EXPECT_EQ(describe(focus), focus->getDescriptionTranslated(0)
		+ "\n\nYour hero: each mark lets friendly ranged attacks ignore 10.85% Creature Defense; "
		"3 such marks: 32.55%. Battle bonuses may increase this.");
}

TEST_F(NewHorizonsMagicDescriptionTest, MasterChainRetentionTracksLevelAndCapWithSchoolSuffixLast)
{
	const auto * spell = descriptionSpell("new-horizons:masterChainLightning");
	for(const auto & [level, retention] : std::array<std::pair<int, const char *>, 3>{{{1, "76"}, {12, "87"}, {99, "90"}}})
	{
		hero.level = level;
		EXPECT_EQ(describe(spell), spell->getDescriptionTranslated(0) + "\n\nCurrent retention: " + retention
			+ "%.\n\nNo School Rank: 100% of normal damage from Spell Power.");
	}
}

TEST_F(NewHorizonsMagicDescriptionTest, TimeStopIndependentReplacementKeepsCompleteCurrentReadout)
{
	EXPECT_EQ(describe(descriptionSpell("new-horizons:timeStop")),
		"Stop time in the selected area until the beginning of your next Hero Action. Creatures in stasis cannot move, "
		"act, retaliate, take damage, receive healing, teleport, or receive new effects. Their existing effect timers "
		"are paused, and they still block their occupied hexes.\n\n"
		"Radius before battle-only Warcasting (hexes): 1 at Spell Power 1. Sorcery and Spellcraft strengthen only the Spell Power part "
		"(100% of normal strength). Maximum radius: 2 hexes. Chronomancer raises the maximum to 3 without adding "
		"a free radius step. Battle-only Warcasting may increase the radius through Spell Power. It does not extend "
		"the duration or change which targets can be affected.");
}

class NewHorizonsMagicPerkDescriptionTest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
};

TEST_F(NewHorizonsMagicPerkDescriptionTest, InitialFateDealerAppendPrecedesSpellcraftAndSchoolSuffix)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto chaos = SecondarySkill(SecondarySkill::decode("new-horizons:chaosMagic"));
	attackerSideHero->setSecSkillLevel(chaos, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:chaosMagic", "new-horizons:chaosMagic.misfortuneWeaver"});
	attackerSideHero->setSecSkillLevel(chaos, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:chaosMagic", "new-horizons:chaosMagic.fateDealer"});
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")), 1, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.fateDealer"));
	const auto * spell = descriptionSpell("new-horizons:handOfFate");
	EXPECT_EQ(newHorizonsMagic::spellDescriptionForHero(attackerSideHero, spell, 0), spell->getDescriptionTranslated(0)
		+ " Fate Dealer draws two spill candidates independently with replacement. If exactly one is hostile to your "
		"current side, it receives the spill; otherwise a fair coin chooses between the two draws, including duplicates. "
		"The selected recipient's own defenses still apply, without a reroll or a second hit."
		+ spellcraftSuffix + "\n\nAdvanced School: 130% of normal damage from Spell Power.");
}

TEST_F(NewHorizonsMagicPerkDescriptionTest, BlessDurationBonusesBenedictionAndSpellcraftKeepExactSuffixOrder)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic")), 1, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:lightMagic", "new-horizons:lightMagic.benediction"});
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")), 1, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL_DURATION,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID()));
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL_DURATION,
		BonusSource::OTHER, 2, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::BLESS))));
	const auto * spell = descriptionSpell("core:bless");
	EXPECT_EQ(newHorizonsMagic::spellDescriptionForHero(attackerSideHero, spell, 0), spell->getDescriptionTranslated(0)
		+ "\n\nCurrent ordinary duration: 2 rounds (combined Spell Power coefficient: 126.5%). "
		"Current total before battle-only adjustments: 6 rounds. SPELL_DURATION modifier: 3. Benediction adds 1 round."
		+ spellcraftSuffix);
}

TEST_F(NewHorizonsMagicDescriptionTest, LivingArmorUsesCapturedPoolAndLifetimeAndKeepsMissingProfileFallback)
{
	const auto * spell = descriptionSpell("new-horizons:summonLivingArmor");
	// This uninitialized legacy hero clamps primary ratings at 99; stay below that cap.
	hero.setPrimarySkill(PrimarySkill::SPELL_POWER, 80, ChangeValueMode::ABSOLUTE);
	rank(spell, 1, 1);
	SCOPED_TRACE(describe(spell));
	ASSERT_EQ(hero.getEffectPower(spell), 80);
	ASSERT_EQ(hero.getEffectPowerDivisor(spell), 1);
	ASSERT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(callback.rules, &hero, spell->getId()), 12650);
	EXPECT_NE(describe(spell).find("Current pool: 141 HP before battle-only Warcasting. Lifetime: 3 rounds"), std::string::npos);
	auto & profile = callback.rules["spells"]["new-horizons:summonLivingArmor"]["livingArmor"];
	profile["baseHitPoints"].Integer() = 7;
	profile["spellPowerCoefficient"].Integer() = 2;
	profile["duration"].Integer() = 5;
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(callback.rules));
	{
		const auto actualReadout = describe(spell);
		SCOPED_TRACE(actualReadout);
		EXPECT_NE(actualReadout.find("Current pool: 209 HP before battle-only Warcasting. Lifetime: 5 rounds"), std::string::npos);
	}
	EXPECT_NE(describe(spell).find("Extend Spell and Echoed Duration each add one round"), std::string::npos);
	profile["spellPowerCoefficient"].Integer() = 0;
	EXPECT_NE(describe(spell).find("Current pool: 7 HP"), std::string::npos);
	callback.rules["spells"]["new-horizons:summonLivingArmor"].Struct().erase("livingArmor");
	EXPECT_EQ(describe(spell), spell->getDescriptionTranslated(0) + spellcraftSuffix);
}
