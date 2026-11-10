/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/CommandArmyBonuses.h"
#include "../../../lib/battle/NewHorizonsWarfare.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CurrentFireBasis.h"
#include "../../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/serializer/CMemorySerializer.h"

namespace
{
class CommandArmyProjectionTest : public HeroCommandFixture
{
protected:
	CommandArmyProjectionTest() { useRevisedOrders = true; }
	void select(const char * skill, std::initializer_list<const char *> perks)
	{
		int rank = 0;
		for(const auto * perk : perks)
		{
			SCOPED_TRACE(std::string(skill) + "." + perk);
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), ++rank, ChangeValueMode::ABSOLUTE);
			ASSERT_NO_THROW(attackerSideHero->applyPerkSelection({skill, std::string(skill) + "." + perk}));
			ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, std::string(skill) + "." + perk));
		}
	}
	CStack * slot(BattleSide side, int index)
	{
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->unitSide() == side && unit->unitSlot() == SlotID(index)) return battle()->getStack(unit->unitId(), false);
		return nullptr;
	}
	void controlledAttack(CStack *& source, CStack *& target, bool shooting = false)
	{
		startGame();
		startBattle();
		source = addStack(BattleSide::ATTACKER,
			CreatureID(CreatureID::decode(shooting ? "core:magog" : "core:pikeman")), BattleHex(leftHex), 10);
		target = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(rightHex), 1000);
		ASSERT_NE(source, nullptr);
		ASSERT_NE(target, nullptr);
		for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
		{
			source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
				BonusSource::OTHER, 10, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK), bound));
			target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
				BonusSource::OTHER, 10, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE), bound));
			for(const auto subtype : {BonusCustomSubtype::creatureDamageMin, BonusCustomSubtype::creatureDamageMax})
				source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::CREATURE_DAMAGE,
					BonusSource::OTHER, 100, BonusSourceID(), subtype, bound));
		}
		ASSERT_EQ(source->getAttack(shooting), target->getDefense(shooting));
		ASSERT_EQ(target->intrinsicMagicalDamageReductionPercent(), 0);
		ASSERT_EQ(newHorizonsMagicalAbilityDamage::spellwardReductionBasisPoints(battle()->battleGetOwnerHero(target)), 0);
	}
	void ward(CStack & target, int warding, int training)
	{
		newHorizonsHeroes::CommandSlotModifiers modifiers;
		modifiers.enhancements.magicalReductionPercent = warding;
		modifiers.authority.magicalReductionPercent = training;
		target.removeBonuses(CSelector([](const Bonus * bonus) { return commandArmyBonuses::isProjectedBonus(*bonus); }));
		for(auto bonus : commandArmyBonuses::project(modifiers))
			target.addNewBonus(std::make_shared<Bonus>(std::move(bonus)));
	}
};
}

TEST_F(CommandArmyProjectionTest, RealOriginReceivesOnePackageWhileNumericSlotAndSummonDoNot)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(select("new-horizons:authority", {"offensiveDrills"}));
	startBattle();
	auto * original = slot(BattleSide::ATTACKER, 0);
	ASSERT_NE(original, nullptr);
	ASSERT_EQ(commandArmyBonuses::originHero(*original), attackerSideHero);
	const auto * temporary = addStack(BattleSide::ATTACKER, original->creatureId(), BattleHex(80), 10);
	ASSERT_NE(temporary, nullptr);
	EXPECT_EQ(commandArmyBonuses::originHero(*temporary), nullptr);
	EXPECT_EQ(original->getAttack(false), temporary->getAttack(false) + 2);
	CStack wrongSlot(original->base, PlayerColor(0), 999, BattleSide::ATTACKER, SlotID(1));
	EXPECT_EQ(commandArmyBonuses::originHero(wrongSlot), nullptr);
	int64_t damage = original->getMaxHealth() + 1;
	original->damage(damage);
	const auto count = original->getCount();
	const auto health = original->getAvailableHealth();
	const auto attack = original->getAttack(false);
	commandArmyBonuses::initialize(*original);
	EXPECT_EQ(original->getAttack(false), attack);
	EXPECT_EQ(original->getCount(), count);
	EXPECT_EQ(original->getAvailableHealth(), health);
	for(const auto & bonus : original->base->getExportedBonusList())
		EXPECT_FALSE(bonus->stacking.starts_with("new-horizons:commandArmy."));
}

