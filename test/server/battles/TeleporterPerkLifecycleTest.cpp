/*
 * TeleporterPerkLifecycleTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of the license available in license.txt file, in the main folder
 *
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/constants/StringConstants.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/SetStackEffect.h"

namespace
{
constexpr auto teleporterSkill = "new-horizons:sorceryMagic";
constexpr auto teleporterPerk = "new-horizons:sorceryMagic.teleporter";

class TeleporterPerkLifecycleTest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void addTeleporterBonus(const battle::Unit * stack)
	{
		Bonus bonus(BonusDuration::STACK_ACTIVATION, BonusType::STACKS_SPEED,
			BonusSource::SPELL_EFFECT, 2, BonusSourceID(SpellID(SpellID::TELEPORT)));
		bonus.stacking = "new-horizons:teleporter";

		SetStackEffect effect;
		effect.battleID = BattleID(0);
		effect.toAdd.emplace_back(stack->unitId(), std::vector<Bonus>{bonus});
		gameHandler->sendAndApply(effect);
	}

	SecondarySkill teleporterSkillId() const
	{
		const auto decoded = SecondarySkill::decode(teleporterSkill);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void prepareTeleport(bool selectPerk)
	{
		startGame();
		const auto skill = teleporterSkillId();
		attackerSideHero->setSecSkillLevel(skill, 2, ChangeValueMode::ABSOLUTE);
		if(selectPerk)
			attackerSideHero->applyPerkSelection({teleporterSkill, teleporterPerk});
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::TELEPORT);
		setTestSpellPointTotal(attackerSideHero, 100);

		startBattle();
		teleported = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(rightHex), 10);
		beginCombat();
		ASSERT_EQ(attackerSideHero->hasActivePerk(teleporterSkill, teleporterPerk), selectPerk);
	}

	BattleHex freeTeleportDestination() const
	{
		const auto accessibility = battle()->getAccessibility(teleported);
		for(si16 offset = 1; offset < GameConstants::BFIELD_SIZE; ++offset)
		{
			const BattleHex candidate(static_cast<si16>(teleported->getPosition().toInt() + offset));
			if(candidate.isAvailable() && accessibility.accessible(candidate, teleported))
				return candidate;
		}
		return BattleHex::INVALID;
	}

	bool castTeleport(const BattleHex & destination)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::TELEPORT;
		action.aimToUnit(teleported);
		action.aimToHex(destination);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	CStack * teleported = nullptr;
};
}

TEST_F(TeleporterPerkLifecycleTest, StackActivationSurvivesNonUnitAndRejectedActionsUntilAcceptedUnitAction)
{
	prepareCommands();
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	addTeleporterBonus(active);
	ASSERT_FALSE(active->getAllBonuses(Bonus::UntilActivationEnds)->empty());

	// Hero actions share the active stack but are not the stack's creature activation.
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_FALSE(active->getAllBonuses(Bonus::UntilActivationEnds)->empty());

	// The server must not consume the bonus for an action it rejects.
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeDefend(active)));
	EXPECT_FALSE(active->getAllBonuses(Bonus::UntilActivationEnds)->empty());

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeDefend(active)));
	EXPECT_TRUE(active->getAllBonuses(Bonus::UntilActivationEnds)->empty());
}

TEST_F(TeleporterPerkLifecycleTest, AcceptedAutomaticUnitActionEndsActivationBonus)
{
	startGame();
	giveArtifact(attackerSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);
	startBattle();
	const auto machines = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::ATTACKER && stack->isBallista();
	});
	ASSERT_EQ(machines.size(), 1u);
	const auto * ballista = machines.front();
	const auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	ASSERT_TRUE(battle()->battleCanShoot(ballista, target->getPosition()));
	addTeleporterBonus(ballista);
	ASSERT_FALSE(ballista->getAllBonuses(Bonus::UntilActivationEnds)->empty());

	beginCombat();
	const auto ballistaActed = [&]()
	{
		return std::any_of(server.startedActions.begin(), server.startedActions.end(), [&](const auto & action)
		{
			return action.battleID == BattleID(0) && action.ba.isUnitAction()
				&& action.ba.stackNumber == ballista->unitId();
		});
	};
	const auto maximumTurns = battle()->stacks.size() * 2;
	for(size_t turn = 0; turn < maximumTurns && !ballistaActed(); ++turn)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_NE(active->unitId(), ballista->unitId());
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
	}

	ASSERT_TRUE(ballistaActed());
	EXPECT_TRUE(ballista->getAllBonuses(Bonus::UntilActivationEnds)->empty());
}

TEST_F(TeleporterPerkLifecycleTest, TeleportAddsSpeedOnlyForAnActiveSavedTeleporterPerk)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the curated New Horizons module to register sorceryMagic";
	prepareTeleport(true);
	const auto destination = freeTeleportDestination();
	ASSERT_TRUE(destination.isValid());
	const auto originalSpeed = teleported->getMovementRange();
	ASSERT_TRUE(castTeleport(destination));
	EXPECT_EQ(teleported->getPosition(), destination);
	const auto activeBonus = teleported->getAllBonuses(Bonus::UntilActivationEnds);
	ASSERT_EQ(activeBonus->size(), 1u);
	EXPECT_EQ(activeBonus->front()->type, BonusType::STACKS_SPEED);
	EXPECT_EQ(activeBonus->front()->val, 2);
	EXPECT_EQ(teleported->getMovementRange(), originalSpeed + 2);
}

TEST_F(TeleporterPerkLifecycleTest, TeleportDoesNotAddSpeedWithoutTheSavedTeleporterSelection)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the curated New Horizons module to register sorceryMagic";
	prepareTeleport(false);
	const auto destination = freeTeleportDestination();
	ASSERT_TRUE(destination.isValid());
	ASSERT_TRUE(castTeleport(destination));
	EXPECT_EQ(teleported->getPosition(), destination);
	EXPECT_TRUE(teleported->getAllBonuses(Bonus::UntilActivationEnds)->empty());
}
