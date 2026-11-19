/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapterFixture.h"
#include "../../AI/BattleAI/ForecastSkillSpecialtyAdmission.h"

TEST_F(ForecastNativeAdapterTest, InertSkillMarkerRequiresExactAuthoredHeroLocalIdentityAndShape)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:pikeman", false, false, "core:orrin", false, true));
	const auto * hero = parent->battleGetVisibleHeroPolicy(BattleSide::ATTACKER).legacyHero();
	ASSERT_NE(hero, nullptr);
	const auto skill = hero->getHeroType()->secondarySkillSpecialtyAlias;
	ASSERT_TRUE(skill);
	const auto & local = hero->getExportedBonusList();
	const auto found = std::ranges::find_if(local, [&](const auto & row)
	{ return row && newHorizonsHeroes::skillSpecialtyMarker::matches(*row, hero->getHeroTypeID(), skill->skill); });
	ASSERT_NE(found, local.end());
	ASSERT_TRUE(forecast::isOwnedSkillSpecialtyMarker(**found, hero));
	EXPECT_FALSE(forecast::isOwnedSkillSpecialtyMarker(**found, nullptr));
	const auto copied = std::make_shared<Bonus>(**found);
	EXPECT_FALSE(forecast::isOwnedSkillSpecialtyMarker(*copied, hero));
	for(int mutation = 0; mutation < 8; ++mutation)
	{
		Bonus malformed(**found);
		switch(mutation)
		{
		case 0: malformed.val = 1; break;
		case 1: malformed.duration = BonusDuration::ONE_BATTLE; break;
		case 2: malformed.stacking += ":foreign"; break;
		case 3: malformed.source = BonusSource::OTHER; break;
		case 4: malformed.hidden = true; break;
		case 5: malformed.statusIdentity = "foreign"; break;
		case 6: malformed.turnsRemain = 1; break;
		case 7: malformed.limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::REBIRTH); break;
		}
		EXPECT_FALSE(newHorizonsHeroes::skillSpecialtyMarker::matches(malformed, hero->getHeroTypeID(), skill->skill)) << mutation;
	}
	const auto units = parent->battleGetAllUnits(false);
	ASSERT_FALSE(units.empty());
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), visible);
	branch->getForUpdate(units.front()->unitId())->bonusesToAdd.push_back(copied);
	EXPECT_FALSE(forecast::NativeAdapter::prepareExchange(*branch));
	EXPECT_FALSE(branch->forecastState());
}

TEST_F(ForecastNativeAdapterTest, SpellLearningMetadataDoesNotGrantHeroResourcesOrChangeOrdinaryDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto roster = parent->battleGetAllUnits(false);
	ASSERT_EQ(roster.size(), 2u);
	auto target = parent->getForUpdate(roster.back()->unitId());
	for(const auto type : {BonusType::LEARN_BATTLE_SPELL_CHANCE, BonusType::LEARN_BATTLE_SPELL_LEVEL_LIMIT,
		BonusType::LEARN_BATTLE_SPELL_CHANCE_PRE_BATTLE, BonusType::LEARN_BATTLE_SPELL_LEVEL_LIMIT_PRE_BATTLE})
	{
		auto row = std::make_shared<Bonus>();
		row->type = type; row->source = BonusSource::GLOBAL; row->val = 10;
		target->bonusesToAdd.push_back(row);
	}
	const auto outside = gameState()->saveToMemory();
	const auto authority = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), parent);
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	EXPECT_FALSE(parent->forecastState()->supports(forecast::Event::HeroResource));
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	auto actual = child->getForUpdate(target->unitId());
	int64_t damage = 37, expected = damage;
	reference->getForUpdate(target->unitId())->damage(expected, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	ASSERT_TRUE(forecast::NativeAdapter::damage(*child, *actual, damage, battle::DamageProvenance::PHYSICAL_CREATURE, nullptr));
	EXPECT_EQ(damage, expected);
	EXPECT_EQ(actual->save(), reference->getForUpdate(target->unitId())->save());
	EXPECT_EQ(parent->getForUpdate(target->unitId())->getAvailableHealth(), target->getAvailableHealth());
	EXPECT_EQ(gameState()->saveToMemory(), outside);
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), authority);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
	// The type exemption does not admit a previously unsupported clock.
	auto invalid = std::make_shared<HypotheticBattle>(environment.get(), visible);
	auto timed = std::make_shared<Bonus>();
	timed->type = BonusType::LEARN_BATTLE_SPELL_CHANCE; timed->source = BonusSource::GLOBAL;
	timed->duration = BonusDuration::N_TURNS; timed->turnsRemain = 1;
	invalid->getForUpdate(target->unitId())->bonusesToAdd.push_back(timed);
	EXPECT_FALSE(forecast::NativeAdapter::prepareExchange(*invalid));
	EXPECT_FALSE(invalid->forecastState());
}