TEST_F(CommandArmyProjectionTest, LordOfTheDeadChangesOnlySkeletonFamilyMaximumHealth)
{
	startGame();
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:vidomina")));
	ASSERT_NO_FATAL_FAILURE(select("new-horizons:necromancy", {"graveKnowledge", "boneCollector", "lordOfTheDead"}));
	const auto captured = newHorizonsNecromancy::policy(attackerSideHero->getPerkState().rules);
	ASSERT_TRUE(captured);
	ASSERT_EQ(captured->version, 3);
	ASSERT_EQ(captured->lordOfTheDeadMaximumHealthPercent, 25);
	EXPECT_EQ(newHorizonsNecromancy::skeletonLeadershipReductionPercent(attackerSideHero, CreatureID(CreatureID::SKELETON)), 0);
	startBattle();
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	for(const auto creature : {CreatureID(CreatureID::SKELETON), CreatureID(CreatureID::decode("core:skeletonWarrior")), pikeman})
	{
		auto * recipient = addStack(BattleSide::ATTACKER, creature, BattleHex(80), 10);
		auto * control = addStack(BattleSide::DEFENDER, creature, BattleHex(90), 10);
		ASSERT_NE(recipient, nullptr);
		ASSERT_NE(control, nullptr);
		const int multiplier = creature == pikeman ? 100 : 125;
		EXPECT_EQ(recipient->getMaxHealth(), control->getMaxHealth() * multiplier / 100);
		EXPECT_EQ(recipient->getCount(), 10);
		EXPECT_EQ(recipient->acquireState()->getMaxHealth(), recipient->getMaxHealth());
		battle()->removeUnit(recipient->unitId());
		battle()->removeUnit(control->unitId());
	}
}

TEST_F(CommandArmyProjectionTest, ActualOrdinaryMagicalAttackAndRetaliationReduce1000To630ExactlyOnce)
{
	CStack * source = nullptr;
	CStack * target = nullptr;
	ASSERT_NO_FATAL_FAILURE(controlledAttack(source, target));
	BattleAttackInfo attack(source, target, 0, false);
	attack.physicalDamage = false;
	const auto raw = battle()->calculateDmgRange(attack);
	ASSERT_EQ(raw.damage.min, 1000);
	ASSERT_EQ(raw.damage.max, 1000);
	ward(*target, 10 + 20, 10);
	for(bool retaliation : {false, true})
	{
		attack.retaliation = retaliation;
		attack.physicalDamage = false;
		const auto magical = battle()->calculateDmgRange(attack);
		EXPECT_EQ(magical.damage.min, 630);
		EXPECT_EQ(magical.damage.max, 630);
		EXPECT_EQ(magical.kills.min, 630 / target->getMaxHealth());
		EXPECT_EQ(magical.kills.max, magical.kills.min);
		attack.physicalDamage = true;
		const auto physical = battle()->calculateDmgRange(attack);
		EXPECT_EQ(physical.damage.min, 1000);
		EXPECT_EQ(physical.damage.max, 1000);
	}
}

TEST_F(CommandArmyProjectionTest, ActualMagicalAttackCombinesSourcesBeforeCapAndRelativePenetration)
{
	CStack * source = nullptr;
	CStack * target = nullptr;
	ASSERT_NO_FATAL_FAILURE(controlledAttack(source, target));
	BattleAttackInfo attack(source, target, 0, false);
	attack.physicalDamage = false;
	ward(*target, 10000, 10); // Sum of Warding awards bounds to 100%, total MDR caps at 95%.
	for(bool retaliation : {false, true})
	{
		attack.retaliation = retaliation;
		const auto capped = battle()->calculateDmgRange(attack);
		EXPECT_EQ(capped.damage.min, 50);
		EXPECT_EQ(capped.damage.max, 50);
	}
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION, BonusSource::OTHER, 50, BonusSourceID()));
	for(bool retaliation : {false, true})
	{
		attack.retaliation = retaliation;
		const auto penetratedCap = battle()->calculateDmgRange(attack);
		EXPECT_EQ(penetratedCap.damage.min, 525); // 1000 * (1 - .95 * .50).
		EXPECT_EQ(penetratedCap.damage.max, 525);
	}
	ward(*target, 30, 10);
	const auto penetrated = battle()->calculateDmgRange(attack);
	EXPECT_EQ(penetrated.damage.min, 815); // 1000 * (1 - (1 - .70 * .90) * .50).
	EXPECT_EQ(penetrated.damage.max, 815);
	attack.physicalDamage = true;
	EXPECT_EQ(battle()->calculateDmgRange(attack).damage.min, 1000);
}

