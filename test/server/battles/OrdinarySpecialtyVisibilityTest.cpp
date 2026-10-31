/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsOrdinaryStrike.h"
#include "../../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../../lib/battle/NewHorizonsRangefinder.h"
#include "../../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/CRandomGenerator.h"
#include <array>
#include <tuple>
#include <string_view>
#if ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/HypotheticBattle.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include <vcmi/Environment.h>
#endif
namespace
{
enum class Kind { DAMAGE, PROTECTION, STRIKE };
struct SpecialtyCase { const char * hero; const char * creature; const char * key; Kind kind; int value = 17; };
constexpr std::array<SpecialtyCase, 12> cases{{
	{"core:dace", "core:minotaur", "retaliationDamagePercent", Kind::DAMAGE},
	{"core:shakti", "core:troglodyte", "massedSpearsDamagePercent", Kind::DAMAGE},
	{"core:fiona", "core:hellHound", "packPursuitDamagePercent", Kind::DAMAGE},
	{"core:vokial", "core:vampire", "bloodScentDamagePercent", Kind::DAMAGE},
	{"core:lacus", "core:waterElemental", "undertowRetaliationPercent", Kind::DAMAGE},
	{"core:broghild", "core:wyvern", "wyvernsPreyDamagePercent", Kind::DAMAGE},
	{"core:gretchin", "core:goblin", "mobPressureDamagePercent", Kind::DAMAGE},
	{"core:tyraxor", "core:goblinWolfRider", "ridersExploitDamagePercent", Kind::DAMAGE},
	{"core:jabarkas", "core:orc", "cruelVolleyDamagePercent", Kind::DAMAGE},
	{"core:korbac", "core:serpentFly", "evasiveFlightReductionPercent", Kind::PROTECTION},
	{"core:aeris", "core:pegasus", "wingbeatInitiativePercent", Kind::STRIKE},
	{"core:theodorus", "core:mage", "measuredVolleyMdrPenetrationPercent", Kind::STRIKE}}};
class OrdinarySpecialtyVisibilityFixture : public HeroCommandFixture
{
protected:
	const SpecialtyCase * selected = nullptr;
	int mode = 0; // positive captured, explicit zero, historical conversion absent
	CStack * actor = nullptr;
	CStack * victim = nullptr;
	void mapLoaded(CMap * loaded) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(mode == 2) rules["additionalSpecialties"].Struct().erase("ordinaryCreatureSpecialties");
		else rules["additionalSpecialties"]["ordinaryCreatureSpecialties"][selected->key].Integer() = mode == 1 ? 0 : selected->value;
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare(const SpecialtyCase & row, int variant = 0)
	{
		selected = &row;
		mode = variant;
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(row.hero)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode(row.creature)), 10}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 1000}});
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
		actor = addStack(BattleSide::ATTACKER, creatureByName(row.creature), BattleHex(3, 5), 10);
		victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(4, 6),
			std::string_view(row.hero) == "core:shakti" ? 2 : 1000);
		const std::string_view hero(row.hero);
		if(hero == "core:fiona" || hero == "core:lacus" || hero == "core:gretchin")
			addStack(BattleSide::ATTACKER, creatureByName(hero == "core:gretchin" ? "core:goblin" : "core:pikeman"), BattleHex(4, 5), 10);
		if(hero == "core:vokial" || hero == "core:jabarkas")
		{
			int64_t injury = hero == "core:vokial" ? 1 : victim->getMaxHealth();
			victim->damage(injury);
		}
		if(hero == "core:tyraxor") victim->counterAttacks.use();
	}
	BattleAttackInfo attack(const battle::Unit * source, const battle::Unit * target) const
	{
		const std::string_view hero(selected->hero);
		BattleAttackInfo result(selected->kind == Kind::PROTECTION ? target : source,
			selected->kind == Kind::PROTECTION ? source : target, 0,
			hero == "core:jabarkas" || hero == "core:theodorus");
		result.retaliation = hero == "core:dace" || hero == "core:lacus" || selected->kind == Kind::PROTECTION;
		return result;
	}
	int contribution(const CBattleInfoCallback & view, const BattleAttackInfo & hit) const
	{
		if(selected->kind == Kind::PROTECTION) return newHorizonsHeroes::ordinaryCreatureProtectionPercent(view, hit);
		if(selected->kind == Kind::DAMAGE) return newHorizonsHeroes::ordinaryCreatureDamageMultiplier(view, hit) - 100;
		const auto effect = newHorizonsHeroes::ordinaryCreatureStrikeBonus(view, hit);
		return effect ? std::abs(effect->val) : 0;
	}
	void expectContribution(const CBattleInfoCallback & view, const battle::Unit * source,
		const battle::Unit * target, bool visible) const
	{
		const auto hit = attack(source, target);
		const int expected = visible && mode == 0 ? selected->value : 0;
		EXPECT_EQ(contribution(view, hit), expected);
		if(selected->kind == Kind::STRIKE)
		{
			EXPECT_EQ(newHorizonsOrdinaryStrike::needsProjection(view, hit), expected > 0);
			const auto plan = newHorizonsOrdinaryStrike::plan(view, hit, 1);
			EXPECT_EQ(plan.add.size(), expected > 0 ? 1u : 0u);
			if(!plan.add.empty())
			{
				EXPECT_EQ(std::abs(plan.add.front().val), expected);
				EXPECT_EQ(plan.target, target->unitId());
			}
		}
	}
	template<class T> static std::vector<std::byte> bytes(T & value)
	{
		CMemorySerializer memory;
		memory.oser & value;
		return memory.extractBuffer();
	}
	auto worldAndRng()
	{
		return std::array{gameState()->saveToMemory(), bytes(*gameHandler->randomizer),
			bytes(dynamic_cast<CRandomGenerator &>(*getRNG())), bytes(CRandomGenerator::getDefault())};
	}
	static Bonus hypnotize()
	{
		return {BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()};
	}
};
class OrdinarySpecialtyVisibilityTest : public OrdinarySpecialtyVisibilityFixture,
	public ::testing::WithParamInterface<std::tuple<int, int>> {};
