/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ForecastNativeAdapterFixture.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/battle/OwnedUnitCreatureResolver.h"
#include "../../lib/battle/NewHorizonsArmorer.h"
namespace
{
using namespace bonusDependencies;
class ForecastModifierOriginBindingTest : public ForecastNativeAdapterTest
{
protected:
	static QueryFrameBinding bind(const StackWithBonuses & unit)
	{
		battle::OwnedUnitCreatureResolver::Creature body;
		body.id=unit.creatureId(); body.key="native-defend-origin-fixture";
		QueryFrameBinding result;
		result.recipientId=unit.unitId(); result.creature=unit.creatureId(); result.count=unit.getCount();
		result.owner=unit.unitOwner(); result.side=unit.unitSide(); result.slot=unit.unitSlot();
		result.ghost=unit.isGhost(); result.summoned=unit.summoned; result.natureSummoned=unit.natureSummoned;
		result.profileOwner=std::make_shared<const battle::OwnedUnitCreatureResolver>(std::vector{body});
		return result;
	}
	static recipientBonusOverlay::Layer leaf()
	{
		recipientBonusOverlay::Records added,updated;
		std::set<std::shared_ptr<Bonus>> removed;
		std::optional<recipientBonusOverlay::Values> stacked;
		std::optional<recipientBonusOverlay::Records> unstacked;
		recipientBonusOverlay::Groups groups; bool veteran=false;
		return recipientBonusOverlay::Layer({added,updated,removed,stacked,unstacked,groups,veteran});
	}
	static void parity(const OwnedBonusQueryFrame & frame,const battle::Unit & native)
	{
		for(bool lane:{false,true})
		{
			const auto actual=frame.query(Selector::all,lane);
			const auto expected=native.getBonusesBeforeCreatureAbilitySuppression(Selector::all,{},lane);
			ASSERT_EQ(actual->size(),expected->size());
			for(size_t i=0;i<actual->size();++i)
			{
				const auto & a=*(*actual)[i]; const auto & e=*(*expected)[i];
				EXPECT_EQ(a.type,e.type); EXPECT_EQ(a.subtype,e.subtype); EXPECT_EQ(a.val,e.val);
				EXPECT_EQ(a.valType,e.valType); EXPECT_EQ(a.effectRange,e.effectRange);
				EXPECT_EQ(a.source,e.source); EXPECT_EQ(a.sid,e.sid); EXPECT_EQ(a.duration,e.duration);
				EXPECT_EQ(a.turnsRemain,e.turnsRemain); EXPECT_EQ(a.stacking,e.stacking);
			}
		}
	}
};
TEST_F(ForecastModifierOriginBindingTest, HeldNativeDefendSourceRowsSurviveEveryPrefixRebindThenCasualty)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	uint32_t id=0; bool found=false;
	for(const auto * unit:parent->battleGetAllUnits(false))
		if(unit->alive() && unit->unitSide()==BattleSide::ATTACKER) { id=unit->unitId(); found=true; break; }
	ASSERT_TRUE(found);
	const auto ancestor=parent->getForUpdate(id); ASSERT_TRUE(ancestor);
	const auto stance=newHorizonsArmorer::buildDefendStance(ancestor.get());
	ASSERT_FALSE(stance.bonuses.empty());
	for(const auto & row:stance.bonuses) EXPECT_EQ(row.source,BonusSource::OTHER);
	ancestor->updateUnitBonus(stance.bonuses); // SAME native Defend bonus construction/update path.
	const auto raw=ancestor->captureModifierFacts(nullptr); ASSERT_TRUE(raw);
	auto context=bind(*ancestor);
	const auto base=raw->bindQueries(context,ancestor->localSpellEffects()); ASSERT_TRUE(base);
	const auto before=base->refreshQueries(context,Change::None); ASSERT_TRUE(before);
	ASSERT_NO_FATAL_FAILURE(parity(*before,*ancestor));
	const auto sibling=before;
	const auto siblingNative=ancestor->save();
	const auto outside=gameState()->saveToMemory();
	const auto native=std::make_shared<StackWithBonuses>(parent.get(),ancestor.get());
	const auto source=base->bindRecipientLayerSource(before); ASSERT_TRUE(source);
	const auto held=before->getAllBonuses(CSelector(Bonus::UntilGetsTurn)); ASSERT_GE(held->size(),size_t(3));
	auto expired=leaf(); auto after=before;
	std::shared_ptr<const CapturedModifiers> current=base;
	for(const auto & record:*held)
	{
		ASSERT_EQ(record->source,BonusSource::OTHER);
		// Native vector removal selects from the SAME fixed origBearer F0 for
		// each later operation. Force owned source rebind after EVERY prefix.
		native->removeUnitBonus(std::vector<Bonus>{*record});
		expired.removed.insert(record);
		current=base->appendRecipientLayer(expired,native->localSpellEffects(),*after,source.get()); ASSERT_TRUE(current);
		after=current->refreshQueries(context,Change::None); ASSERT_TRUE(after);
		ASSERT_NO_FATAL_FAILURE(parity(*after,*native));
	}
	EXPECT_TRUE(after->getAllBonuses(CSelector(Bonus::UntilGetsTurn))->empty());
	int64_t amount=native->getFirstHPleft(); ASSERT_GT(amount,0);
	native->damage(amount); context.count=native->getCount(); EXPECT_LT(context.count,before->binding().count);
	const auto casualty=current->refreshQueries(context,Change::Count,after); ASSERT_TRUE(casualty);
	ASSERT_NO_FATAL_FAILURE(parity(*casualty,*native));
	EXPECT_FALSE(sibling->getAllBonuses(CSelector(Bonus::UntilGetsTurn))->empty());
	EXPECT_FALSE(ancestor->getAllBonuses(CSelector(Bonus::UntilGetsTurn))->empty());
	EXPECT_EQ(ancestor->save(),siblingNative); EXPECT_EQ(gameState()->saveToMemory(),outside);
}
}
