/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "FullGameSnapshotTypes.h"
#include "../../../lib/battle/NewHorizonsCreatureSpecialtyCapabilities.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/nativeRules/NativeRulePilots.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include <array>
namespace
{
using namespace newHorizonsCreatureSpecialtyCapabilities;
Bonus rangedProfile()
{
	Bonus result(BonusDuration::PERMANENT, BonusType::SHOOTER, BonusSource::HERO_SPECIAL,
		1, BonusSourceID(HeroTypeID(0)));
	result.subtype = BonusSubtypeID(BonusCustomSubtype(static_cast<int32_t>(RangedProfileKind::Stonecaller)));
	result.parameters = std::make_shared<BonusParameters>(std::vector<int32_t>{1, 12, 100, 101});
	return result;
}
TEST(CreatureSpecialtyCapability, ReservedRangedProfileRequiresExactNumericVersionAndSource)
{
	const auto profile = rangedProfile();
	EXPECT_TRUE(isStonecallerShooterProfile(profile));
	auto ordinary = profile;
	ordinary.source = BonusSource::CREATURE_ABILITY;
	ordinary.parameters.reset();
	EXPECT_FALSE(isStonecallerShooterProfile(ordinary));
	for(const auto & values : {std::vector<int32_t>{2, 12, 100, 101}, {1, 13, 100, 101},
		{1, 12, -1, 101}, {1, 12, 100, 100}, {1, 12, 100}})
	{
		auto invalid = profile;
		invalid.parameters = std::make_shared<BonusParameters>(values);
		EXPECT_THROW(isStonecallerShooterProfile(invalid), std::runtime_error);
	}
}
TEST(CreatureSpecialtyCapability, ReservedRangedProfileRejectsUnsupportedCodecBeforePrefix)
{
	auto profile = rangedProfile();
	CMemorySerializer current;
	ASSERT_NO_THROW(profile.serialize(current.oser));
	Bonus restored;
	ASSERT_NO_THROW(restored.serialize(current.iser));
	EXPECT_TRUE(isStonecallerShooterProfile(restored));
	CMemorySerializer old;
	old.oser.version = static_cast<ESerializationVersion>(1156);
	EXPECT_THROW(profile.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}
class CreatureSpecialtyCapabilityTest : public HeroCommandFixture
{
protected:
	CStack * actor = nullptr;
	CStack * enemy = nullptr;
	void prepare(const char * hero, const char * family, int count = 10, int enemyCount = 100000)
	{
		useRevisedOrders = true;
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(hero)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode(family)), 1}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 10}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(BattleSide::ATTACKER, creatureByName(family), BattleHex(3, 5), count);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), enemyCount);
	}
	void activate()
	{
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = actor->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}
	void nextRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
		activate();
	}
	BattleAction cast(SpellID spell, const battle::Unit * target)
	{
		BattleAction request;
		request.actionType = EActionType::MONSTER_SPELL;
		request.side = BattleSide::ATTACKER;
		request.stackNumber = actor->unitId();
		request.spell = spell;
		request.aimToUnit(target);
		return request;
	}
};
TEST_F(CreatureSpecialtyCapabilityTest, StonecallerUsesCurrentScopedFamilyAndBaselineRatherThanModifiedAmmunition)
{
	prepare("core:thunar", "core:earthElemental");
	ASSERT_TRUE(stonecallerRanged(*actor));
	EXPECT_TRUE(actor->isShooter());
	EXPECT_EQ(actor->shots.total(), 12);
	EXPECT_FALSE(creatureByName("core:earthElemental").toCreature()->hasBonusOfType(BonusType::SHOOTER));
	auto * magma = addStack(BattleSide::ATTACKER, creatureByName("core:magmaElemental"), BattleHex(3, 7), 10);
	EXPECT_TRUE(stonecallerRanged(*magma));
	EXPECT_EQ(magma->shots.total(), 12);
	auto * air = addStack(BattleSide::ATTACKER, creatureByName("core:airElemental"), BattleHex(3, 3), 10);
	EXPECT_FALSE(stonecallerRanged(*air));
	auto * imported = addStack(BattleSide::DEFENDER, creatureByName("core:earthElemental"), BattleHex(10, 3), 10);
	EXPECT_FALSE(stonecallerRanged(*imported));
	EXPECT_FALSE(imported->isShooter());
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SHOTS, BonusSource::OTHER, 5, BonusSourceID()));
	EXPECT_EQ(actor->shots.total(), 17);
	EXPECT_TRUE(stonecallerRanged(*actor));
}
TEST_F(CreatureSpecialtyCapabilityTest, StonecallerKeepsOrdinaryRangeBlockingAndNoMeleePenalty)
{
	prepare("core:thunar", "core:magmaElemental");
	EXPECT_TRUE(actor->hasBonusOfType(BonusType::NO_MELEE_PENALTY));
	EXPECT_FALSE(actor->hasBonusOfType(BonusType::FREE_SHOOTING));
	EXPECT_FALSE(actor->hasBonusOfType(BonusType::NO_DISTANCE_PENALTY));
	EXPECT_FALSE(actor->hasBonusOfType(BonusType::NO_WALL_PENALTY));
	EXPECT_EQ(actor->getRangedFullDamageDistance(), GameConstants::BATTLE_SHOOTING_PENALTY_DISTANCE);
	EXPECT_TRUE(battle()->battleCanShootAction(actor, enemy->getPosition()));
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(4, 5), 10);
	EXPECT_FALSE(battle()->battleCanShootAction(actor, enemy->getPosition()));
	EXPECT_TRUE(stonecallerRanged(*actor)); // classification survives ineligible action state
}
TEST_F(CreatureSpecialtyCapabilityTest, TwelveAcceptedShotsExhaustOneSharedPoolButKeepLastShotPresentation)
{
	prepare("core:thunar", "core:earthElemental");
	beginCombat();
	activate();
	const auto action = BattleAction::makeShotAttack(actor, enemy);
	for(int shot = 0; shot < 12; ++shot)
	{
		if(shot) nextRound();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		EXPECT_EQ(actor->shots.available(), 11 - shot);
		EXPECT_TRUE(stonecallerRanged(*actor));
	}
	nextRound();
	const auto before = actor->acquireState()->save();
	const auto started = server.startedActions.size();
	EXPECT_FALSE(actor->canShoot());
	EXPECT_TRUE(stonecallerRanged(*actor));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(actor->shots.available(), 0);
	EXPECT_EQ(actor->acquireState()->save(), before);
	EXPECT_EQ(server.startedActions.size(), started);
}
TEST_F(CreatureSpecialtyCapabilityTest, ArchshamanGrantsOnlyOgreMagiAdvancedSpellsWithoutHeroPowerInheritance)
{
	prepare("core:vey", "core:ogreMage");
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 500, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(actor->getHeroCaster(), nullptr);
	EXPECT_EQ(actor->casts.total(), 3);
	for(const auto spell : {SpellID(SpellID::BERSERK), SpellID(SpellID::MISFORTUNE), SpellID(SpellID::BLOODLUST)})
	{
		EXPECT_TRUE(actor->hasBonusOfType(BonusType::SPELLCASTER, BonusSubtypeID(spell)));
		EXPECT_EQ(actor->getSpellSchoolLevel(spell.toSpell(), nullptr), 2);
		EXPECT_EQ(actor->getEnchantPower(spell.toSpell()), 3);
		EXPECT_EQ(actor->getEffectPower(spell.toSpell()), 0);
	}
	auto * base = addStack(BattleSide::ATTACKER, creatureByName("core:ogre"), BattleHex(3, 7), 10);
	EXPECT_FALSE(base->hasBonusOfType(BonusType::SPELLCASTER, BonusSubtypeID(SpellID(SpellID::BERSERK))));
	auto * imported = addStack(BattleSide::DEFENDER, creatureByName("core:ogreMage"), BattleHex(10, 3), 10);
	EXPECT_FALSE(imported->hasBonusOfType(BonusType::SPELLCASTER, BonusSubtypeID(SpellID(SpellID::MISFORTUNE))));
	EXPECT_TRUE(actor->getBonuses(Selector::type()(BonusType::PERCENTAGE_DAMAGE_BOOST)
		.And(Selector::sourceTypeSel(BonusSource::HERO_SPECIAL)))->empty());
}
TEST_F(CreatureSpecialtyCapabilityTest, ArchshamanSpellsAndNativeBloodlustSpendExistingThreeCharges)
{
	prepare("core:vey", "core:ogreMage", 1000, 10);
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::BLOCKS_RETALIATION,
		BonusSource::OTHER, 1, BonusSourceID()));
	beginCombat();
	activate();
	const auto mana = attackerSideHero->getManaAvailable();
	const std::array spells{SpellID(SpellID::MISFORTUNE), SpellID(SpellID::BLOODLUST), SpellID(SpellID::BERSERK)};
	for(size_t index = 0; index < spells.size(); ++index)
	{
		if(index) nextRound();
		const auto request = cast(spells[index], index == 1 ? actor : enemy);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), request));
		EXPECT_EQ(actor->casts.available(), 2 - static_cast<int>(index));
		EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	}
	nextRound();
	const auto before = actor->acquireState()->save();
	EXPECT_FALSE(actor->canCast());
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), cast(SpellID(SpellID::BLOODLUST), actor)));
	EXPECT_EQ(actor->acquireState()->save(), before);
	EXPECT_EQ(actor->casts.available(), 0);
}
TEST_F(CreatureSpecialtyCapabilityTest, ArchshamanRetainsOrdinaryCreatureAbilitySuppression)
{
	prepare("core:vey", "core:ogreMage");
	const auto suppression = nativeRules::creatureSuppression(SpellID(SpellID::FORGETFULNESS), "creatureAbilitySuppression", 3, false);
	actor->addNewBonus(std::make_shared<Bonus>(suppression));
	beginCombat();
	activate();
	const auto started = server.startedActions.size();
	const auto before = actor->acquireState()->save();
	const auto enemyBefore = enemy->acquireState()->save();
	const auto rng = [&]
	{
		CMemorySerializer wire;
		wire.oser & *gameHandler->randomizer;
		return wire.extractBuffer();
	};
	const auto randomBefore = rng();
	EXPECT_FALSE(actor->canCast());
	EXPECT_EQ(actor->casts.available(), 0); // suppression hides the native CASTS grant
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), cast(SpellID(SpellID::MISFORTUNE), enemy)));
	EXPECT_EQ(actor->acquireState()->save(), before);
	EXPECT_EQ(enemy->acquireState()->save(), enemyBefore);
	EXPECT_EQ(rng(), randomBefore);
	EXPECT_EQ(server.startedActions.size(), started);
	SetStackEffect removal;
	removal.battleID = BattleID(0);
	removal.toRemove.emplace_back(actor->unitId(), std::vector<Bonus>{suppression});
	gameHandler->sendAndApply(removal);
	EXPECT_EQ(actor->casts.available(), 3);
	EXPECT_TRUE(actor->canCast());
}
}