TEST_P(OrdinarySpecialtyVisibilityTest, RealPlayerKnownHiddenAndAuthorityDoNotReadHiddenCapturedCoefficients)
{
	ASSERT_NO_FATAL_FAILURE(prepare(cases.at(std::get<0>(GetParam())), std::get<1>(GetParam())));
	const auto before = worldAndRng();
	CPlayerBattleCallback known(battle(), PlayerColor(0)), hidden(battle(), PlayerColor(1));
	ASSERT_EQ(hidden.battleGetVisibleHeroPolicy(BattleSide::ATTACKER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	ASSERT_EQ(hidden.battleGetOwnerHero(actor), attackerSideHero); // Real raw API trap is deliberately retained.
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(known, *actor), attackerSideHero);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(hidden, *actor), nullptr);
	expectContribution(known, actor, victim, true);
	expectContribution(hidden, actor, victim, false);
	expectContribution(*battle(), actor, victim, true);
	EXPECT_EQ(worldAndRng(), before);
}
#if ENABLE_BATTLE_AI
class VisibilityEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit VisibilityEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
TEST_P(OrdinarySpecialtyVisibilityTest, ProjectedKnownHiddenAndAllKnowingKeepTheSamePerspectiveBoundary)
{
	ASSERT_NO_FATAL_FAILURE(prepare(cases.at(std::get<0>(GetParam())), std::get<1>(GetParam())));
	const auto before = worldAndRng();
	VisibilityEnvironment environment(gameState());
	for(const auto player : {PlayerColor(0), PlayerColor(1), PlayerColor::SPECTATOR})
	{
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), player);
		auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
		auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
		const auto source = branch->getForUpdate(actor->unitId());
		const auto target = branch->getForUpdate(victim->unitId());
		ASSERT_NE(source, nullptr);
		ASSERT_NE(target, nullptr);
		ASSERT_EQ(branch->battleGetOwnerHero(source.get()), attackerSideHero); // Override is raw on both layers.
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(*branch, *source), player == PlayerColor(1) ? nullptr : attackerSideHero);
		expectContribution(*branch, source.get(), target.get(), player != PlayerColor(1));
	}
	EXPECT_EQ(worldAndRng(), before);
}
#endif
INSTANTIATE_TEST_SUITE_P(TwelveFamiliesCurrentZeroHistorical, OrdinarySpecialtyVisibilityTest,
	::testing::Combine(::testing::Range(0, 12), ::testing::Range(0, 3)));
