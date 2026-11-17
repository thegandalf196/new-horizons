/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/battle/CreatureAbility.h"
#include "../../../lib/battle/ConsecratedArmsOrder.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"

namespace
{
using namespace creatureAbility;
class CreatureAbilityApplicationTest : public HeroCommandFixture
{
protected:
	CStack * recipient = nullptr;
	void prepare(bool unshakable = true)
	{
		useRevisedOrders = true;
		prepareCommands();
		recipient = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
		consecratedArms::Policy policy{1, unshakable ? static_cast<uint16_t>(consecratedArms::Perk::UNSHAKABLE_FAITH) : uint16_t{0}};
		auto prayer = consecratedArms::orderEffects(*battle(), BattleSide::DEFENDER,
			{recipient->unitId()}, {consecratedArms::Prayer::STEADFAST_SOUL}, policy, 600);
		gameHandler->sendAndApply(prayer);
	}
	SetStackEffect group(Descriptor descriptor)
	{
		std::vector<Bonus> components;
		for(const auto type : {BonusType::MORALE, BonusType::LUCK, BonusType::STACKS_SPEED})
		{
			Bonus component(BonusDuration::N_TURNS, type, BonusSource::OTHER, -1, BonusSourceID());
			component.turnsRemain = 2;
			component.statusIdentity = "creatureAbilityApplicationTest";
			component.statusTags = {BonusStatusTag::DEBUFF};
			component.abilityDescriptor = descriptor;
			components.push_back(std::move(component));
		}
		SetStackEffect packet;
		packet.battleID = battle()->getBattleID();
		packet.toAdd.emplace_back(recipient->unitId(), std::move(components));
		return packet;
	}
	std::vector<std::byte> rngBytes(CRandomGenerator & rng)
	{
		CMemorySerializer memory;
		memory.oser & rng;
		return memory.extractBuffer();
	}
};
}

TEST_F(CreatureAbilityApplicationTest, DirectThreeComponentAfflictionConsumesExactlyOneResistanceDraw)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CRandomGenerator expected(400);
	const bool resisted = expected.nextInt(9999) < 4000;
	auto & actual = dynamic_cast<CRandomGenerator &>(gameHandler->getRandomGenerator());
	actual.setSeed(400);
	auto packet = group(nativeAffliction(Native::CONCUSSIVE_STUN, Family::CREATURE_ABILITY, PlayerColor(0)));
	gameHandler->sendAndApply(packet);
	EXPECT_EQ(rngBytes(actual), rngBytes(expected));
	const auto applied = recipient->getBonuses(CSelector([](const Bonus * bonus)
	{
		return bonus->statusIdentity == "creatureAbilityApplicationTest";
	}));
	EXPECT_EQ(applied->size(), resisted ? 0 : 3);
}

TEST_F(CreatureAbilityApplicationTest, SpellResolvedAndUnknownHistoricalOriginsDoNotReceiveAnotherRoll)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto & actual = dynamic_cast<CRandomGenerator &>(gameHandler->getRandomGenerator());
	actual.setSeed(500);
	const auto before = rngBytes(actual);
	auto spellDescriptor = std::make_shared<CreatureAbilityDescriptor>(*nativeAffliction(
		Native::CONCUSSIVE_STUN, Family::CREATURE_ABILITY, PlayerColor(0)));
	spellDescriptor->resolution = Resolution::SPELL_CAST;
	auto spell = group(spellDescriptor);
	gameHandler->sendAndApply(spell);
	EXPECT_EQ(rngBytes(actual), before);
	auto legacy = group({});
	gameHandler->sendAndApply(legacy);
	EXPECT_EQ(rngBytes(actual), before);
}

TEST_F(CreatureAbilityApplicationTest, PrayerWithoutUnshakableDoesNotChangePhysicalAfflictionOrRng)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	auto & actual = dynamic_cast<CRandomGenerator &>(gameHandler->getRandomGenerator());
	actual.setSeed(600);
	const auto before = rngBytes(actual);
	auto packet = group(nativeAffliction(Native::PHYSICAL_POISON, Family::CREATURE_ABILITY, PlayerColor(0)));
	gameHandler->sendAndApply(packet);
	EXPECT_EQ(rngBytes(actual), before);
	EXPECT_FALSE(packet.toAdd.empty());
}

TEST_F(CreatureAbilityApplicationTest, FamilyIrresistibilityAndWholeCastDamageClassificationWinOverProtection)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto creature = nativeAffliction(Native::PHYSICAL_POISON, Family::CREATURE_ABILITY, PlayerColor(0));
	const auto hero = nativeAffliction(Native::PHYSICAL_POISON, Family::HERO_SPELL, PlayerColor(0));
	const auto skill = nativeAffliction(Native::PHYSICAL_POISON, Family::SKILL, PlayerColor(0));
	EXPECT_EQ(unshakableResistanceBP(*creature, *recipient), 4000);
	EXPECT_EQ(unshakableResistanceBP(*hero, *recipient), 0);
	EXPECT_EQ(unshakableResistanceBP(*skill, *recipient), 0);
	auto irresistible = *creature;
	irresistible.interactions |= IRRESISTIBLE;
	EXPECT_EQ(unshakableResistanceBP(irresistible, *recipient), 0);
	auto damage = *creature;
	damage.categories |= DAMAGE;
	EXPECT_EQ(unshakableResistanceBP(damage, *recipient), 4000);
	damage.resolution = Resolution::SPELL_CAST;
	EXPECT_EQ(unshakableResistanceBP(damage, *recipient), 0);
}
