/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../OverkillRandomPrayerRules.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/MetamagicDoubleCast.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/nativeRules/CompiledMetamagicRules.h"
#include "../../../lib/spells/CSpell.h"

namespace metamagicDoubleCastTest
{
using Ledger = HeroActionAllowanceState;
constexpr auto ATTACKER = BattleSide::ATTACKER;

class MetamagicDoubleCastTest : public HeroCommandFixture
{
protected:
	bool pinOverkill = false;
	/// Defender Mana for Mana Siphon scenarios; negative keeps the fixture default.
	int64_t defenderMana = -1;
	/// Knowledge for scenarios that need Normal Mana headroom; negative keeps the default.
	int knowledge = -1;
	/// Spell Power for scenarios that scale with it; negative keeps the default.
	int spellPower = -1;

	void SetUp() override
	{
		useRevisedOrders = true;
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		if(pinOverkill)
		{
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, overkillRandomPrayerRules());
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, overkillRandomPrayerHeroes());
		}
	}

	void prepare(int rank, const char * heroType = nullptr)
	{
		startGame();
		if(heroType)
			attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode(heroType)));
		if(rank > 0)
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")),
				rank, ChangeValueMode::ABSOLUTE);
		if(!attackerSideHero->hasSpellbook())
			giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto spell : {SpellID::BLESS, SpellID::CURSE, SpellID::SLOW})
			attackerSideHero->addSpellToSpellbook(spell);
		if(knowledge >= 0)
			attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, knowledge, ChangeValueMode::ABSOLUTE);
		if(spellPower >= 0)
			attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 200);
		if(defenderMana >= 0)
			setTestSpellPointTotal(defenderSideHero, defenderMana);
		startBattle();
		beginCombat();
		ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
	}

	void learn(std::initializer_list<const char *> perks)
	{
		for(const auto * name : perks)
		{
			const auto id = std::string("new-horizons:metamagic.") + name;
			attackerSideHero->applyPerkSelection({"new-horizons:metamagic", id});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:metamagic", id)) << name;
		}
	}

	bool cast(SpellID spell)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = ATTACKER;
		action.spell = spell;
		const battle::Unit * target = battle()->battleActiveUnit();
		if(spell == SpellID(SpellID::CURSE) || spell == SpellID(SpellID::SLOW))
			for(const auto * unit : battle()->battleGetAllStacks(false))
				if(unit->unitSide() == BattleSide::DEFENDER && unit->alive())
					target = unit;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	const MetamagicDoubleCastState & state() const { return battle()->getSide(ATTACKER).metamagicDoubleCast; }
	const Ledger & ledger() const { return battle()->getHeroActionAllowances(ATTACKER); }
	bool offered() const { return metamagicDoubleCast::offerGranted(ledger()); }
};
}