TEST_F(OrdinarySpecialtyVisibilityFixture, NativeEffectiveOwnerTracksHypnotizeWhilePuppetKeepsOriginalArmy)
{
	ASSERT_NO_FATAL_FAILURE(prepare(cases.front()));
	CPlayerBattleCallback own(battle(), PlayerColor(0)), other(battle(), PlayerColor(1));
	const auto puppet = newHorizonsPuppetMaster::controlMarker(SpellID(SpellID::decode("new-horizons:puppetMaster")), PlayerColor(1));
	actor->addNewBonus(std::make_shared<Bonus>(puppet));
	ASSERT_EQ(own.battleGetActionController(actor), PlayerColor(1));
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(own, *actor), attackerSideHero);
	expectContribution(own, actor, victim, true);
	actor->removeBonuses(Selector::type()(BonusType::PUPPET_MASTER_CONTROL));
	actor->addNewBonus(std::make_shared<Bonus>(hypnotize()));
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(own, *actor), nullptr);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(other, *actor), defenderSideHero);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(*battle(), *actor), defenderSideHero);
	auto * imported = addStack(BattleSide::DEFENDER, creatureByName("core:minotaur"), BattleHex(6, 6), 10);
	imported->addNewBonus(std::make_shared<Bonus>(hypnotize()));
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(own, *imported), attackerSideHero);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(other, *imported), nullptr);
	expectContribution(own, imported, victim, true);
	expectContribution(*battle(), imported, victim, true);
}
#if ENABLE_BATTLE_AI
TEST_F(OrdinarySpecialtyVisibilityFixture, ProjectedOwnerTransferChangesOnlyTheBranchAndRetainsAuthorityRules)
{
	ASSERT_NO_FATAL_FAILURE(prepare(cases.front()));
	auto * imported = addStack(BattleSide::DEFENDER, creatureByName("core:minotaur"), BattleHex(6, 6), 10);
	const auto before = worldAndRng();
	VisibilityEnvironment environment(gameState());
	auto own = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, own);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto source = branch->getForUpdate(actor->unitId());
	const auto foreign = branch->getForUpdate(imported->unitId());
	source->addUnitBonus({newHorizonsPuppetMaster::controlMarker(SpellID(SpellID::decode("new-horizons:puppetMaster")), PlayerColor(1))});
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(*branch, *source), attackerSideHero);
	source->removeUnitBonus(Selector::type()(BonusType::PUPPET_MASTER_CONTROL));
	source->addUnitBonus({hypnotize()});
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(*branch, *source), nullptr);
	foreign->addUnitBonus({hypnotize()});
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(*branch, *foreign), attackerSideHero);
	expectContribution(*branch, foreign.get(), branch->battleGetUnitByID(victim->unitId()), true);
	EXPECT_EQ(worldAndRng(), before);
}
#endif
TEST_F(OrdinarySpecialtyVisibilityFixture, UnavailableCallbackDoesNotInventEntitlementForItsRealUnit)
{
	ASSERT_NO_FATAL_FAILURE(prepare(cases.front()));
	CPlayerBattleCallback absent(nullptr, PlayerColor(0));
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(absent, *actor), nullptr);
	CPlayerBattleCallback own(battle(), PlayerColor(1));
	EXPECT_EQ(own.battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(), VisibleHeroPolicy::Knowledge::VISIBLE_HERO);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(own, *victim), defenderSideHero);
}
TEST_F(OrdinarySpecialtyVisibilityFixture, VisiblePublishedCreatureBonusesRemainReadableWithHiddenHeroPolicy)
{
	constexpr SpecialtyCase valeska{"core:valeska", "core:archer", "closeQuartersDrill", Kind::DAMAGE, 1};
	ASSERT_NO_FATAL_FAILURE(prepare(valeska));
	CPlayerBattleCallback hidden(battle(), PlayerColor(1));
	ASSERT_EQ(newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(hidden, *actor), nullptr);
	EXPECT_TRUE(actor->hasBonusOfType(BonusType::NO_MELEE_PENALTY));
	EXPECT_FALSE(actor->hasBonusOfType(BonusType::FREE_SHOOTING));
}
TEST_F(OrdinarySpecialtyVisibilityFixture, PublishedRangefinderEnvelopeIsNotReclassifiedAsHiddenCapturedRules)
{
	constexpr SpecialtyCase christian{"core:christian", "core:ballista", "rangefinderFullDamageDistance", Kind::DAMAGE, 12};
	ASSERT_NO_FATAL_FAILURE(prepare(christian));
	CPlayerBattleCallback hidden(battle(), PlayerColor(1));
	ASSERT_EQ(newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(hidden, *actor), nullptr);
	EXPECT_EQ(newHorizonsRangefinder::fullDamageDistance(*actor), 12);
}
TEST_F(OrdinarySpecialtyVisibilityFixture, AlreadyPublishedWingbeatStaysObservableWhenItsSourceHeroIsHidden)
{
	ASSERT_NO_FATAL_FAILURE(prepare(cases.at(10)));
	const auto plan = newHorizonsOrdinaryStrike::plan(*battle(), attack(actor, victim), 1);
	ASSERT_EQ(plan.add.size(), 1u);
	victim->addNewBonus(std::make_shared<Bonus>(plan.add.front()));
	CPlayerBattleCallback hidden(battle(), PlayerColor(1));
	ASSERT_EQ(newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(hidden, *actor), nullptr);
	EXPECT_TRUE(victim->hasBonus(CSelector(newHorizonsOrdinaryStrike::isInitiativeEffect)));
	EXPECT_TRUE(newHorizonsOrdinaryStrike::mayChangeInitiative(hidden));
	expectContribution(hidden, actor, victim, false);
}
TEST_F(OrdinarySpecialtyVisibilityFixture, ActualNativeDamagePreviewKeepsAuthorityPremiumButDoesNotReadHiddenDacePolicy)
{
	ASSERT_NO_FATAL_FAILURE(prepare(cases.front()));
	const auto before = worldAndRng();
	CPlayerBattleCallback known(battle(), PlayerColor(0)), hidden(battle(), PlayerColor(1));
	const auto hit = attack(actor, victim);
	const auto authoritative = battle()->calculateDmgRange(hit);
	const auto visible = known.calculateDmgRange(hit);
	const auto concealed = hidden.calculateDmgRange(hit);
	ASSERT_GT(authoritative.damage.min, 0);
	EXPECT_EQ(visible.damage.min, authoritative.damage.min);
	EXPECT_EQ(visible.damage.max, authoritative.damage.max);
	EXPECT_LT(concealed.damage.min, authoritative.damage.min);
	EXPECT_LT(concealed.damage.max, authoritative.damage.max);
	EXPECT_EQ(worldAndRng(), before);
}
}
