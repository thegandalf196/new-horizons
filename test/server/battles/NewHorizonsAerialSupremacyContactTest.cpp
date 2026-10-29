/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsAerialSupremacyFixture.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include <set>
namespace
{
// Frozen ordinary algorithm, retained as the independent extraction oracle.
AttackableTiles legacyMeleeTiles(const battle::Unit * attacker, const battle::Unit * defender,
	BattleHex destinationTile, BattleHex attackerPos, BattleHex defenderPos)
{
	//does not return hex attacked directly
	AttackableTiles at;


	BattleHex attackOriginHex = (attackerPos.toInt() != BattleHex::INVALID) ? attackerPos : attacker->getPosition(); //real or hypothetical (cursor) position

	defenderPos = (defenderPos.toInt() != BattleHex::INVALID) ? defenderPos : defender->getPosition(); //real or hypothetical (cursor) position

	if(attacker->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT))
		at.hostileCreaturePositions.insert(attacker->getSurroundingHexes(attackerPos));

	// If attacker is double-wide and its head is not adjacent to enemy we need to turn around
	if(attacker->doubleWide() && !vstd::contains(defender->getSurroundingHexes(defenderPos), attackOriginHex))
		attackOriginHex = attacker->occupiedHex(attackOriginHex);

	auto attackDirection = BattleHex::mutualPosition(attackOriginHex, defenderPos);

	// If defender is double-wide, attacker always prefers targeting its 'tail', if it is reachable
	if(defender->doubleWide() && BattleHex::mutualPosition(attackOriginHex, defender->occupiedHex(defenderPos)) != BattleHex::NONE)
		attackDirection = BattleHex::mutualPosition(attackOriginHex, defender->occupiedHex(defenderPos));

	if(attackDirection == BattleHex::NONE)
		throw std::runtime_error("!!!");

	const auto & processTargets = [&](const std::vector<int> & additionalTargets) -> BattleHexArray
	{
		BattleHexArray output;

		for (int targetPath : additionalTargets)
		{
			BattleHex target = attackOriginHex;
			std::vector<BattleHex::EDir> path;

			for (int targetPathLeft = targetPath; targetPathLeft != 0; targetPathLeft /= 10)
				path.push_back(static_cast<BattleHex::EDir>((attackDirection + targetPathLeft % 10 - 1) % 6));

			try
			{
				if(attacker->doubleWide() && attacker->coversPos(target.cloneInDirection(path.front())))
					target.moveInDirection(attackDirection);

				for(BattleHex::EDir nextDirection : path)
					target.moveInDirection(nextDirection);
			}
			catch(const std::out_of_range &)
			{
				// Hex out of range, for example outside of battlefield. This is valid situation, so skip this hex
				continue;
			}

			if (target.isValid() && !attacker->coversPos(target))
				output.insert(target);
		}
		return output;
	};

	const auto multihexUnit = attacker->getBonusesOfType(BonusType::MULTIHEX_UNIT_ATTACK);
	const auto multihexEnemy = attacker->getBonusesOfType(BonusType::MULTIHEX_ENEMY_ATTACK);
	const auto multihexAnimation = attacker->getBonusesOfType(BonusType::MULTIHEX_ANIMATION);

	for (const auto & bonus : *multihexUnit)
		at.friendlyCreaturePositions.insert(processTargets(bonus->parameters->toVector()));

	for (const auto & bonus : *multihexEnemy)
		at.hostileCreaturePositions.insert(processTargets(bonus->parameters->toVector()));

	for (const auto & bonus : *multihexAnimation)
		at.overrideAnimationPositions.insert(processTargets(bonus->parameters->toVector()));

	if(attacker->hasBonusOfType(BonusType::THREE_HEADED_ATTACK))
		at.hostileCreaturePositions.insert(processTargets({2,6}));

	if(attacker->hasBonusOfType(BonusType::WIDE_BREATH))
		at.friendlyCreaturePositions.insert(processTargets({ 11, 111, 2, 12, 6, 16 }));

	if(attacker->hasBonusOfType(BonusType::TWO_HEX_ATTACK_BREATH))
		at.friendlyCreaturePositions.insert(processTargets({ 11 }));

