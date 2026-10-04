/*
 * NewHorizonsFriendlyFireTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"

#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/CStack.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsFriendlyFire.h"
#include "../../../lib/spells/Problem.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../SpellPointTestUtils.h"

#include <algorithm>
#include <string_view>
#include <vector>

namespace
{
constexpr std::string_view armageddonKey = "core:armageddon";
constexpr std::string_view handOfFateKey = "new-horizons:handOfFate";

SpellID spellNamed(std::string_view name)
{
	return SpellID(SpellID::decode(std::string(name)));
}

class NewHorizonsFriendlyFireTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	const CSpell * prepare(std::string_view spellKey)
	{
		startGame();
		const auto * spell = spellNamed(spellKey).toSpell();
		if(!spell)
			return nullptr;

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(spell->getId());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 30, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		removeDeployedUnits();
		return spell;
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	void beginScenario()
	{
		beginCombat();
		for(int index = 0; index < 100; ++index)
		{
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			if(battle()->battleGetOwner(active) == attackerSideHero->getOwner())
				return;
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->battleGetOwner(active), BattleAction::makeDefend(active)));
		}
		FAIL() << "No legal attacker-side action became available";
	}

	void addSpellImmunity(CStack * stack, SpellID spell)
	{
		stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(spell)));
	}

	std::vector<uint32_t> previewIds(const CSpell * spell, const spells::Target & aim) const
	{
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
		const auto mechanics = spell->battleMechanics(&cast);
		return ids(newHorizonsFriendlyFire::potentialFriendlyDamageTargets(*mechanics, aim));
	}

	static std::vector<uint32_t> ids(const std::vector<const CStack *> & stacks)
	{
		std::vector<uint32_t> result;
		result.reserve(stacks.size());
		for(const auto * stack : stacks)
			if(stack)
				result.push_back(stack->unitId());
		return result;
	}

	bool castGlobally(const CSpell * spell)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell->getId();
		action.stackNumber = -1;
		action.aimToHex(BattleHex::INVALID);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->getOwner(), action);
	}
};
}

TEST_F(NewHorizonsFriendlyFireTest, ArmageddonPreviewMatchesEffectiveAllegianceAndDoesNotMutateOrDrawRandomness)
{
	const auto * spell = prepare(armageddonKey);
	ASSERT_NE(spell, nullptr);
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 5), 100);
	auto * immuneFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 100);
	auto * hypnotized = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 100);
	auto * hostile = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(13, 5), 100);
	ASSERT_NE(friendly, nullptr);
	ASSERT_NE(immuneFriendly, nullptr);
	ASSERT_NE(hypnotized, nullptr);
	ASSERT_NE(hostile, nullptr);
	addSpellImmunity(immuneFriendly, spell->getId());
	hypnotized->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(battle()->battleGetOwner(hypnotized), attackerSideHero->getOwner());

	const spells::Target aim{spells::Destination(BattleHex::INVALID)};
	const auto friendlyHealthBefore = friendly->getAvailableHealth();
	const auto immuneHealthBefore = immuneFriendly->getAvailableHealth();
	const auto hypnotizedHealthBefore = hypnotized->getAvailableHealth();
	const auto hostileHealthBefore = hostile->getAvailableHealth();
	const auto manaBefore = attackerSideHero->getManaAvailable();

	CRandomGenerator expectedRandom(BattleTestFixture::seed);
	const auto expectedNextRoll = expectedRandom.nextInt(0, 100000);
	gameHandler->randomizer->setSeed(BattleTestFixture::seed);
	const auto preview = previewIds(spell, aim);
	EXPECT_EQ(preview, (std::vector<uint32_t>{friendly->unitId(), hypnotized->unitId()}));
	EXPECT_EQ(previewIds(spell, aim), preview);
	EXPECT_EQ(gameHandler->randomizer->getDefault().nextInt(0, 100000), expectedNextRoll);
	EXPECT_EQ(friendly->getAvailableHealth(), friendlyHealthBefore);
	EXPECT_EQ(immuneFriendly->getAvailableHealth(), immuneHealthBefore);
	EXPECT_EQ(hypnotized->getAvailableHealth(), hypnotizedHealthBefore);
	EXPECT_EQ(hostile->getAvailableHealth(), hostileHealthBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);

	beginScenario();
	ASSERT_TRUE(castGlobally(spell));
	EXPECT_LT(friendly->getAvailableHealth(), friendlyHealthBefore);
	EXPECT_EQ(immuneFriendly->getAvailableHealth(), immuneHealthBefore);
	EXPECT_LT(hypnotized->getAvailableHealth(), hypnotizedHealthBefore);
	EXPECT_LT(hostile->getAvailableHealth(), hostileHealthBefore);
}

TEST_F(NewHorizonsFriendlyFireTest, ImmuneFriendliesDoNotProduceAConfirmationRecipient)
{
	const auto * spell = prepare(armageddonKey);
	ASSERT_NE(spell, nullptr);
	auto * immuneFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 5), 100);
	auto * hostile = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 100);
	ASSERT_NE(immuneFriendly, nullptr);
	ASSERT_NE(hostile, nullptr);
	addSpellImmunity(immuneFriendly, spell->getId());

	EXPECT_TRUE(previewIds(spell, spells::Target{spells::Destination(BattleHex::INVALID)}).empty());
}

TEST_F(NewHorizonsFriendlyFireTest, HandOfFatePreviewIncludesPotentialAllyCollateralWithoutChoosingOrDrawing)
{
	const auto * spell = prepare(handOfFateKey);
	ASSERT_NE(spell, nullptr);
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 5), 100);
	auto * immuneFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 100);
	auto * primary = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 100);
	auto * enemyCollateral = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 2), 100);
	ASSERT_NE(friendly, nullptr);
	ASSERT_NE(immuneFriendly, nullptr);
	ASSERT_NE(primary, nullptr);
	ASSERT_NE(enemyCollateral, nullptr);
	addSpellImmunity(immuneFriendly, spell->getId());
	const spells::Target aim{spells::Destination(primary)};

	CRandomGenerator expectedRandom(BattleTestFixture::seed);
	const auto expectedNextRoll = expectedRandom.nextInt(0, 100000);
	gameHandler->randomizer->setSeed(BattleTestFixture::seed);
	const auto preview = previewIds(spell, aim);
	EXPECT_EQ(preview, (std::vector<uint32_t>{friendly->unitId()}));
	EXPECT_EQ(previewIds(spell, aim), preview);
	EXPECT_EQ(gameHandler->randomizer->getDefault().nextInt(0, 100000), expectedNextRoll);
	EXPECT_EQ(std::find(preview.begin(), preview.end(), primary->unitId()), preview.end());
	EXPECT_EQ(std::find(preview.begin(), preview.end(), immuneFriendly->unitId()), preview.end());
	EXPECT_EQ(std::find(preview.begin(), preview.end(), enemyCollateral->unitId()), preview.end());
}

TEST(NewHorizonsFriendlyFireConfirmationGateTest, RejectsStaleSnapshotsAndAllowsAtMostOneConfirm)
{
	newHorizonsFriendlyFire::ConfirmationSnapshot snapshot;
	snapshot.battleID = BattleID(4);
	snapshot.spellID = SpellID(SpellID::ARMAGEDDON);
	snapshot.heroID = ObjectInstanceID(12);
	snapshot.casterSide = BattleSide::ATTACKER;
	snapshot.round = 2;
	snapshot.castingSession = 7;
	snapshot.friendlyUnitIDs = {3, 9};

	newHorizonsFriendlyFire::ConfirmationGate accepted(snapshot);
	EXPECT_TRUE(accepted.isPending());
	EXPECT_TRUE(accepted.confirm(snapshot));
	EXPECT_FALSE(accepted.confirm(snapshot));
	EXPECT_FALSE(accepted.isPending());

	newHorizonsFriendlyFire::ConfirmationGate stale(snapshot);
	auto changedRound = snapshot;
	++changedRound.round;
	EXPECT_FALSE(stale.confirm(changedRound));
	EXPECT_TRUE(stale.isPending());
	auto changedRecipients = snapshot;
	changedRecipients.friendlyUnitIDs.pop_back();
	EXPECT_FALSE(stale.confirm(changedRecipients));
	EXPECT_TRUE(stale.cancel());
	EXPECT_FALSE(stale.confirm(snapshot));
	EXPECT_FALSE(stale.cancel());
}
