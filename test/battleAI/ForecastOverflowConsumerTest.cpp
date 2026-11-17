/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapterFixture.h"
#include "../../AI/BattleAI/ForecastHeroActionAdmission.h"
#include "../../AI/BattleAI/HeroActionAdmission.h"
#include "../../AI/BattleAI/PossibleSpellcast.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMetamagicOverflow.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/bonuses/Propagators.h"
#include <typeinfo>

namespace forecastTest
{
TEST_F(ForecastNativeAdapterTest, OrdinaryTowerMetamagicOverflowUsesCompactTwoEndpointReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, true, "core:pikeman", false, false, "core:neela", true));
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	hero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
	hero->setPrimarySkill(PrimarySkill::SPELL_POWER, 10, ChangeValueMode::ABSOLUTE);
	parent = std::make_shared<HypotheticBattle>(environment.get(), visible);
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), visible);
	std::vector<uint32_t> enemies;
	for(const auto * unit : parent->battleGetAllUnits(false))
		if(unit->unitSide() == BattleSide::DEFENDER) enemies.push_back(unit->unitId());
	ASSERT_EQ(enemies.size(), 2u);
	const auto primary = enemies[0], secondary = enemies[1];
	// Native town information reaches both combatants through PLAYER ancestry.
	// Its provider survives unit deaths, so it is not an outgoing combat aura.
	for(const auto id : {primary, secondary})
	{
		const auto guild = parent->battleGetUnitByID(id)->getUnstackedBonuses(Selector::type()(BonusType::THIEVES_GUILD_ACCESS));
		ASSERT_EQ(guild->size(), 1u);
		const auto & row = *guild->front();
		EXPECT_EQ(row.source, BonusSource::TOWN_STRUCTURE);
		const auto building = row.sid.as<BuildingTypeUniqueID>();
		ASSERT_TRUE(building.hasValue());
		EXPECT_EQ(building.getBuilding(), BuildingID::TAVERN);
		EXPECT_EQ(row.val, 1); EXPECT_EQ(row.valType, BonusValueType::ADDITIVE_VALUE);
		EXPECT_EQ(row.duration, BonusDuration::PERMANENT);
		EXPECT_FALSE(row.parameters); EXPECT_FALSE(row.limiter); EXPECT_FALSE(row.updater);
		EXPECT_FALSE(row.propagationUpdater);
		ASSERT_TRUE(row.propagator);
		ASSERT_EQ(typeid(*row.propagator), typeid(CPropagatorNodeType));
		EXPECT_EQ(row.propagator->getPropagatorType(), BonusNodeType::PLAYER);
	}
	for(auto * board : {parent.get(), reference.get()})
	{
		auto amount = board->battleGetUnitByID(primary)->getAvailableHealth() - 1;
		board->getForUpdate(primary)->damage(amount, false, battle::DamageProvenance::OTHER);
	}
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *sibling);
	PossibleSpellcast candidate;
	candidate.spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	candidate.dest = {spells::Destination(parent->battleGetUnitByID(primary))};
	ASSERT_EQ(forecast::heroAction::classify(*parent, BattleSide::ATTACKER, candidate).route,
		forecast::heroAction::Route::Compact);
	const auto random = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	const auto secondaryBefore = sibling->battleGetUnitByID(secondary)->getAvailableHealth();
	for(auto * board : {parent.get(), reference.get()})
	{
		const auto admission = heroActionProjection::admitCandidate(*board, BattleSide::ATTACKER, candidate);
		ASSERT_TRUE(admission.admitted);
		ASSERT_TRUE(admission.spellAllowance);
		spells::BattleCast cast(board, hero, spells::Mode::HERO, candidate.spell);
		auto mechanics = candidate.spell->battleMechanics(&cast);
		const spells::Target target{spells::Destination(board->battleGetUnitByID(primary))};
		ASSERT_EQ(newHorizonsMetamagic::overflowSupport(*mechanics, target), newHorizonsMetamagic::OverflowSupport::Supported);
		mechanics->setMetamagicOverflowProjectionOutcome(newHorizonsMetamagic::OverflowProjectionOutcome::BothLand);
		mechanics->castEval(board->getServerCallback(), target);
		ASSERT_TRUE(mechanics->metamagicOverflowReceipt());
		ASSERT_EQ(mechanics->metamagicOverflowReceipt()->recipient, secondary);
		ASSERT_GT(mechanics->metamagicOverflowReceipt()->damageIfLands, 0);
		ASSERT_TRUE(board->projectAcceptedHeroSpell(BattleSide::ATTACKER, SpellID::MAGIC_ARROW, primary, *admission.spellAllowance));
	}
	ASSERT_TRUE(parent->forecastState());
	EXPECT_EQ(parent->battleGetUnitByID(primary)->getAvailableHealth(), reference->battleGetUnitByID(primary)->getAvailableHealth());
	EXPECT_EQ(parent->battleGetUnitByID(secondary)->getAvailableHealth(), reference->battleGetUnitByID(secondary)->getAvailableHealth());
	EXPECT_EQ(parent->forecastState()->unit(secondary)->health.available(), reference->battleGetUnitByID(secondary)->getAvailableHealth());
	EXPECT_LT(parent->battleGetUnitByID(secondary)->getAvailableHealth(), secondaryBefore);
	EXPECT_EQ(sibling->battleGetUnitByID(primary)->getAvailableHealth(), 1);
	EXPECT_EQ(sibling->battleGetUnitByID(secondary)->getAvailableHealth(), secondaryBefore);
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), random);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
}
}
