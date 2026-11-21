/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../AI/BattleAI/DoubleCastFollowUp.h"
#include "../../AI/BattleAI/ForecastPerkPolicy.h"
#include "../../AI/BattleAI/PossibleSpellcast.h"
#include "../../lib/battle/MetamagicDoubleCast.h"
#include "../../lib/spells/CSpell.h"

namespace heroQueueTest
{
namespace
{
class MetamagicDoubleCastAITest : public HeroQueueSelectorTest
{
protected:
	void prepare(int metamagicRank, bool woundedActor = true)
	{
		useRevisedOrders = true;
		ASSERT_NO_FATAL_FAILURE(world()); // Books Cure.
		attackerSideHero->addSpellToSpellbook(SpellID::BLESS);
		if(metamagicRank)
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")),
				metamagicRank, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(arena());
		// A frozen, badly wounded Angel makes Cure a follow-up the AI values.
		if(!woundedActor)
			return; // Nothing for Cure to heal or cleanse.
		ASSERT_NO_FATAL_FAILURE(injure(actor, actor->getAvailableHealth() - 1000));
		ASSERT_NO_FATAL_FAILURE(freeze(actor));
	}
	void learn(const char * perk)
	{
		const auto id = std::string("new-horizons:metamagic.") + perk;
		attackerSideHero->applyPerkSelection({"new-horizons:metamagic", id});
		ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:metamagic", id)) << perk;
	}
	bool offerOpen() const
	{
		return metamagicDoubleCast::offerGranted(battle()->getHeroActionAllowances(BattleSide::ATTACKER));
	}
	/// Board and cache built exactly as the selector fixture does.
	float completion(float valuePerMana)
	{
		callback = std::make_shared<QueueCallback>();
		callback->onBattleStarted(battle());
		live = callback->getBattle(BattleID(0));
		environment = std::make_shared<QueueEnvironment>(gameState());
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		DamageCache cache;
		cache.buildDamageCache(parent, BattleSide::ATTACKER);
		return doubleCastAI::completionValue(*live, parent, BattleSide::ATTACKER, *attackerSideHero, cache, valuePerMana, 1.0f);
	}
};
}

static bool submittedSpell(const QueueCallback & callback)
{
	return std::any_of(callback.heroRequests.begin(), callback.heroRequests.end(), [](const BattleAction & action)
	{
		return action.actionType == EActionType::HERO_SPELL;
	});
}

TEST_F(MetamagicDoubleCastAITest, CastsItsHeroActionSpellWhenNoOfferIsOpen)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	ASSERT_NO_FATAL_FAILURE(characterize(true));
	EXPECT_TRUE(submittedSpell(*callback)) << "Control: Cure is a valued Hero Action spell";
}

TEST_F(MetamagicDoubleCastAITest, CastsAValuedDifferentFollowUpThroughTheOffer)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	ASSERT_TRUE(cast(attackerSideHero, SpellID::BLESS, active));
	ASSERT_TRUE(offerOpen());
	ASSERT_EQ(battle()->battleActiveUnit(), active);
	ASSERT_NO_FATAL_FAILURE(characterize(true));
	ASSERT_EQ(callback->heroRequests.size(), 1u);
	const auto request = callback->heroRequests.front();
	ASSERT_EQ(request.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(request.spell, SpellID(SpellID::CURE)) << "The wounded Angel makes Cure the valued follow-up";
	// The authority accepts it as the double cast: one use spent, offer closed.
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), request));
	EXPECT_FALSE(offerOpen());
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicDoubleCast.usesSpent, 1);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicDoubleCast.completedDoubleCasts, 1);
}

TEST_F(MetamagicDoubleCastAITest, DeclinesAUselessFollowUpForFree)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1, false)); // Nobody is wounded or frozen: Cure has nothing to do.
	ASSERT_TRUE(cast(attackerSideHero, SpellID::BLESS, active));
	ASSERT_TRUE(offerOpen());
	ASSERT_NO_FATAL_FAILURE(characterize(false));
	EXPECT_FALSE(submittedSpell(*callback)) << "A follow-up worth less than declining is not cast";
	// Declining is free: the creature action that follows forfeits the offer without spending a use.
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(active)));
	EXPECT_FALSE(offerOpen());
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicDoubleCast.usesSpent, 0);
}

