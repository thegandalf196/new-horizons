/*
 * NewHorizonsSpellRosterConsumerTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../spells/NewHorizonsMagicProfileFixture.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/callback/CGameInfoCallback.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/rewardable/Reward.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/texts/CGeneralTextHandler.h"
#include "../../../lib/texts/MetaString.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include <limits>

namespace
{
SpellID spellNamed(const std::string & name) { return SpellID(SpellID::decode(name)); }

// Intentionally adversarial read context, not a valid partial-core save. Actual
// managed-identity import and valid old/new saved-roster tests are a later gate.
class ExcludingWorld final : public CGameInfoCallback
{
	CGameState & state;
	JsonNode rules;
	GameSettings settings;
public:
	ExcludingWorld(CGameState & state, const std::string & excluded) : state(state), rules(state.getMagicRules())
	{
		rules["spells"].Struct().erase(excluded);
		settings.loadBase(state.getSettings().getFullConfig());
		settings.addOverride(EGameSettings::SPELLS_TOMES_GRANT_BANNED_SPELLS, JsonNode(true));
	}
	CGameState & gameState() override { return state; }
	const CGameState & gameState() const override { return state; }
	const JsonNode & getMagicRules() const override { return rules; }
	const IGameSettings & getSettings() const override { return settings; }
};

class LegacyMagicWorld final : public CGameInfoCallback
{
	CGameState & state;
	JsonNode rules;
public:
	explicit LegacyMagicWorld(CGameState & state) : state(state) {}
	CGameState & gameState() override { return state; }
	const CGameState & gameState() const override { return state; }
	const JsonNode & getMagicRules() const override { return rules; }
};

class ScopedHeroCallback
{
	CGHeroInstance & hero;
	IGameInfoCallback * prior;
public:
	ScopedHeroCallback(CGHeroInstance & hero, IGameInfoCallback * replacement)
		: hero(hero), prior(hero.cb) { hero.cb = replacement; }
	~ScopedHeroCallback() { hero.cb = prior; }
};

class ScopedHeroSpellExclusion
{
	CHero * heroType;
	SpellID spell;
	bool inserted;
public:
	ScopedHeroSpellExclusion(CGHeroInstance & hero, SpellID spell)
		: heroType(const_cast<CHero *>(hero.getHeroType())), spell(spell), inserted(heroType->excludedSpells.insert(spell).second) {}
	~ScopedHeroSpellExclusion()
	{
		if(inserted)
			heroType->excludedSpells.erase(spell);
	}
};

class ExcludingBattle final : public BattleInfo
{
	JsonNode rules;
public:
	ExcludingBattle(IGameInfoCallback * world, JsonNode rules) : BattleInfo(world), rules(std::move(rules)) {}
	const JsonNode & getMagicRules() const override { return rules; }
};

class ScopedArmyBattleLinks
{
	CGHeroInstance & left;
	CGHeroInstance & right;
	BattleInfo * leftBattle;
	BattleInfo * rightBattle;
public:
	ScopedArmyBattleLinks(CGHeroInstance & left, CGHeroInstance & right)
		: left(left), right(right), leftBattle(left.battle), rightBattle(right.battle) {}
	~ScopedArmyBattleLinks() { left.battle = leftBattle; right.battle = rightBattle; }
};
}

class NewHorizonsSpellRosterConsumerTest : public HeroCommandFixture
{
protected:
	std::unique_ptr<newHorizonsTest::MagicV1Baseline> baseline;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		baseline = std::make_unique<newHorizonsTest::MagicV1Baseline>();
	}
	void TearDown() override
	{
		HeroCommandFixture::TearDown();
		baseline.reset();
	}
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepareHero(bool book = true)
	{
		startGame();
		attackerSideHero->removeAllSpells();
		if(book)
			giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	}
	void removeNewHorizonsSchoolRanks()
	{
		for(const auto skill : newHorizonsMagic::schoolSkills(gameState()->getMagicRules()))
			attackerSideHero->setSecSkillLevel(skill, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	}
	ArtifactID spellbindersHat() const { return ArtifactID::decode("core:spellbindersHat"); }
};

TEST_F(NewHorizonsSpellRosterConsumerTest, ExcludedKnownBookRemainsStoredButCannotGrant)
{
	prepareHero();
	const auto arrow = spellNamed("core:magicArrow");
	attackerSideHero->addSpellToSpellbook(arrow);
	ASSERT_FALSE(attackerSideHero->getSourcesForSpell(arrow).empty());
	ExcludingWorld excluded(*gameState(), "core:magicArrow");
	EXPECT_THROW(newHorizonsMagic::validateRules(excluded.getMagicRules()), std::runtime_error);
	ScopedHeroCallback context(*attackerSideHero, &excluded);
	EXPECT_TRUE(attackerSideHero->getSpellsInSpellbook().count(arrow));
	EXPECT_TRUE(attackerSideHero->getSourcesForSpell(arrow).empty());
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(arrow.toSpell()));
}

TEST_F(NewHorizonsSpellRosterConsumerTest, SpecificSpellBonusCannotBypassRoster)
{
	prepareHero(false);
	const auto arrow = spellNamed("core:magicArrow");
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(arrow)));
	ASSERT_FALSE(attackerSideHero->getSourcesForSpell(arrow).empty());
	ExcludingWorld excluded(*gameState(), "core:magicArrow");
	ScopedHeroCallback context(*attackerSideHero, &excluded);
	EXPECT_TRUE(attackerSideHero->getSourcesForSpell(arrow).empty());
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(arrow.toSpell()));
}

TEST_F(NewHorizonsSpellRosterConsumerTest, SchoolGrantAndTomesGrantBannedSettingCannotBypassRoster)
{
	prepareHero();
	const auto arrow = spellNamed("core:magicArrow");
	const auto schools = attackerSideHero->getSpellSchools(arrow.toSpell());
	ASSERT_FALSE(schools.empty());
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELLS_OF_SCHOOL,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(schools.front())));
	ASSERT_FALSE(attackerSideHero->getSourcesForSpell(arrow).empty());
	ExcludingWorld excluded(*gameState(), "core:magicArrow");
	ScopedHeroCallback context(*attackerSideHero, &excluded);
	ASSERT_TRUE(excluded.getSettings().getBoolean(EGameSettings::SPELLS_TOMES_GRANT_BANNED_SPELLS));
	EXPECT_TRUE(attackerSideHero->getSourcesForSpell(arrow).empty());
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(arrow.toSpell()));
}

TEST_F(NewHorizonsSpellRosterConsumerTest, LevelGrantCannotBypassRoster)
{
	prepareHero();
	const auto implosion = spellNamed("core:implosion");
	const auto level = attackerSideHero->getSpellLevel(implosion.toSpell());
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELLS_OF_LEVEL,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(BonusCustomSubtype::spellLevel(level))));
	ASSERT_FALSE(attackerSideHero->getSourcesForSpell(implosion).empty());
	ExcludingWorld excluded(*gameState(), "core:implosion");
	ScopedHeroCallback context(*attackerSideHero, &excluded);
	EXPECT_TRUE(attackerSideHero->getSourcesForSpell(implosion).empty());
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(implosion.toSpell()));
}

TEST_F(NewHorizonsSpellRosterConsumerTest, SpellbindersHatTemporarilyInscribesEligibleLevelFiveCombatSpells)
{
	prepareHero();
	removeNewHorizonsSchoolRanks();
	const auto armageddon = spellNamed("core:armageddon");
	ASSERT_EQ(attackerSideHero->getSpellLevel(armageddon.toSpell()), 5);
	ASSERT_TRUE(armageddon.toSpell()->isCommonHeroSpell());
	ASSERT_TRUE(armageddon.toSpell()->isCombat());
	ASSERT_FALSE(attackerSideHero->spellbookContainsSpell(armageddon));
	ASSERT_FALSE(attackerSideHero->isSpellInscribedForCasting(armageddon));

	giveArtifact(attackerSideHero, spellbindersHat(), ArtifactPosition::HEAD);

	EXPECT_FALSE(newHorizonsMagic::hasSchoolProficiency(attackerSideHero, armageddon));
	EXPECT_TRUE(attackerSideHero->isSpellInscribedForCasting(armageddon));
	EXPECT_TRUE(vstd::contains(attackerSideHero->getInscribedSpellsForCasting(), armageddon));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(armageddon));
	EXPECT_TRUE(attackerSideHero->getSpellsInSpellbook().empty());
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(armageddon.toSpell()));

	// Adventure spells do not become combat inscriptions, even if their legacy
	// spell definition has level five.
	const auto dimensionDoor = spellNamed("core:dimensionDoor");
	ASSERT_FALSE(dimensionDoor.toSpell()->isCombat());
	EXPECT_FALSE(attackerSideHero->isSpellInscribedForCasting(dimensionDoor));
}

TEST_F(NewHorizonsSpellRosterConsumerTest, SpellbindersHatStillRequiresPhysicalSpellbookManaAndHeroAction)
{
	prepareHero(false);
	removeNewHorizonsSchoolRanks();
	const auto armageddon = spellNamed("core:armageddon");
	giveArtifact(attackerSideHero, spellbindersHat(), ArtifactPosition::HEAD);

	EXPECT_TRUE(attackerSideHero->isSpellInscribedForCasting(armageddon));
	EXPECT_FALSE(attackerSideHero->hasSpellbook());
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(armageddon.toSpell()));

	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setNormalSpellPoints(0);
	startBattle();
	beginCombat();
	ASSERT_TRUE(attackerSideHero->canCastThisSpell(armageddon.toSpell()));

	BattleAction cast;
	cast.actionType = EActionType::HERO_SPELL;
	cast.side = BattleSide::ATTACKER;
	cast.spell = armageddon;
	// Global battlefield spells still require an explicit target entry in the
	// authoritative action protocol, represented by the invalid-hex sentinel.
	cast.aimToHex(BattleHex::INVALID);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), cast));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 0);

	attackerSideHero->setNormalSpellPoints(100);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), cast));
	EXPECT_LT(attackerSideHero->getManaAvailable(), 100);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), cast))
		<< "The Hat grants inscription, not a second hero spell action";
}

TEST_F(NewHorizonsSpellRosterConsumerTest, SpellbindersHatGrantRespectsMapAndSavedRosterBans)
{
	prepareHero();
	const auto armageddon = spellNamed("core:armageddon");
	giveArtifact(attackerSideHero, spellbindersHat(), ArtifactPosition::HEAD);
	ASSERT_TRUE(attackerSideHero->isSpellInscribedForCasting(armageddon));

	ASSERT_TRUE(gameState()->getMap().allowedSpells.count(armageddon));
	gameState()->getMap().allowedSpells.erase(armageddon);
	EXPECT_FALSE(attackerSideHero->isSpellInscribedForCasting(armageddon));
	EXPECT_TRUE(attackerSideHero->getSourcesForSpell(armageddon).empty());
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(armageddon.toSpell()));
	gameState()->getMap().allowedSpells.insert(armageddon);

	ExcludingWorld excluded(*gameState(), "core:armageddon");
	ScopedHeroCallback context(*attackerSideHero, &excluded);
	EXPECT_FALSE(attackerSideHero->isSpellInscribedForCasting(armageddon));
	EXPECT_TRUE(attackerSideHero->getSourcesForSpell(armageddon).empty());
}

TEST_F(NewHorizonsSpellRosterConsumerTest, SpellbindersHatGrantRespectsHeroExclusionsAndLegacyRules)
{
	prepareHero();
	const auto armageddon = spellNamed("core:armageddon");
	giveArtifact(attackerSideHero, spellbindersHat(), ArtifactPosition::HEAD);
	ASSERT_TRUE(attackerSideHero->isSpellInscribedForCasting(armageddon));
	{
		ScopedHeroSpellExclusion excluded(*attackerSideHero, armageddon);
		EXPECT_FALSE(attackerSideHero->isSpellInscribedForCasting(armageddon));
		EXPECT_TRUE(attackerSideHero->getSourcesForSpell(armageddon).empty());
	}
	EXPECT_TRUE(attackerSideHero->isSpellInscribedForCasting(armageddon));

	LegacyMagicWorld legacy(*gameState());
	ScopedHeroCallback legacyContext(*attackerSideHero, &legacy);
	EXPECT_FALSE(attackerSideHero->isSpellInscribedForCasting(armageddon));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(armageddon));
	// Armageddon is level four in the classic rules and only becomes level five
	// in the New Horizons roster, so the classic Spellbinder's Hat does not
	// grant it. A genuinely classic fifth-level spell remains available.
	ASSERT_EQ(armageddon.toSpell()->getLevel(), 4);
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(armageddon.toSpell()));
	const auto implosion = spellNamed("core:implosion");
	ASSERT_EQ(implosion.toSpell()->getLevel(), 5);
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(implosion));
	EXPECT_FALSE(attackerSideHero->isSpellInscribedForCasting(implosion));
	EXPECT_FALSE(attackerSideHero->getSourcesForSpell(implosion).empty());
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(implosion.toSpell()));
}

TEST_F(NewHorizonsSpellRosterConsumerTest, AllowBannedCannotLearnAnExcludedUnknownSpell)
{
	prepareHero();
	const auto arrow = spellNamed("core:magicArrow");
	ASSERT_TRUE(attackerSideHero->canLearnSpell(arrow.toSpell(), true));
	ExcludingWorld excluded(*gameState(), "core:magicArrow");
	ScopedHeroCallback context(*attackerSideHero, &excluded);
	EXPECT_FALSE(attackerSideHero->canLearnSpell(arrow.toSpell(), false));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(arrow.toSpell(), true));
}

TEST_F(NewHorizonsSpellRosterConsumerTest, SixSchoolRanksGateLearningButNotInscribedSpells)
{
	prepareHero();
	const auto animateDead = spellNamed("core:animateDead");
	const auto antiMagic = spellNamed("core:antiMagic");
	const auto armageddon = spellNamed("core:armageddon");
	const auto airElemental = spellNamed("core:airElemental");
	const auto summonBoat = spellNamed("core:summonBoat");
	const SecondarySkill shadow(SecondarySkill::decode("new-horizons:shadowMagic"));
	const SecondarySkill sorcery(SecondarySkill::decode("new-horizons:sorceryMagic"));
	const SecondarySkill havoc(SecondarySkill::decode("new-horizons:havocMagic"));
	const SecondarySkill nature(SecondarySkill::decode("new-horizons:natureMagic"));

	for(const auto skill : {shadow, sorcery, havoc, nature})
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);

	EXPECT_EQ(newHorizonsMagic::requiredSchoolRank(gameState()->getMagicRules(), animateDead), MasteryLevel::BASIC);
	EXPECT_EQ(newHorizonsMagic::requiredSchoolRank(gameState()->getMagicRules(), antiMagic), MasteryLevel::ADVANCED);
	EXPECT_EQ(newHorizonsMagic::requiredSchoolRank(gameState()->getMagicRules(), armageddon), MasteryLevel::EXPERT);
	EXPECT_EQ(newHorizonsMagic::requiredSchoolRank(gameState()->getMagicRules(), summonBoat), MasteryLevel::NONE);
	EXPECT_FALSE(attackerSideHero->canLearnSpell(animateDead.toSpell(), true));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(antiMagic.toSpell(), true));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(armageddon.toSpell(), true));
	EXPECT_TRUE(newHorizonsMagic::hasSchoolProficiency(attackerSideHero, summonBoat));

	attackerSideHero->addSpellToSpellbook(armageddon);
	ASSERT_FALSE(attackerSideHero->getSourcesForSpell(armageddon).empty());
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(armageddon.toSpell()));
	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(attackerSideHero->getSecSkillLevel(havoc), MasteryLevel::EXPERT);
	ASSERT_EQ(newHorizonsMagic::spellSchoolSkills(gameState()->getMagicRules(), armageddon), (std::vector<SecondarySkill>{havoc}));
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(armageddon.toSpell()));

	attackerSideHero->setSecSkillLevel(nature, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	const auto requiredMulti = newHorizonsMagic::requiredSchoolRank(gameState()->getMagicRules(), airElemental);
	ASSERT_GT(requiredMulti, MasteryLevel::NONE);
	EXPECT_FALSE(newHorizonsMagic::hasSchoolProficiency(attackerSideHero, airElemental));
	attackerSideHero->setSecSkillLevel(nature, requiredMulti, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(attackerSideHero->getSecSkillLevel(nature), requiredMulti);
	EXPECT_TRUE(newHorizonsMagic::hasSchoolProficiency(attackerSideHero, airElemental));

	LegacyMagicWorld legacy(*gameState());
	ScopedHeroCallback legacyContext(*attackerSideHero, &legacy);
	EXPECT_EQ(newHorizonsMagic::requiredSchoolRank(legacy.getMagicRules(), armageddon), MasteryLevel::NONE);
	EXPECT_TRUE(newHorizonsMagic::hasSchoolProficiency(attackerSideHero, armageddon));
}

TEST_F(NewHorizonsSpellRosterConsumerTest, SpellLockIsNormallyLearnableAndInscribedCastingIgnoresSchoolRank)
{
	prepareHero();
	const auto spellLock = spellNamed("new-horizons:spellLock");
	const SecondarySkill sorcery(SecondarySkill::decode("new-horizons:sorceryMagic"));
	ASSERT_TRUE(spellLock.toSpell()->isCommonHeroSpell());
	ASSERT_EQ(newHorizonsMagic::requiredSchoolRank(gameState()->getMagicRules(), spellLock), MasteryLevel::EXPERT);

	removeNewHorizonsSchoolRanks();
	EXPECT_FALSE(attackerSideHero->canLearnSpell(spellLock.toSpell(), true));
	attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_TRUE(attackerSideHero->canLearnSpell(spellLock.toSpell(), true));

	attackerSideHero->addSpellToSpellbook(spellLock);
	ASSERT_TRUE(attackerSideHero->spellbookContainsSpell(spellLock));
	attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(newHorizonsMagic::hasSchoolProficiency(attackerSideHero, spellLock));
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(spellLock.toSpell()));
}

TEST_F(NewHorizonsSpellRosterConsumerTest, CanLearnSpellAcceptsEachRequiredSchoolRank)
{
	prepareHero();
	const auto rules = gameState()->getMagicRules();
	struct SchoolCase
	{
		SpellID spell;
		SecondarySkill school;
		int required;
	};
	const std::array cases{
		SchoolCase{spellNamed("core:animateDead"), SecondarySkill(SecondarySkill::decode("new-horizons:shadowMagic")), MasteryLevel::BASIC},
		SchoolCase{spellNamed("core:antiMagic"), SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), MasteryLevel::ADVANCED},
		SchoolCase{spellNamed("core:armageddon"), SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic")), MasteryLevel::EXPERT},
		SchoolCase{spellNamed("new-horizons:spellLock"), SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), MasteryLevel::EXPERT},
	};

	for(const auto & test : cases)
	{
		SCOPED_TRACE(test.spell.toSpell()->getJsonKey());
		ASSERT_EQ(newHorizonsMagic::requiredSchoolRank(rules, test.spell), test.required);
		attackerSideHero->setSecSkillLevel(test.school, test.required - 1, ChangeValueMode::ABSOLUTE);
		EXPECT_FALSE(attackerSideHero->canLearnSpell(test.spell.toSpell(), true));
		attackerSideHero->setSecSkillLevel(test.school, test.required, ChangeValueMode::ABSOLUTE);
		EXPECT_TRUE(attackerSideHero->canLearnSpell(test.spell.toSpell(), true));
	}
}

TEST_F(NewHorizonsSpellRosterConsumerTest, MageGuildGrantUsesSchoolProficiencyBeforeApplyingChangeSpells)
{
	startGame(true);
	attackerSideHero->removeAllSpells();
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);

	auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(town, nullptr);
	town->addBuilding(BuildingID::MAGES_GUILD_5);
	ASSERT_EQ(town->mageGuildLevel(), 5);

	const auto animateDead = spellNamed("core:animateDead");
	const auto antiMagic = spellNamed("core:antiMagic");
	const auto armageddon = spellNamed("core:armageddon");
	town->spells.assign(GameConstants::SPELL_LEVELS, {});
	town->spells[2] = {animateDead};
	town->spells[3] = {antiMagic};
	town->spells[4] = {armageddon};

	const SecondarySkill shadow(SecondarySkill::decode("new-horizons:shadowMagic"));
	const SecondarySkill sorcery(SecondarySkill::decode("new-horizons:sorceryMagic"));
	const SecondarySkill havoc(SecondarySkill::decode("new-horizons:havocMagic"));
	for(const auto skill : {shadow, sorcery, havoc})
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);

	// This is the authoritative guild path, which emits and applies ChangeSpells;
	// none of the three rows may bypass its school-rank gate.
	gameHandler->giveSpells(town, attackerSideHero);
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(animateDead));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(antiMagic));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(armageddon));

	attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	gameHandler->giveSpells(town, attackerSideHero);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(animateDead));
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(antiMagic));
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(armageddon));
}

TEST_F(NewHorizonsSpellRosterConsumerTest, OrdinaryAcquisitionPolicyRejectsFreshSpecialtyAndRemovedSpells)
{
	startGame(true);
	attackerSideHero->removeAllSpells();
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	const auto masterChainLightning = spellNamed("new-horizons:masterChainLightning");
	const auto counterspell = spellNamed("new-horizons:counterspell");
	const auto & rules = gameState()->getMagicRules();
	const SecondarySkill havoc(SecondarySkill::decode("new-horizons:havocMagic"));
	const SecondarySkill sorcery(SecondarySkill::decode("new-horizons:sorceryMagic"));
	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);

	for(const auto spell : {masterChainLightning, counterspell})
	{
		ASSERT_FALSE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(rules, spell));
		EXPECT_FALSE(attackerSideHero->canLearnSpell(spell.toSpell(), true))
			<< spell.toSpell()->getJsonKey();
	}
	const auto summonBoat = spellNamed("core:summonBoat");
	ASSERT_TRUE(summonBoat.toSpell()->isAdventure());
	ASSERT_FALSE(attackerSideHero->spellbookContainsSpell(summonBoat));
	EXPECT_TRUE(attackerSideHero->canLearnSpell(summonBoat.toSpell()))
		<< "Adventure Spell acquisition follows the fixed Guild unlock rules, not the school-spell policy";

	auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(town, nullptr);
	town->addBuilding(BuildingID::MAGES_GUILD_5);
	town->spells.assign(GameConstants::SPELL_LEVELS, {});
	town->spells[2] = {counterspell};
	town->spells[3] = {masterChainLightning};

	// Even a fully qualified hero cannot learn ordinary offers disabled by the
	// saved eligibility profile. Explicit known spells are handled separately.
	gameHandler->giveSpells(town, attackerSideHero);
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(masterChainLightning));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(counterspell));
}

TEST_F(NewHorizonsSpellRosterConsumerTest, ScholarGrantUsesSchoolProficiencyBeforeApplyingChangeSpells)
{
	prepareHero();
	defenderSideHero->removeAllSpells();
	giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);

	const auto animateDead = spellNamed("core:animateDead");
	defenderSideHero->addSpellToSpellbook(animateDead);
	// New Horizons migrates the legacy Scholar skill to a Learning perk. Inject
	// the server bonus directly so this still exercises the authoritative,
	// legacy-compatible exchange path without changing the canonical roster.
	defenderSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::LEARN_MEETING_SPELL_LIMIT, BonusSource::OTHER, MasteryLevel::EXPERT,
		BonusSourceID()));

	const SecondarySkill shadow(SecondarySkill::decode("new-horizons:shadowMagic"));
	attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	gameHandler->useScholarSkill(defenderSideHero->id, attackerSideHero->id);
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(animateDead));

	attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	gameHandler->useScholarSkill(defenderSideHero->id, attackerSideHero->id);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(animateDead));
}

TEST_F(NewHorizonsSpellRosterConsumerTest, BothAllowedSpellEnumeratorsFilterBeforeLevelLookup)
{
	prepareHero();
	const auto arrow = spellNamed("core:magicArrow");
	ASSERT_TRUE(gameState()->getMap().allowedSpells.count(arrow));
	ExcludingWorld excluded(*gameState(), "core:magicArrow");
	EXPECT_FALSE(excluded.isAllowed(arrow));
	std::vector<SpellID> worldResult;
	excluded.getAllowedSpells(worldResult, std::nullopt);
	EXPECT_FALSE(vstd::contains(worldResult, arrow));
	std::vector<SpellID> mapResult;
	static_cast<MapInfoCallback &>(excluded).getAllowedSpells(mapResult, 1);
	EXPECT_FALSE(vstd::contains(mapResult, arrow));
	EXPECT_TRUE(excluded.getSpellSchools(arrow).empty());
	EXPECT_EQ(excluded.getSpellLevel(arrow), 0);
}

TEST_F(NewHorizonsSpellRosterConsumerTest, InvalidIdsFailBeforeSchoolLevelCostAndGrantDereference)
{
	prepareHero();
	for(const auto id : {SpellID(SpellID::NONE), SpellID(-2), SpellID(std::numeric_limits<int32_t>::max())})
	{
		EXPECT_TRUE(newHorizonsMagic::spellSchools(gameState()->getMagicRules(), id).empty());
		EXPECT_EQ(newHorizonsMagic::spellLevel(gameState()->getMagicRules(), id), 0);
		EXPECT_THROW(newHorizonsMagic::spellCost(gameState()->getMagicRules(), id, 0), std::runtime_error);
		EXPECT_TRUE(attackerSideHero->getSourcesForSpell(id).empty());
	}
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(nullptr));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(nullptr, true));
}

TEST_F(NewHorizonsSpellRosterConsumerTest, CommonCastGateRejectsExcludedBattleDespiteAdmittingWorld)
{
	prepareHero();
	const auto arrow = spellNamed("core:magicArrow");
	attackerSideHero->addSpellToSpellbook(arrow);
	setTestSpellPointTotal(attackerSideHero, 100);
	startBattle();
	beginCombat();
	spells::BattleCast original(battle(), attackerSideHero, spells::Mode::HERO, arrow.toSpell());
	spells::detail::ProblemImpl originalProblem;
	ASSERT_TRUE(arrow.toSpell()->battleMechanics(&original)->canBeCast(originalProblem));
	auto excluded = battle()->getMagicRules();
	excluded["spells"].Struct().erase("core:magicArrow");
	CMemorySerializer wire;
	wire.oser & *battle();
	wire.iser.cb = gameState().get();
	{
		ScopedArmyBattleLinks restore(*attackerSideHero, *defenderSideHero);
		ExcludingBattle incoming(gameState().get(), excluded);
		wire.iser & static_cast<BattleInfo &>(incoming);
		incoming.localInit();
		ASSERT_TRUE(newHorizonsMagic::spellAllowedByWorldRoster(*gameState(), arrow));
		ASSERT_FALSE(newHorizonsMagic::spellAllowedByBattleRoster(incoming, arrow));
		for(const auto mode : {spells::Mode::HERO, spells::Mode::PASSIVE})
		{
			spells::BattleCast cast(&incoming, attackerSideHero, mode, arrow.toSpell());
			spells::detail::ProblemImpl problem;
			EXPECT_FALSE(arrow.toSpell()->battleMechanics(&cast)->canBeCast(problem));
		}
	}
	EXPECT_EQ(attackerSideHero->battle, battle());
	EXPECT_EQ(defenderSideHero->battle, battle());
	// Delegated exclusion is synthetic and invalid as a full saved core roster;
	// actual managed-identity old-save and authoritative rejection gates remain.
}

TEST_F(NewHorizonsSpellRosterConsumerTest, SpellRewardExplainsTheRecipientsExactSchoolRequirement)
{
	prepareHero();
	const auto chainLightning = spellNamed("core:chainLightning");
	const SecondarySkill havoc(SecondarySkill::decode("new-horizons:havocMagic"));
	const auto & rules = gameState()->getMagicRules();

	ASSERT_TRUE(newHorizonsMagic::spellAllowedByHeroRoster(rules, chainLightning));
	ASSERT_TRUE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(rules, chainLightning));
	ASSERT_EQ(newHorizonsMagic::requiredSchoolRank(rules, chainLightning), MasteryLevel::ADVANCED);
	ASSERT_EQ(newHorizonsMagic::spellSchoolSkills(rules, chainLightning), (std::vector<SecondarySkill>{havoc}));
	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(attackerSideHero->getSpellLearningStatus(chainLightning.toSpell()),
		CGHeroInstance::SpellLearningStatus::INSUFFICIENT_SCHOOL);

	Rewardable::Reward reward;
	reward.spells.push_back(chainLightning);
	std::vector<Component> components;
	reward.loadComponents(components, attackerSideHero);
	ASSERT_EQ(components.size(), 1u);
	const auto & unavailable = components.front();
	EXPECT_EQ(unavailable.type, ComponentType::SPELL);
	EXPECT_EQ(unavailable.subType.as<SpellID>(), chainLightning);
	ASSERT_EQ(unavailable.value, std::optional<int32_t>(-1));
	ASSERT_TRUE(unavailable.helpReason.has_value());
	const std::string reason = unavailable.helpReason->toString(LIBRARY->staticTexts());
	const std::string requiredRank = LIBRARY->generaltexth->translate("core.skilllev", MasteryLevel::ADVANCED - 1);
	const std::string schoolName = LIBRARY->skillh->getById(havoc)->getNameTranslated();
	EXPECT_NE(reason.find(requiredRank), std::string::npos);
	EXPECT_NE(reason.find(schoolName), std::string::npos);
	EXPECT_EQ(reason.find('%'), std::string::npos) << "localized reason must not retain unresolved tokens";

	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(attackerSideHero->getSpellLearningStatus(chainLightning.toSpell()),
		CGHeroInstance::SpellLearningStatus::LEARNABLE);
	components.clear();
	reward.loadComponents(components, attackerSideHero);
	ASSERT_EQ(components.size(), 1u);
	EXPECT_EQ(components.front().value, std::optional<int32_t>(0));
	EXPECT_FALSE(components.front().helpReason.has_value());

	attackerSideHero->addSpellToSpellbook(chainLightning);
	EXPECT_EQ(attackerSideHero->getSpellLearningStatus(chainLightning.toSpell()),
		CGHeroInstance::SpellLearningStatus::UNAVAILABLE);
	components.clear();
	reward.loadComponents(components, attackerSideHero);
	ASSERT_EQ(components.size(), 1u);
	EXPECT_FALSE(components.front().helpReason.has_value());
}

TEST_F(NewHorizonsSpellRosterConsumerTest, OtherSpellRewardRestrictionsAndScrollsDoNotClaimSchoolIsTheBlocker)
{
	prepareHero(false);
	const auto chainLightning = spellNamed("core:chainLightning");
	const SecondarySkill havoc(SecondarySkill::decode("new-horizons:havocMagic"));
	ASSERT_TRUE(newHorizonsMagic::spellAllowedByHeroRoster(gameState()->getMagicRules(), chainLightning));
	ASSERT_TRUE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(gameState()->getMagicRules(), chainLightning));
	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);

	const auto expectNoSchoolReason = [&](SpellID spell)
	{
		Rewardable::Reward reward;
		reward.spells.push_back(spell);
		std::vector<Component> components;
		reward.loadComponents(components, attackerSideHero);
		EXPECT_EQ(components.size(), 1u) << spell.toSpell()->getJsonKey();
		if(components.size() == 1)
		{
			EXPECT_FALSE(components.front().helpReason.has_value()) << spell.toSpell()->getJsonKey();
		}
	};

	EXPECT_EQ(attackerSideHero->getSpellLearningStatus(chainLightning.toSpell()),
		CGHeroInstance::SpellLearningStatus::UNAVAILABLE);
	expectNoSchoolReason(chainLightning); // no spellbook is a separate blocker

	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	const auto special = spellNamed("core:landMineTrigger");
	const auto creatureAbility = spellNamed("core:summonDemons");
	ASSERT_TRUE(special.toSpell()->isSpecial());
	ASSERT_TRUE(creatureAbility.toSpell()->isCreatureAbility());
	for(const auto skill : newHorizonsMagic::schoolSkills(gameState()->getMagicRules()))
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(attackerSideHero->getSpellLearningStatus(special.toSpell()), CGHeroInstance::SpellLearningStatus::UNAVAILABLE);
	expectNoSchoolReason(special);
	EXPECT_EQ(attackerSideHero->getSpellLearningStatus(creatureAbility.toSpell()), CGHeroInstance::SpellLearningStatus::UNAVAILABLE);
	expectNoSchoolReason(creatureAbility);

	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	{
		ScopedHeroSpellExclusion excluded(*attackerSideHero, chainLightning);
		EXPECT_EQ(attackerSideHero->getSpellLearningStatus(chainLightning.toSpell()), CGHeroInstance::SpellLearningStatus::UNAVAILABLE);
		expectNoSchoolReason(chainLightning);
	}

	attackerSideHero->addSpellToSpellbook(chainLightning);
	EXPECT_EQ(attackerSideHero->getSpellLearningStatus(chainLightning.toSpell()), CGHeroInstance::SpellLearningStatus::UNAVAILABLE);
	expectNoSchoolReason(chainLightning); // already-known is not a School-rank failure
	attackerSideHero->removeSpellFromSpellbook(chainLightning);

	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(gameState()->getMap().allowedSpells.count(chainLightning));
	gameState()->getMap().allowedSpells.erase(chainLightning);
	EXPECT_EQ(attackerSideHero->getSpellLearningStatus(chainLightning.toSpell()), CGHeroInstance::SpellLearningStatus::UNAVAILABLE);
	attackerSideHero->setSecSkillLevel(havoc, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(attackerSideHero->getSpellLearningStatus(chainLightning.toSpell()), CGHeroInstance::SpellLearningStatus::UNAVAILABLE)
		<< "an explicit map ban remains a blocker even when the hero also lacks School rank";
	gameState()->getMap().allowedSpells.insert(chainLightning);
	EXPECT_EQ(attackerSideHero->getSpellLearningStatus(chainLightning.toSpell()), CGHeroInstance::SpellLearningStatus::INSUFFICIENT_SCHOOL);

	Rewardable::Reward scrollReward;
	scrollReward.grantedScrolls.push_back(chainLightning);
	std::vector<Component> scrollComponents;
	scrollReward.loadComponents(scrollComponents, attackerSideHero);
	ASSERT_EQ(scrollComponents.size(), 1u);
	EXPECT_EQ(scrollComponents.front().type, ComponentType::SPELL);
	EXPECT_FALSE(scrollComponents.front().helpReason.has_value());

	LegacyMagicWorld legacy(*gameState());
	ScopedHeroCallback legacyContext(*attackerSideHero, &legacy);
	attackerSideHero->setSecSkillLevel(SecondarySkill::WISDOM, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	const auto legacySpell = spellNamed("core:armageddon");
	EXPECT_EQ(attackerSideHero->getSpellLearningStatus(legacySpell.toSpell()), CGHeroInstance::SpellLearningStatus::LEARNABLE);
	Rewardable::Reward legacyReward;
	legacyReward.spells.push_back(legacySpell);
	std::vector<Component> legacyComponents;
	legacyReward.loadComponents(legacyComponents, attackerSideHero);
	ASSERT_EQ(legacyComponents.size(), 1u);
	EXPECT_EQ(legacyComponents.front().value, std::optional<int32_t>(0));
	EXPECT_FALSE(legacyComponents.front().helpReason.has_value());
}

TEST(ComponentHelpReasonSerialization, CurrentRoundTripAndOlderReadKeepComponentIdentity)
{
	const auto spell = spellNamed("core:antiMagic");
	Component original(ComponentType::SPELL, spell, -1);
	original.helpReason = MetaString::createFromRawString("Requires Advanced Sorcery Magic.");

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.oser & original;
	CMemorySerializer currentReader(current.extractBuffer());
	currentReader.iser.version = ESerializationVersion::CURRENT;
	Component restored;
	currentReader.iser & restored;
	EXPECT_EQ(restored.type, original.type);
	EXPECT_EQ(restored.subType.as<SpellID>(), spell);
	EXPECT_EQ(restored.value, std::optional<int32_t>(-1));
	ASSERT_TRUE(restored.helpReason.has_value());
	EXPECT_EQ(*restored.helpReason, *original.helpReason);

	Component legacySource(ComponentType::SPELL, spell, -1);
	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_PRIMARY_EXPERIENCE_REWARD;
	legacy.oser & legacySource;
	const auto legacyWithoutReason = legacy.extractBuffer();

	CMemorySerializer legacyWriterWithReason;
	legacyWriterWithReason.oser.version = ESerializationVersion::NEW_HORIZONS_PRIMARY_EXPERIENCE_REWARD;
	legacyWriterWithReason.oser & original;
	EXPECT_EQ(*original.helpReason, MetaString::createFromRawString("Requires Advanced Sorcery Magic."));
	const auto legacyWithReason = legacyWriterWithReason.extractBuffer();
	EXPECT_EQ(legacyWithReason, legacyWithoutReason) << "older writers omit the unsupported optional field";

	CMemorySerializer legacyReader(legacyWithReason);
	legacyReader.iser.version = ESerializationVersion::NEW_HORIZONS_PRIMARY_EXPERIENCE_REWARD;
	Component legacyRestored;
	legacyRestored.helpReason = MetaString::createFromRawString("stale target value");
	legacyReader.iser & legacyRestored;
	EXPECT_EQ(legacyRestored.type, original.type);
	EXPECT_EQ(legacyRestored.subType.as<SpellID>(), spell);
	EXPECT_EQ(legacyRestored.value, std::optional<int32_t>(-1));
	EXPECT_FALSE(legacyRestored.helpReason.has_value());
}
