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
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../server/CGameHandler.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

namespace
{
constexpr auto WISDOM_SKILL = "new-horizons:wisdom";
constexpr auto PREPARED_CASTER = "new-horizons:wisdom.preparedCaster";

class NewHorizonsPreparedCasterTest : public HeroCommandFixture
{
protected:
	bool capturePreparedCasterAsPlanned = false;

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
		newHorizonsMagic::validateRules(magicRules);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(capturePreparedCasterAsPlanned)
		{
			for(auto & perk : perkRules["skills"][WISDOM_SKILL]["perks"].Vector())
			{
				if(perk["id"].String() == PREPARED_CASTER)
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

	void prepareAttackerSpellbook(CGHeroInstance * hero)
	{
		giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		hero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		setTestSpellPointTotal(hero, 1000);
	}

	CStack * prepareBattleWithPreparedCaster(int wisdomRank = MasteryLevel::BASIC)
	{
		startGame();
		grantPreparedCaster(attackerSideHero, wisdomRank);
		prepareAttackerSpellbook(attackerSideHero);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		startBattle();
		auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
		beginCombat();
		return target;
	}

	bool issueMagicArrow(CStack * target, int overcharge = 0)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::MAGIC_ARROW;
		action.spellOvercharge = overcharge;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
};
}

TEST_F(NewHorizonsPreparedCasterTest, AcceptedFirstSpellGetsDiscountAndTheGatePersistsAcrossRounds)
{
	auto * target = prepareBattleWithPreparedCaster(MasteryLevel::EXPERT);
	ASSERT_NE(target, nullptr);
	ASSERT_EQ(attackerSideHero->getListedSpellCost(SpellID(SpellID::MAGIC_ARROW).toSpell()), 4);
	const auto wisdomCost = newHorizonsMagic::wisdomAdjustedCost(
		attackerSideHero->getListedSpellCost(SpellID(SpellID::MAGIC_ARROW).toSpell()), 1, MasteryLevel::EXPERT);
	ASSERT_EQ(wisdomCost, 3);
	EXPECT_EQ(battle()->battleGetSpellCost(SpellID(SpellID::MAGIC_ARROW).toSpell(), attackerSideHero), 1);
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
	EXPECT_EQ(battle()->battleGetSpellCost(magicArrow, attackerSideHero), 1)
		<< "Prepared Caster and allied battlefield reductions cannot make an ordinary spell free";
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

TEST_F(NewHorizonsPreparedCasterTest, CompletedCastMarkerHasAppendOnlyBinaryCompatibility)
{
	SideInBattle original(nullptr);
	original.heroSpellCastCompleted = true;

	CMemorySerializer current;
	current.oser.version = current.iser.version = ESerializationVersion::CURRENT;
	current.oser & original;
	SideInBattle restored(nullptr);
	current.iser & restored;
	EXPECT_TRUE(restored.heroSpellCastCompleted);

	CMemorySerializer oldDownsave;
	oldDownsave.oser.version = ESerializationVersion::NEW_HORIZONS_CRUSADE_MAGIC_REDUCTION;
	EXPECT_THROW(oldDownsave.oser & original, std::runtime_error);
	EXPECT_TRUE(oldDownsave.extractBuffer().empty());

	CMemorySerializer oldSave;
	oldSave.oser.version = oldSave.iser.version = ESerializationVersion::NEW_HORIZONS_CRUSADE_MAGIC_REDUCTION;
	SideInBattle oldState(nullptr);
	oldSave.oser & oldState;
	SideInBattle loadedFromOld(nullptr);
	loadedFromOld.heroSpellCastCompleted = true;
	oldSave.iser & loadedFromOld;
	EXPECT_FALSE(loadedFromOld.heroSpellCastCompleted);
}
