/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NativeRosterSpellLuaParityFixture.h"

namespace test
{
TEST_F(NativeRosterSpellLuaParity, ReanimatePreservesTemporaryCasualtiesAndWoundBudget)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	int64_t damage = 900;
	child->getForUpdate(braced->unitId())->damage(damage);
	ASSERT_NO_FATAL_FAILURE(compare("reanimate", "new-horizons:reanimate", JsonNode(), source()));
}

TEST_F(NativeRosterSpellLuaParity, SacrificePreservesHealingBeforeVictimRemovalAndResurrectionProvenance)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto victim = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(100), 20)->unitId();
	resetChild();
	const auto target = braced->unitId();
	auto corpse = child->getForUpdate(target);
	auto damage = corpse->getAvailableHealth();
	corpse->damage(damage);
	JsonNode config;
	config["healLevel"].String() = "resurrect";
	config["healPower"].String() = "permanent";
	ASSERT_NO_FATAL_FAILURE(compare("sacrifice", "core:sacrifice", config,
		[target, victim](const HypotheticBattle & battle)
		{
			return spells::Target{spells::Destination(battle.battleGetUnitByID(target)),
				spells::Destination(battle.battleGetUnitByID(victim))};
		}));
}

TEST_F(NativeRosterSpellLuaParity, NaturesWrathPreservesMixedHealDamageHopOrderAndResolvedRecipients)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	child->getForUpdate(incoming->unitId())->position = BattleHex(94);
	int64_t damage = 900;
	child->getForUpdate(braced->unitId())->damage(damage);
	ASSERT_NO_FATAL_FAILURE(compare("naturesWrath", "new-horizons:naturesWrath", JsonNode(), source(), {braced->unitId(), incoming->unitId()}));
}

TEST_F(NativeRosterSpellLuaParity, PandemoniumPreservesDebuffSnapshotBeforeAnyRecipientDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	Bonus slow(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT,
		-2, BonusSourceID(SpellID(SpellID::SLOW)));
	slow.turnsRemain = 3;
	slow.statusTags = {BonusStatusTag::DEBUFF};
	slow.statusIdentity = "core:slow";
	child->addUnitBonus(incoming->unitId(), {slow});
	ASSERT_NO_FATAL_FAILURE(compare("pandemonium", "new-horizons:pandemonium", JsonNode(), source(), {incoming->unitId()}));
}

TEST_F(NativeRosterSpellLuaParity, TransfigureMatterPreservesObstacleRemovalBeforeGolemCreationAndTrim)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto obstacle = std::make_shared<CObstacleInstance>();
	obstacle->obstacleType = CObstacleInstance::USUAL;
	obstacle->ID = 0;
	obstacle->uniqueID = 7;
	obstacle->pos = BattleHex(77);
	ASSERT_FALSE(obstacle->getAffectedTiles().empty());
	const auto target = obstacle->getAffectedTiles().front();
	battle()->obstacles.push_back(obstacle);
	resetChild();
	JsonNode config;
	config["id"].String() = "core:diamondGolem";
	ASSERT_NO_FATAL_FAILURE(compare("transfigureMatter", "new-horizons:transfigureMatter", config,
		[target](const HypotheticBattle &) { return spells::Target{spells::Destination(target)}; }));
}
}