TEST_F(CommandArmyProjectionTest, ActualSpellLikeCreatureShotUsesCommandMdrWithoutOtherMagicalProtection)
{
	CStack * source = nullptr;
	CStack * target = nullptr;
	ASSERT_NO_FATAL_FAILURE(controlledAttack(source, target, true));
	const BattleAttackInfo shot(source, target, 0, true);
	ASSERT_FALSE(shot.physicalDamage); // Native Magog classification, not a test override.
	const auto raw = battle()->calculateDmgRange(shot);
	ASSERT_EQ(raw.damage.min, 1000);
	ASSERT_EQ(raw.damage.max, 1000);
	ward(*target, 30, 10);
	const auto reduced = battle()->calculateDmgRange(shot);
	EXPECT_EQ(reduced.damage.min, 630);
	EXPECT_EQ(reduced.damage.max, 630);
}

TEST_F(CommandArmyProjectionTest, OlderLobbyWriterRejectsCurrentCommandWorldBeforeEnvelopePrefix)
{
	startGame();
	LobbyStartGame lobby;
	lobby.initializedGameState = gameState();
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_NATIVE_RULES_REDESIGN;
	try
	{
		lobby.serialize(memory.oser);
		FAIL() << "Expected current-world refusal before the lobby envelope";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_STREQ(error.what(), "Army Command requires a new game/save format");
	}
	EXPECT_TRUE(memory.extractBuffer().empty());
}

TEST_F(CommandArmyProjectionTest, PrimordialHostCoversDirectAttackModesWithoutAnyAttunementFlag)
{
	startGame();
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
	ASSERT_NO_FATAL_FAILURE(select("new-horizons:elementalRebirth", {"elementalAttunement", "rebirthChain", "phoenixSpark"}));
	startBattle();
	ASSERT_EQ(battle()->getElementalAttunementMask(BattleSide::ATTACKER), 0);
	const auto * target = slot(BattleSide::DEFENDER, 0);
	ASSERT_NE(target, nullptr);
	for(const auto * key : {"core:airElemental", "core:stormElemental", "core:fireElemental", "core:energyElemental",
		"core:waterElemental", "core:iceElemental", "core:earthElemental", "core:magmaElemental", "core:firebird", "core:phoenix",
		"core:pixie", "core:sprite", "core:psychicElemental", "core:magicElemental"})
	{
		auto * recipient = addStack(BattleSide::ATTACKER, creatureByName(key), BattleHex(80), 10);
		ASSERT_NE(recipient, nullptr);
		const std::string name(key);
		const bool eligible = name != "core:pixie" && name != "core:sprite"
			&& name != "core:psychicElemental" && name != "core:magicElemental";
		EXPECT_EQ(commandArmyBonuses::directAttackPercent(*recipient), eligible ? 15 : 0);
		for(bool physical : {false, true}) for(bool shooting : {false, true}) for(bool retaliation : {false, true})
		{
			BattleAttackInfo attack(recipient, target, 0, shooting);
			attack.physicalDamage = physical;
			attack.retaliation = retaliation;
			DamageAttackInfo payload;
			newHorizonsWarfare::appendAttackModifiers(*battle(), attack, payload);
			EXPECT_EQ(payload.heroOrderFinalDamageMultipliers, eligible ? std::vector<int>{115} : std::vector<int>{});
		}
		battle()->removeUnit(recipient->unitId());
	}
}
