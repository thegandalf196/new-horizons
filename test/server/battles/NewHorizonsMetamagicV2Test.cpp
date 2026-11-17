/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/spells/NewHorizonsMetamagicCore.h"
#include "../../../lib/spells/NewHorizonsMetamagicOverflow.h"
#include "../../../lib/spells/NewHorizonsMetamagicEnchantments.h"
#include "../../../lib/spells/NewHorizonsMetamagicAmmunition.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/AbilityCaster.h"
#include "../../../lib/spells/ObstacleCasterProxy.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsSorcerySpells.h"
#include "../../../lib/battle/ConsecratedArms.h"
#include "../../../lib/combatScripts/nativeEvents/NativeEventCommon.h"
#include "../../../lib/nativeRules/CapturedRuleSet.h"
#include "../../../lib/nativeRules/CompiledMetamagicRules.h"
#include "../../../lib/nativeRules/CapturedRuleExecutionAudit.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/NewHorizonsFocusMagic.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../mock/mock_ServerCallback.h"
#include "../../mock/mock_vstd_RNG.h"
namespace
{
class MetamagicV2 : public HeroCommandFixture
{
protected:
	testing::NiceMock<ServerCallbackMock> authority;
	testing::NiceMock<vstd::RNGMock> random;
	CStack * primary = nullptr;
	CStack * second = nullptr;
	CStack * third = nullptr;
	void SetUp() override
	{
		useRevisedOrders = true;
		HeroCommandFixture::SetUp();
		ON_CALL(authority, getRNG()).WillByDefault(testing::Return(&random));
		ON_CALL(random, nextInt(testing::_, testing::_)).WillByDefault(testing::Return(99));
		ON_CALL(authority, apply(testing::Matcher<CPackForClient &>(testing::_))).WillByDefault([&](CPackForClient & p) { gameHandler->sendAndApply(p); });
		ON_CALL(authority, apply(testing::Matcher<StacksInjured &>(testing::_))).WillByDefault([&](StacksInjured & p) { gameHandler->sendAndApply(p); });
		ON_CALL(authority, apply(testing::Matcher<SetStackEffect &>(testing::_))).WillByDefault([&](SetStackEffect & p) { gameHandler->sendAndApply(p); });
		ON_CALL(authority, apply(testing::Matcher<BattleUnitsChanged &>(testing::_))).WillByDefault([&](BattleUnitsChanged & p) { gameHandler->sendAndApply(p); });
	}
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		auto perks = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepare()
	{
		startGame();
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:neela")));
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")), 3, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), 3, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		setTestSpellPointTotal(attackerSideHero, 100);
		startBattle();
		primary = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(10, 5), 10);
		second = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(11, 5), 100);
		third = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(10, 6), 100);
		ASSERT_NE(primary, nullptr); ASSERT_NE(second, nullptr); ASSERT_NE(third, nullptr);
		beginCombat();
	}
	void learn(const char * basic, const char * advanced = nullptr, const char * expert = nullptr)
	{
		for(const auto * name : {basic, advanced, expert})
			if(name)
			{
				const auto id = std::string("new-horizons:metamagic.") + name;
				attackerSideHero->applyPerkSelection({"new-horizons:metamagic", id});
				ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:metamagic", id));
			}
	}
	spells::BattleCast cast(SpellID spell = SpellID::MAGIC_ARROW)
	{
		spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		event.setEffectValue(300); event.setSpellLevel(0);
		return event;
	}
	void bonus(CStack & unit, BonusType type, int value, BonusSubtypeID subtype = BonusSubtypeID{})
	{
		unit.addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, value, BonusSourceID(), subtype));
	}
};
}
TEST_F(MetamagicV2, CascadeDuplicatesFullSpillAndPublishesTwoDistinctInjuriesTogether)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(learn("conservedFormula", "widenSpells", "arcaneCascade"));
	auto event = cast();
	auto mechanics = event.getSpell()->battleMechanics(&event);
	const spells::Target aim{spells::Destination(primary)};
	const auto preview = newHorizonsMetamagic::previewOverflow(*mechanics, aim);
	ASSERT_TRUE(preview);
	ASSERT_EQ(preview->additionalRecipients.size(), 1u);
	EXPECT_EQ(preview->rawSpill, 120);
	EXPECT_EQ(preview->additionalRecipients.front().rawSpill, 120);
	EXPECT_NE(preview->recipient, preview->additionalRecipients.front().recipient);
	const auto hp2 = second->getAvailableHealth(), hp3 = third->getAvailableHealth();
	mechanics->cast(&authority, aim);
	EXPECT_FALSE(primary->alive());
	EXPECT_EQ(hp2 - second->getAvailableHealth(), 120);
	EXPECT_EQ(hp3 - third->getAvailableHealth(), 120);
	const auto packet = std::ranges::find_if(server.injuries, [](const StacksInjured & p)
	{
		return p.stacks.size() == 2 && std::ranges::all_of(p.stacks, [](const BattleStackAttacked & hit)
			{ return hit.flags & BattleStackAttacked::SECONDARY; });
	});
	ASSERT_NE(packet, server.injuries.end());
	EXPECT_EQ(server.casts.size(), 1u);
}
TEST_F(MetamagicV2, EmpoweredArrowBypassesResistanceButRetainsMagicalImmunityAndRecallRules)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(learn("conservedFormula", "widenSpells", "empoweredSpells"));
	bonus(*primary, BonusType::MAGIC_RESISTANCE, 75);
	auto event = cast();
	auto mechanics = event.getSpell()->battleMechanics(&event);
	EXPECT_TRUE(newHorizonsMetamagic::empoweredDamage(*mechanics));
	EXPECT_FALSE(mechanics->wouldResist(primary));
	const auto recall = SpellID(SpellID::decode("new-horizons:forcedRecall"));
	auto recallEvent = cast(recall);
	auto recallMechanics = recallEvent.getSpell()->battleMechanics(&recallEvent);
	EXPECT_FALSE(newHorizonsMetamagic::empoweredDamage(*recallMechanics));
	EXPECT_EQ(newHorizonsMetamagic::debuffRetryChance(*recallMechanics), 0);
	bonus(*primary, BonusType::SPELL_IMMUNITY, 0, BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW)));
	EXPECT_FALSE(mechanics->isReceptive(primary));
}
TEST_F(MetamagicV2, LevitationUsesTheAcceptedEnchantmentLifetimeAndInstantHealingHasNoSeed)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(learn("conservedFormula", "levitation"));
	const battle::Unit * ally = nullptr;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(battle()->battleGetOwner(unit) == PlayerColor(0)) { ally = unit; break; }
	ASSERT_NE(ally, nullptr);
	const auto spell = SpellID(SpellID::BLESS);
	Bonus original(BonusDuration::N_TURNS, BonusType::ALWAYS_MAXIMUM_DAMAGE, BonusSource::SPELL_EFFECT, 1, BonusSourceID(spell));
	original.turnsRemain = 2;
	original.spellCasterOwner = PlayerColor(0);
	SetStackEffect packet;
	packet.toUpdate.emplace_back(ally->unitId(), std::vector<Bonus>{original});
	newHorizonsMetamagic::enchantmentCapabilities(attackerSideHero, *battle(), spell, spell, packet);
	ASSERT_EQ(packet.toUpdate.front().second.size(), 2u);
	const auto & flying = packet.toUpdate.front().second.back();
	EXPECT_EQ(flying.type, BonusType::FLYING);
	EXPECT_EQ(flying.sid, original.sid);
	EXPECT_EQ(flying.turnsRemain, 2);
	EXPECT_EQ(flying.spellCasterOwner, original.spellCasterOwner);
	SetStackEffect empty;
	newHorizonsMetamagic::enchantmentCapabilities(attackerSideHero, *battle(), SpellID::CURE, SpellID::CURE, empty);
	EXPECT_TRUE(empty.toAdd.empty()); EXPECT_TRUE(empty.toUpdate.empty());
}
TEST_F(MetamagicV2, DisruptionRemovesThreeFiniteShotsAndConvertedFocusWithoutGrantingActions)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	bonus(*second, BonusType::SHOOTER, 1);
	bonus(*second, BonusType::SHOTS, 10);
	const auto before = second->acquireState()->shots.available();
	newHorizonsMetamagic::destroyShots(authority, *battle(), *second);
	EXPECT_EQ(second->acquireState()->shots.available(), std::max(0, before - 3));
	const auto focus = SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL));
	for(const auto & record : newHorizonsFocusMagic::enchantment(*second, focus))
		second->addNewBonus(std::make_shared<Bonus>(record));
	ASSERT_TRUE(newHorizonsFocusMagic::active(*second));
	ASSERT_FALSE(newHorizonsFocusMagic::converted(*second));
	newHorizonsMetamagic::destroyShots(authority, *battle(), *second);
	EXPECT_EQ(second->acquireState()->shots.available(), std::max(0, before - 6));
	EXPECT_EQ(newHorizonsFocusMagic::charges(*second), 0);
	for(const auto & record : newHorizonsFocusMagic::enchantment(*third, focus))
		third->addNewBonus(std::make_shared<Bonus>(record));
	ASSERT_TRUE(newHorizonsFocusMagic::converted(*third));
	newHorizonsMetamagic::destroyShots(authority, *battle(), *third);
	EXPECT_EQ(newHorizonsFocusMagic::charges(*third), 0);
	EXPECT_FALSE(newHorizonsFocusMagic::converted(*third));
	EXPECT_TRUE(server.casts.empty());
}
TEST_F(MetamagicV2, RetryOnResistedSlowUsesOneAdditionalNormalCheckAndOneCompletedCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(learn("conservedFormula"));
	bonus(*primary, BonusType::MAGIC_RESISTANCE, 50);
	attackerSideHero->addSpellToSpellbook(SpellID::SLOW);
	auto event = cast(SpellID::SLOW);
	auto mechanics = event.getSpell()->battleMechanics(&event);
	ASSERT_EQ(newHorizonsMetamagic::debuffRetryChance(*mechanics), 60);
	const auto eagerDraws = battle()->battleGetAllUnits(false).size();
	size_t draws = 0;
	ON_CALL(random, nextInt(testing::_, testing::_)).WillByDefault([&](int, int)
	{
		return draws++ <= eagerDraws ? 0 : 99; // First resist, retry chance succeeds, normal retry lands.
	});
	const auto mana = attackerSideHero->getManaAvailable();
	const spells::Target aim{spells::Destination(primary)};
	const auto cost = mechanics->getTargetAwareManaCost(aim);
	const int round = battle()->getRound();
	const auto action = battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(round);
	mechanics->cast(&authority, {spells::Destination(primary)});
	EXPECT_EQ(draws, eagerDraws + 2);
	EXPECT_EQ(server.casts.size(), 1u);
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), cost); // Successful retry cannot refund.
	const auto afterAction = battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(round);
	EXPECT_EQ(action.heroActions, afterAction.heroActions + 1);

	ASSERT_TRUE(mechanics->getResolvedHeroRecipients().size() > 0);
	EXPECT_EQ(primary->valOfBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SLOW)))
		.And(Selector::type()(BonusType::STACKS_INITIATIVE))) < 0, true);
	const auto again = draws;
	EXPECT_FALSE(mechanics->wouldResist(primary));
	EXPECT_EQ(draws, again); // Repeated consumers never retry the same cast twice.
}
TEST_F(MetamagicV2, CoreFriendlyDamageReductionKeepsEnemyDamageAndSculptingReplacesIt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto event = cast(SpellID::ARMAGEDDON);
	auto mechanics = event.getSpell()->battleMechanics(&event);
	const battle::Unit * ally = nullptr;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(battle()->battleGetOwner(unit) == PlayerColor(0)) { ally = unit; break; }
	ASSERT_NE(ally, nullptr);
	EXPECT_EQ(newHorizonsMetamagic::recipientDamagePercent(*mechanics, ally), 40);
	EXPECT_EQ(newHorizonsMetamagic::recipientDamagePercent(*mechanics, primary), 100);
	ASSERT_NO_FATAL_FAILURE(learn("conservedFormula", "spellSculpting"));
	auto sculpted = event.getSpell()->battleMechanics(&event);
	EXPECT_EQ(newHorizonsMetamagic::recipientDamagePercent(*sculpted, ally), 0);
	EXPECT_EQ(newHorizonsMetamagic::recipientDamagePercent(*sculpted, primary), 100);
}
TEST_F(MetamagicV2, DisruptedStackLosesCartExemptionWhileOtherStacksKeepUnlimitedSupply)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * cart = addStack(BattleSide::DEFENDER, CreatureID::AMMO_CART, BattleHex(14, 1), 1);
	ASSERT_NE(cart, nullptr);
	for(auto * target : {second, third})
	{
		bonus(*target, BonusType::SHOOTER, 1);
		bonus(*target, BonusType::SHOTS, 10);
		ASSERT_FALSE(target->acquireState()->shots.isLimited());
	}
	const auto before = second->acquireState()->shots.available();
	newHorizonsMetamagic::destroyShots(authority, *battle(), *second);
	EXPECT_TRUE(second->acquireState()->shots.isLimited());
	EXPECT_FALSE(third->acquireState()->shots.isLimited());
	EXPECT_TRUE(cart->alive());
	EXPECT_EQ(second->acquireState()->shots.available(), before - 3);
	newHorizonsMetamagic::destroyShots(authority, *battle(), *second);
	EXPECT_EQ(second->acquireState()->shots.available(), before - 6);
	EXPECT_TRUE(server.casts.empty());
}
TEST_F(MetamagicV2, RemovingEnchantmentsRequiresTheOptionalUnravelingPerk)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto haste = SpellID(SpellID::HASTE);
	auto effect = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_INITIATIVE,
		BonusSource::SPELL_EFFECT, 1, BonusSourceID(haste));
	effect->turnsRemain = 3;
	effect->spellCasterOwner = PlayerColor(1);
	primary->addNewBonus(effect);
	auto event = cast(); event.setEffectValue(1);
	const spells::Target aim{spells::Destination(primary)};
	auto ordinary = event.getSpell()->battleMechanics(&event);
	ordinary->cast(&authority, aim);
	EXPECT_TRUE(primary->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(haste))));
	battle()->nextRound();
	ASSERT_NO_FATAL_FAILURE(learn("unravelingMagic"));
	ON_CALL(random, nextInt(testing::_, testing::_)).WillByDefault(testing::Return(0));
	auto unraveling = event.getSpell()->battleMechanics(&event);
	unraveling->cast(&authority, aim);
	EXPECT_FALSE(primary->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(haste))));
	EXPECT_EQ(server.casts.size(), 2u); // Exactly the two originals, no discharge cast.
}
TEST_F(MetamagicV2, DisabledCoreQueriesDoNotReadmitRulesOrResolveSpellRegistries)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")), 0, ChangeValueMode::ABSOLUTE);
	auto event = cast();
	auto mechanics = event.getSpell()->battleMechanics(&event);
	const auto energy = battle()->getCapturedMagicRules()->metamagicEnergy();
	ASSERT_NE(energy, nullptr);
	const auto resolutions = energy->sourceResolutionCount();
	nativeRules::executionAudit::Counters counters;
	{
		nativeRules::executionAudit::Scope audit(counters);
		for(int query = 0; query < 1000; ++query)
		{
			EXPECT_EQ(newHorizonsMetamagic::debuffRetryChance(*mechanics), 0);
			EXPECT_EQ(newHorizonsMetamagic::friendlyDamagePercent(*mechanics), 100);
			EXPECT_FALSE(newHorizonsMetamagic::empoweredDamage(*mechanics));
		}
	}
	EXPECT_EQ(energy->sourceResolutionCount(), resolutions);
	EXPECT_EQ(counters, nativeRules::executionAudit::Counters{});
}
TEST_F(MetamagicV2, ForceLancePrimaryBypassesAbsorptionAndSpillUsesTheSecondaryShield)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const consecratedArms::Policy policy{3, 0};
	for(auto * target : {primary, second})
	{
		for(const auto & bonus : consecratedArms::prayerBonuses(consecratedArms::Prayer::VICARIOUS_REDEMPTION, 200, policy, 3, 1))
			target->addNewBonus(std::make_shared<Bonus>(bonus));
		auto state = target->acquireState();
		state->gainRedemptionShield(10000, 5000, false);
		nativeEvents::changeUnit({authority, *battle(), nullptr, nullptr, JsonNode(), {}}, *state);
	}
	ASSERT_GT(primary->getRedemptionShieldHitPoints(), 0);
	const auto shield2 = second->getRedemptionShieldHitPoints();
	ASSERT_GT(shield2, 0);
	auto event = cast(SpellID(SpellID::decode("new-horizons:forceLance")));
	event.setEffectPower(100);
	auto mechanics = event.getSpell()->battleMechanics(&event);
	const auto raw = newHorizonsSorcerySpells::forceLanceDamage(*mechanics);
	const auto preview = newHorizonsMetamagic::previewOverflow(*mechanics, {spells::Destination(primary)});
	ASSERT_TRUE(preview);
	EXPECT_EQ(preview->postShieldPrimaryDamage, raw);
	const auto before2 = second->getAvailableHealth();
	mechanics->cast(&authority, {spells::Destination(primary)});
	EXPECT_FALSE(primary->alive());
	ASSERT_TRUE(mechanics->metamagicOverflowReceipt());
	EXPECT_EQ(mechanics->metamagicOverflowReceipt()->postShieldPrimaryDamage, raw);
	EXPECT_EQ(before2 - second->getAvailableHealth(), preview->damageIfLands);
	EXPECT_EQ(second->getRedemptionShieldHitPoints(), std::max<int64_t>(0, shield2 - preview->defendedSecondaryDamage));
}
TEST_F(MetamagicV2, ForceLanceRemainsSubjectToTheVowCasualtyCeiling)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const consecratedArms::Policy policy{3, 0};
	for(const auto & bonus : consecratedArms::prayerBonuses(consecratedArms::Prayer::UNBROKEN_VOW, 200, policy, 3, 1))
		primary->addNewBonus(std::make_shared<Bonus>(bonus));
	const auto count = primary->getCount();
	const auto ceiling = consecratedArms::prayerValues(*primary, consecratedArms::Prayer::UNBROKEN_VOW).casualtyCeilingBP;
	const auto allowed = (static_cast<int64_t>(count) * ceiling + 9999) / 10000;
	auto event = cast(SpellID(SpellID::decode("new-horizons:forceLance")));
	event.setEffectPower(100);
	auto mechanics = event.getSpell()->battleMechanics(&event);
	mechanics->cast(&authority, {spells::Destination(primary)});
	EXPECT_TRUE(primary->alive());
	EXPECT_LE(count - primary->getCount(), allowed);
	EXPECT_FALSE(mechanics->metamagicOverflowReceipt());
}
TEST_F(MetamagicV2, FocusEnhancementLossIsCappedByTheActualNativeShotsDestroyed)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto focus = SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL));
	for(auto * unit : {second, third})
	{
		bonus(*unit, BonusType::SHOOTER, 1);
		bonus(*unit, BonusType::SHOTS, 1);
		for(const auto & record : newHorizonsFocusMagic::enchantment(*unit, focus))
			unit->addNewBonus(std::make_shared<Bonus>(record));
	}
	auto empty = third->acquireState();
	empty->shots.use(empty->shots.available());
	nativeEvents::changeUnit({authority, *battle(), nullptr, nullptr, JsonNode(), {}}, *empty);
	ASSERT_EQ(third->acquireState()->shots.available(), 0);
	ASSERT_EQ(newHorizonsFocusMagic::charges(*third), 3);
	newHorizonsMetamagic::destroyShots(authority, *battle(), *third);
	EXPECT_EQ(newHorizonsFocusMagic::charges(*third), 3);
	const int before = second->acquireState()->shots.available();
	ASSERT_EQ(before, 1);
	newHorizonsMetamagic::destroyShots(authority, *battle(), *second);
	EXPECT_EQ(second->acquireState()->shots.available(), 0);
	EXPECT_EQ(newHorizonsFocusMagic::charges(*second), 2);
	EXPECT_TRUE(newHorizonsFocusMagic::active(*second));
}
TEST_F(MetamagicV2, ConservedFormulaRefundsOnlyAfterFinalResistanceAndKeepsTheHeroActionSpent)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(learn("conservedFormula"));
	bonus(*primary, BonusType::MAGIC_RESISTANCE, 50);
	attackerSideHero->addSpellToSpellbook(SpellID::SLOW);
	auto event = cast(SpellID::SLOW);
	auto mechanics = event.getSpell()->battleMechanics(&event);
	const auto eagerDraws = battle()->battleGetAllUnits(false).size();
	size_t draws = 0;
	ON_CALL(random, nextInt(testing::_, testing::_)).WillByDefault([&](int, int)
		{ return draws++ < eagerDraws ? 0 : 99; }); // First resisted; free retry opportunity fails.
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_LE(mana, attackerSideHero->manaLimit());
	const auto cost = mechanics->getTargetAwareManaCost({spells::Destination(primary)});
	ASSERT_GT(cost, 0);
	const int round = battle()->getRound();
	const auto before = battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(round);
	mechanics->cast(&authority, {spells::Destination(primary)});
	EXPECT_EQ(draws, eagerDraws + 1);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	ASSERT_EQ(server.casts.size(), 1u);
	EXPECT_EQ(server.casts.front().announcement.paidHeroManaCost, cost);
	const auto after = battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(round);
	EXPECT_EQ(before.heroActions, after.heroActions + 1);
	EXPECT_EQ(before.spellActions, after.spellActions);
	EXPECT_EQ(before.orderActions, after.orderActions);
	EXPECT_TRUE(mechanics->getResolvedHeroRecipients().empty());
	const auto finalDraws = draws;
	EXPECT_TRUE(mechanics->wouldResist(primary));
	EXPECT_EQ(draws, finalDraws);
}
TEST_F(MetamagicV2, CreatureBindAndIndifferentStoneGazeAddNoResistanceDrawWithoutUnshakable)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const battle::Unit * casterUnit = nullptr;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(battle()->battleGetOwner(unit) == PlayerColor(0)) { casterUnit = unit; break; }
	ASSERT_NE(casterUnit, nullptr);
	spells::AbilityCaster caster(casterUnit, 0);
	EXPECT_CALL(random, nextInt(testing::_, testing::_)).Times(0);
	for(const auto [id, target] : {std::pair{SpellID::BIND, second}, std::pair{SpellID::STONE_GAZE, third}})
	{
		const auto spell = SpellID(id);
		spells::BattleCast event(battle(), &caster, spells::Mode::PASSIVE, spell.toSpell());
		event.setSpellLevel(0); event.setEffectDuration(2);
		const auto descriptor = creatureAbility::spellEffect(spell, creatureAbility::Family::CREATURE_ABILITY,
			creatureAbility::HOSTILE_AFFLICTION, creatureAbility::PREVENTABLE | creatureAbility::REMOVABLE, PlayerColor(0));
		event.setCreatureAbilityDescriptor(descriptor);
		auto mechanics = spell.toSpell()->battleMechanics(&event);
		mechanics->cast(&authority, {spells::Destination(target)});
		const auto effects = target->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
		ASSERT_FALSE(effects->empty());
		for(const auto & effect : *effects) ASSERT_EQ(effect->abilityDescriptor, descriptor);
	}
}
TEST_F(MetamagicV2, UnshakableUsesOneCheckForTheWholeStoneGazeAffliction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const consecratedArms::Policy policy{3, static_cast<uint16_t>(consecratedArms::Perk::UNSHAKABLE_FAITH)};
	for(const auto & bonus : consecratedArms::prayerBonuses(consecratedArms::Prayer::STEADFAST_SOUL, 200, policy, 3, 1))
		primary->addNewBonus(std::make_shared<Bonus>(bonus));
	const battle::Unit * casterUnit = nullptr;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(battle()->battleGetOwner(unit) == PlayerColor(0)) { casterUnit = unit; break; }
	ASSERT_NE(casterUnit, nullptr);
	spells::AbilityCaster caster(casterUnit, 0);
	const auto spell = SpellID(SpellID::STONE_GAZE);
	spells::BattleCast event(battle(), &caster, spells::Mode::PASSIVE, spell.toSpell());
	event.setSpellLevel(0); event.setEffectDuration(2);
	event.setCreatureAbilityDescriptor(creatureAbility::spellEffect(spell, creatureAbility::Family::CREATURE_ABILITY,
		creatureAbility::HOSTILE_AFFLICTION, creatureAbility::PREVENTABLE | creatureAbility::REMOVABLE, PlayerColor(0)));
	auto mechanics = spell.toSpell()->battleMechanics(&event);
	ASSERT_GT(newHorizonsMetamagic::normalResistanceBasisPoints(*mechanics, primary), 0);
	EXPECT_CALL(random, nextInt(0, 99)).Times(1).WillOnce(testing::Return(0));
	mechanics->cast(&authority, {spells::Destination(primary)});
	EXPECT_FALSE(primary->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))));
	EXPECT_EQ(server.casts.size(), 1u);
}
TEST_F(MetamagicV2, UnshakableCannotResistForceLanceOrItsFixedPin)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const consecratedArms::Policy policy{3, static_cast<uint16_t>(consecratedArms::Perk::UNSHAKABLE_FAITH)};
	for(const auto & bonus : consecratedArms::prayerBonuses(consecratedArms::Prayer::STEADFAST_SOUL, 200, policy, 3, 1))
		primary->addNewBonus(std::make_shared<Bonus>(bonus));
	auto event = cast(SpellID(SpellID::decode("new-horizons:forceLance")));
	event.setEffectValue(1);
	auto mechanics = event.getSpell()->battleMechanics(&event);
	EXPECT_CALL(random, nextInt(testing::_, testing::_)).Times(0);
	mechanics->cast(&authority, {spells::Destination(primary)});
	const auto pin = primary->getBonus(Selector::type()(BonusType::BIND_EFFECT));
	ASSERT_NE(pin, nullptr);
	ASSERT_NE(pin->abilityDescriptor, nullptr);
	EXPECT_EQ(pin->abilityDescriptor->family, creatureAbility::Family::HERO_SPELL);
	EXPECT_TRUE(pin->abilityDescriptor->interactions & creatureAbility::IRRESISTIBLE);
	EXPECT_EQ(pin->abilityDescriptor->sourceOwner, PlayerColor(0));
	EXPECT_EQ(primary->getAvailableHealth(), 99);
}
TEST_F(MetamagicV2, PhysicalCreatureAfflictionDoesNotAcquireOrdinaryMagicResistance)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	bonus(*primary, BonusType::MAGIC_RESISTANCE, 50);
	const consecratedArms::Policy policy{3, static_cast<uint16_t>(consecratedArms::Perk::UNSHAKABLE_FAITH)};
	for(const auto & bonus : consecratedArms::prayerBonuses(consecratedArms::Prayer::STEADFAST_SOUL, 250, policy, 3, 1))
		primary->addNewBonus(std::make_shared<Bonus>(bonus));
	const battle::Unit * casterUnit = nullptr;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(battle()->battleGetOwner(unit) == PlayerColor(0)) { casterUnit = unit; break; }
	ASSERT_NE(casterUnit, nullptr);
	spells::AbilityCaster caster(casterUnit, 0);
	const auto bind = SpellID(SpellID::BIND);
	spells::BattleCast event(battle(), &caster, spells::Mode::PASSIVE, bind.toSpell());
	event.setSpellLevel(0); event.setEffectDuration(2);
	event.setCreatureAbilityDescriptor(creatureAbility::spellEffect(bind, creatureAbility::Family::CREATURE_ABILITY,
		creatureAbility::HOSTILE_AFFLICTION, creatureAbility::PREVENTABLE | creatureAbility::REMOVABLE, PlayerColor(0)));
	auto mechanics = bind.toSpell()->battleMechanics(&event);
	EXPECT_EQ(newHorizonsMetamagic::normalResistanceBasisPoints(*mechanics, primary), 2250);
	EXPECT_CALL(random, nextInt(0, 9999)).Times(1).WillOnce(testing::Return(2300));
	mechanics->cast(&authority, {spells::Destination(primary)});
	EXPECT_TRUE(primary->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(bind))));
	const auto curse = SpellID(SpellID::CURSE);
	spells::BattleCast normalEvent(battle(), &caster, spells::Mode::PASSIVE, curse.toSpell());
	normalEvent.setCreatureAbilityDescriptor(creatureAbility::spellEffect(curse, creatureAbility::Family::CREATURE_ABILITY,
		creatureAbility::HOSTILE_AFFLICTION,
		creatureAbility::PREVENTABLE | creatureAbility::NORMAL_RESISTIBLE | creatureAbility::REMOVABLE, PlayerColor(0)));
	auto normal = curse.toSpell()->battleMechanics(&normalEvent);
	EXPECT_EQ(newHorizonsMetamagic::normalResistanceBasisPoints(*normal, primary), 7250);
}
TEST_F(MetamagicV2, CapturedFriendlyHazardSafetyUsesItsOwningSideAndSurvivesRankChanges)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(6, 3), 100);
	ASSERT_NE(ally, nullptr);
	auto source = cast(SpellID::FIRE_WALL);
	auto sourceMechanics = source.getSpell()->battleMechanics(&source);
	SpellCreatedObstacle obstacle;
	obstacle.ID = SpellID::FIRE_WALL;
	obstacle.trigger = SpellID(SpellID::decode("core:fireWallTrigger"));
	obstacle.casterSide = BattleSide::ATTACKER;
	obstacle.damageSnapshot = true; obstacle.minimalDamage = 1000; obstacle.spellLevel = 3;
	obstacle.capturedMdrPenetration = sourceMechanics->getCapturedMdrPenetration();
	ASSERT_EQ(obstacle.capturedMdrPenetration["metamagicFriendlyDamagePercent"].Integer(), 40);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")), 0, ChangeValueMode::ABSOLUTE);
	spells::ObstacleCasterProxy ownProxy(PlayerColor(0), attackerSideHero, obstacle);
	spells::BattleCast own(battle(), &ownProxy, spells::Mode::PASSIVE, obstacle.trigger.toSpell());
	auto ownMechanics = own.getSpell()->battleMechanics(&own);
	EXPECT_EQ(newHorizonsMetamagic::recipientDamagePercent(*ownMechanics, ally), 40);
	EXPECT_EQ(newHorizonsMetamagic::recipientDamagePercent(*ownMechanics, primary), 100);
	EXPECT_EQ(ownMechanics->adjustEffectValue(ally), 400);
	EXPECT_EQ(ownMechanics->adjustEffectValue(primary), 1000);
	auto enemyObstacle = obstacle; enemyObstacle.casterSide = BattleSide::DEFENDER;
	spells::ObstacleCasterProxy enemyProxy(PlayerColor(1), defenderSideHero, enemyObstacle);
	spells::BattleCast enemy(battle(), &enemyProxy, spells::Mode::PASSIVE, enemyObstacle.trigger.toSpell());
	auto enemyMechanics = enemy.getSpell()->battleMechanics(&enemy);
	EXPECT_EQ(newHorizonsMetamagic::recipientDamagePercent(*enemyMechanics, ally), 100);
	EXPECT_EQ(enemyMechanics->adjustEffectValue(ally), 1000);
}
TEST_F(MetamagicV2, SculptingCoversSingleAreaChainGlobalAndCapturedHazardDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(learn("conservedFormula", "spellSculpting"));
	const auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(6, 3), 100);
	ASSERT_NE(ally, nullptr);
	for(const auto id : {SpellID::MAGIC_ARROW, SpellID::FIREBALL, SpellID::CHAIN_LIGHTNING, SpellID::ARMAGEDDON})
	{
		auto event = cast(id);
		auto mechanics = event.getSpell()->battleMechanics(&event);
		EXPECT_EQ(newHorizonsMetamagic::recipientDamagePercent(*mechanics, ally), 0);
		EXPECT_EQ(newHorizonsMetamagic::recipientDamagePercent(*mechanics, primary), 100);
		EXPECT_EQ(mechanics->adjustEffectValue(ally), 0);
	}
	auto source = cast(SpellID::FIRE_WALL);
	auto sourceMechanics = source.getSpell()->battleMechanics(&source);
	SpellCreatedObstacle obstacle;
	obstacle.ID = SpellID::FIRE_WALL;
	obstacle.trigger = SpellID(SpellID::decode("core:fireWallTrigger"));
	obstacle.casterSide = BattleSide::ATTACKER;
	obstacle.damageSnapshot = true; obstacle.minimalDamage = 1000; obstacle.spellLevel = 3;
	obstacle.capturedMdrPenetration = sourceMechanics->getCapturedMdrPenetration();
	spells::ObstacleCasterProxy proxy(PlayerColor(0), attackerSideHero, obstacle);
	spells::BattleCast delayed(battle(), &proxy, spells::Mode::PASSIVE, obstacle.trigger.toSpell());
	auto mechanics = delayed.getSpell()->battleMechanics(&delayed);
	EXPECT_EQ(mechanics->adjustEffectValue(ally), 0);
	EXPECT_EQ(mechanics->adjustEffectValue(primary), 1000);
}
TEST_F(MetamagicV2, AutomaticHeroDamageKeepsFriendlySafetyWithoutASecondOverflowOpportunity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto event = cast(SpellID::ARMAGEDDON);
	spells::AutomaticHeroCastReceipt automatic;
	automatic.kind = spells::AutomaticHeroCastKind::DIVINE_FURY;
	event.setAutomaticHeroCast(automatic);
	auto mechanics = event.getSpell()->battleMechanics(&event);
	EXPECT_EQ(newHorizonsMetamagic::friendlyDamagePercent(*mechanics), 40);
	EXPECT_FALSE(newHorizonsMetamagic::previewOverflow(*mechanics, {spells::Destination(primary)}));
	auto arrow = cast();
	auto ordinary = arrow.getSpell()->battleMechanics(&arrow);
	ASSERT_TRUE(newHorizonsMetamagic::previewOverflow(*ordinary, {spells::Destination(primary)}));
	arrow.setAutomaticHeroCast(automatic);
	auto automaticArrow = arrow.getSpell()->battleMechanics(&arrow);
	EXPECT_FALSE(newHorizonsMetamagic::previewOverflow(*automaticArrow, {spells::Destination(primary)}));
}