	if (attacker->hasBonusOfType(BonusType::PRISM_HEX_ATTACK_BREATH))
		at.friendlyCreaturePositions.insert(processTargets({ 11, 12, 16 }));

	return at;
}

std::set<uint32_t> ids(const battle::Units & units)
{
	std::set<uint32_t> result;
	for(const auto * unit : units) result.insert(unit->unitId());
	return result;
}
std::shared_ptr<Bonus> grant(CStack * unit, BonusType type)
{
	auto bonus = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, 1, BonusSourceID());
	unit->addNewBonus(bonus);
	return bonus;
}
}

TEST_F(NewHorizonsAerialSupremacyBattleTest, DefaultContactWorkerMatchesFrozenGenericSingleAndDoubleWideGeometry)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->moveUnit(shooter->unitId(), BattleHex(9, 5));
	for(auto * actor : {shooter, griffin})
	{
		const auto * defender = actor == shooter ? griffin : shooter;
		for(const auto type : {BonusType::ATTACKS_ALL_ADJACENT, BonusType::THREE_HEADED_ATTACK,
			BonusType::WIDE_BREATH, BonusType::TWO_HEX_ATTACK_BREATH, BonusType::PRISM_HEX_ATTACK_BREATH})
		{
			const auto bonus = grant(actor, type);
			const auto expected = legacyMeleeTiles(actor, defender, defender->getPosition(), actor->getPosition(), defender->getPosition());
			const auto actual = battle()->getPotentiallyAttackableHexes(actor, defender, defender->getPosition(), actor->getPosition(), defender->getPosition());
			EXPECT_EQ(actual.hostileCreaturePositions, expected.hostileCreaturePositions);
			EXPECT_EQ(actual.friendlyCreaturePositions, expected.friendlyCreaturePositions);
			EXPECT_EQ(actual.overrideAnimationPositions, expected.overrideAnimationPositions);
			actor->removeBonus(bonus);
		}
	}
}
TEST_F(NewHorizonsAerialSupremacyBattleTest, AerialAllAdjacentUsesLandingFootprintAndCurrentAllegianceWithoutSelfHit)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(6, 4), 10);
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(6, 6), 10);
	grant(griffin, BonusType::ATTACKS_ALL_ADJACENT);
	const auto excursion = newHorizonsAerialSupremacy::prepare(*battle(), griffin, shooter);
	ASSERT_TRUE(excursion);
	const auto origin = griffin->getPosition();
	const auto surrounding = griffin->getSurroundingHexes(excursion->landing);
	std::set<uint32_t> expected;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit->alive() && !unit->isInvincible() && unit->unitId() != griffin->unitId()
			&& battle()->battleGetOwner(unit) != battle()->battleGetOwner(griffin))
			for(const auto & hex : unit->getHexes())
				if(surrounding.contains(hex)) expected.insert(unit->unitId());
	const auto actual = battle()->getAerialAttackedBattleUnits(*excursion);
	EXPECT_EQ(ids(actual.first), expected);
	EXPECT_FALSE(ids(actual.first).contains(griffin->unitId()));
	EXPECT_EQ(griffin->getPosition(), origin);
	EXPECT_EQ(griffin->aerialRetaliationRound, -1);
}
TEST_F(NewHorizonsAerialSupremacyBattleTest, AerialBreathAndWideBreathRetainFriendlyFireBehindOriginalShooter)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto excursion = newHorizonsAerialSupremacy::prepare(*battle(), griffin, shooter);
	ASSERT_TRUE(excursion);
	auto contact = excursion->landing;
	if(BattleHex::mutualPosition(contact, excursion->target) == BattleHex::NONE)
		contact = griffin->occupiedHex(contact);
	const auto direction = BattleHex::mutualPosition(contact, excursion->target);
	ASSERT_NE(direction, BattleHex::NONE);
	const auto behind = excursion->target.cloneInDirection(static_cast<BattleHex::EDir>(direction));
	ASSERT_TRUE(behind.isAvailable());
	ASSERT_EQ(battle()->battleGetUnitByPos(behind, true), nullptr);
	auto * friendly = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), behind, 10);
	const auto current = newHorizonsAerialSupremacy::prepare(*battle(), griffin, shooter);
	ASSERT_TRUE(current);
	ASSERT_EQ(current->landing, excursion->landing);
	for(const auto type : {BonusType::TWO_HEX_ATTACK_BREATH, BonusType::WIDE_BREATH})
	{
		const auto bonus = grant(griffin, type);
		const auto actual = battle()->getAerialAttackedBattleUnits(*current);
		EXPECT_TRUE(ids(actual.first).contains(friendly->unitId()));
		EXPECT_FALSE(ids(actual.first).contains(griffin->unitId()));
		griffin->removeBonus(bonus);
	}
}
namespace
{
class NewHorizonsAerialProtectTest : public NewHorizonsAerialSupremacyBattleTest
{
	void mapLoaded(CMap * map) override
	{
		NewHorizonsAerialSupremacyBattleTest::mapLoaded(map);
		// Protect is retained only by the supported historical Order capture.
		const JsonNode file(JsonPath::builtin("config/newHorizonsCombatV3"));
		const auto & rules = file["combat"]["heroCommands"];
		ASSERT_NO_THROW(heroCommands::validateRules(rules));
		ASSERT_FALSE(heroCommands::isRevisedRules(rules));
		map->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, rules);
	}
};
}
TEST_F(NewHorizonsAerialProtectTest, ProtectRedirectsDamageWithoutReplacingOriginalShooterContactDescriptor)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * protector = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 5), 1000);
	BattleSetActiveStack active;
	active.battleID = battle()->getBattleID();
	active.stack = protector->unitId();
	active.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(active);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			protector->unitId(), shooter->unitId())));
	const auto origin = griffin->getPosition();
	const auto shooterPosition = shooter->getPosition();
	const auto health = protector->getAvailableHealth();
	ASSERT_TRUE(shoot());
	ASSERT_EQ(server.attacks.size(), 2);
	const auto & counter = server.attacks.back();
	ASSERT_TRUE(counter.aerialExcursion);
	EXPECT_EQ(counter.aerialExcursion->opponent, shooter->unitId());
	EXPECT_EQ(counter.tile, shooterPosition);
	EXPECT_EQ(counter.aerialExcursion->origin, origin);
	EXPECT_LT(protector->getAvailableHealth(), health);
	EXPECT_EQ(griffin->getPosition(), origin);
	EXPECT_TRUE(std::ranges::any_of(counter.bsa, [protector](const BattleStackAttacked & hit)
		{ return hit.stackAttacked == protector->unitId(); }));
}
TEST_F(NewHorizonsAerialSupremacyBattleTest, NativeAerialCounterKeepsGrantedAllAdjacentAgainstSingleAndDoubleWideRecipients)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * single = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(6, 4), 1000);
	auto * wide = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(7, 6), 1000);
	ASSERT_FALSE(single->doubleWide());
	ASSERT_TRUE(wide->doubleWide());
	grant(griffin, BonusType::ATTACKS_ALL_ADJACENT);
	const auto excursion = newHorizonsAerialSupremacy::prepare(*battle(), griffin, shooter);
	ASSERT_TRUE(excursion);
	const auto contactTargets = ids(battle()->getAerialAttackedBattleUnits(*excursion).first);
	ASSERT_TRUE(contactTargets.contains(single->unitId()));
	ASSERT_TRUE(contactTargets.contains(wide->unitId()));
	const auto singleHealth = single->getAvailableHealth();
	const auto wideHealth = wide->getAvailableHealth();
	ASSERT_TRUE(shoot());
	ASSERT_EQ(server.attacks.size(), 2);
	const auto & counter = server.attacks.back();
	ASSERT_TRUE(counter.aerialExcursion);
	EXPECT_LT(single->getAvailableHealth(), singleHealth);
	EXPECT_LT(wide->getAvailableHealth(), wideHealth);
	EXPECT_TRUE(std::ranges::any_of(counter.bsa, [single](const BattleStackAttacked & hit)
		{ return hit.stackAttacked == single->unitId() && hit.isSecondary(); }));
	EXPECT_TRUE(std::ranges::any_of(counter.bsa, [wide](const BattleStackAttacked & hit)
		{ return hit.stackAttacked == wide->unitId() && hit.isSecondary(); }));
}