TEST_F(MetamagicDoubleCastAITest, FollowUpFilterRefusesOnlyTheOfferPaidRepeat)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	{
		const doubleCastAI::FollowUpFilter closed(*battle(), BattleSide::ATTACKER, attackerSideHero);
		EXPECT_FALSE(closed.offerOpen());
		EXPECT_FALSE(closed.refuses(SpellID::BLESS));
		EXPECT_FALSE(closed.paidByOffer(SpellID::CURE));
	}
	ASSERT_TRUE(cast(attackerSideHero, SpellID::BLESS, active));
	const doubleCastAI::FollowUpFilter filter(*battle(), BattleSide::ATTACKER, attackerSideHero);
	ASSERT_TRUE(filter.offerOpen());
	EXPECT_TRUE(filter.refuses(SpellID::BLESS));
	EXPECT_FALSE(filter.refuses(SpellID::CURE));
	EXPECT_TRUE(filter.paidByOffer(SpellID::CURE));
	// Few follow-ups: nothing is dropped by the candidate cap.
	EXPECT_TRUE(filter.overCap({SpellID(SpellID::CURE).toSpell(), SpellID(SpellID::BLESS).toSpell()}).empty());
}

TEST_F(MetamagicDoubleCastAITest, FollowUpCapKeepsTheHighestLevelSpellsDeterministically)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	ASSERT_TRUE(cast(attackerSideHero, SpellID::BLESS, active));
	const doubleCastAI::FollowUpFilter filter(*battle(), BattleSide::ATTACKER, attackerSideHero);
	ASSERT_TRUE(filter.offerOpen());
	std::vector<const CSpell *> roster;
	for(int id = 0; id < 70 && roster.size() < doubleCastAI::MAX_FOLLOW_UP_SPELLS + 4; ++id)
		if(const auto * spell = SpellID(id).toSpell(); spell && spell->isCombat() && filter.paidByOffer(spell->getId()))
			roster.push_back(spell);
	ASSERT_GT(roster.size(), doubleCastAI::MAX_FOLLOW_UP_SPELLS);
	const auto dropped = filter.overCap(roster);
	EXPECT_EQ(dropped.size(), roster.size() - doubleCastAI::MAX_FOLLOW_UP_SPELLS);
	EXPECT_EQ(dropped, filter.overCap(roster));
	int lowestKept = 99;
	for(const auto * spell : roster)
		if(std::ranges::find(dropped, spell->getId()) == dropped.end())
			lowestKept = std::min(lowestKept, static_cast<int>(spell->getLevel()));
	for(const auto id : dropped)
		EXPECT_LE(id.toSpell()->getLevel(), lowestKept);
}

TEST_F(MetamagicDoubleCastAITest, ArcaneWardAddsCompletionValueOnlyWhenOwned)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	ASSERT_TRUE(cast(attackerSideHero, SpellID::BLESS, active));
	ASSERT_TRUE(offerOpen());
	EXPECT_EQ(completion(1.0f), 0.0f) << "No completion perk, no completion value";
	ASSERT_NO_FATAL_FAILURE(learn("arcaneWard"));
	EXPECT_GT(completion(0.0f), 0.0f) << "The enemy can still strike a warded stack this round";
	EXPECT_TRUE(forecast::ordinaryPerksSupported(*attackerSideHero)) << "Double Cast perks keep forecasts admitted";
}

TEST_F(MetamagicDoubleCastAITest, FormulaReserveManaIsPricedAtTheBestCandidateValuePerMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	ASSERT_TRUE(cast(attackerSideHero, SpellID::BLESS, active));
	ASSERT_NO_FATAL_FAILURE(learn("formulaRecovery"));
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 0); // Room under the cap for the restored Mana.
	ASSERT_GT(attackerSideHero->manaLimit(), 0);
	EXPECT_EQ(completion(0.0f), 0.0f) << "Mana is worthless when no candidate gains from spending it";
	const auto cheap = completion(1.0f);
	EXPECT_GT(cheap, 0.0f);
	EXPECT_FLOAT_EQ(completion(2.0f), 2 * cheap);
}

TEST_F(MetamagicDoubleCastAITest, CompletionValueIsAddedOnlyToOfferPaidCandidates)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	ASSERT_TRUE(cast(attackerSideHero, SpellID::BLESS, active));
	ASSERT_NO_FATAL_FAILURE(learn("arcaneWard"));
	ASSERT_GT(completion(0.0f), 0.0f);
	std::vector<PossibleSpellcast> candidates(3);
	candidates[0].spell = SpellID(SpellID::CURE).toSpell();
	candidates[0].value = 10.0f;
	candidates[1].command = HeroCommand::CHARGE;
	candidates[1].value = 10.0f;
	candidates[2].spell = SpellID(SpellID::CURE).toSpell();
	candidates[2].value = std::numeric_limits<float>::lowest(); // Not evaluated: never revived.
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::ATTACKER);
	doubleCastAI::addCompletionValue(candidates, *live, parent, BattleSide::ATTACKER, 0.0f, cache, 1.0f);
	EXPECT_GT(candidates[0].value, 10.0f);
	EXPECT_EQ(candidates[1].value, 10.0f);
	EXPECT_EQ(candidates[2].value, std::numeric_limits<float>::lowest());
}
}
