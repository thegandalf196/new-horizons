/*
 * FactionMember.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once

#include "Entity.h"

#include <utility>

class BonusList;
class PrimarySkill;

class DLL_LINKAGE AFactionMember: public IConstBonusProvider, public INativeTerrainProvider
{
public:
	bool isNativeTerrain(TerrainId terrain) const override;
	/**
	 Returns magic resistance considering some bonuses.
	*/
	virtual int32_t magicResistance() const;
	/**
	 Returns minimal damage of creature or (when implemented) hero.
	*/
	virtual int getMinDamage(bool ranged) const;
	/**
	 Returns maximal damage of creature or (when implemented) hero.
	*/
	virtual int getMaxDamage(bool ranged) const;
	/**
	 Returns attack of creature or hero.
	*/
	virtual int getAttack(bool ranged) const;
	/**
	 Returns defence of creature or hero.
	*/
	virtual int getDefense(bool ranged) const;
	/**
	 Returns morale of creature or hero. Taking absolute bonuses into account.
	 Uses the member's saved rules context when available, otherwise engine settings.
	*/
	int moraleVal() const;
	/// Returns morale after applying an additional flat value before morale caps and minimums.
	int moraleValWithBonus(int32_t additionalMorale) const;
	/**
	 Returns luck of creature or hero. Taking absolute bonuses into account.
	 For now, uses range from EGameSettings
	*/
	int luckVal() const;
	/**
	 Returns total value of all morale bonuses and sets bonusList as a pointer to the list of selected bonuses.
	 @param bonusList is the out param it's list of all selected bonuses
	 @return total value of all morale within this member's accepted range, and 0 otherwise
	*/
	int moraleValAndBonusList(std::shared_ptr<const BonusList> & bonusList) const;
	int luckValAndBonusList(std::shared_ptr<const BonusList> & bonusList) const;

	bool unaffectedByMorale() const;

protected:
	/// Inclusive minimum and maximum Morale accepted by this member's rules context.
	virtual std::pair<int32_t, int32_t> getMoraleLimits() const;
	/// Effective Morale contributions; overrides must not mutate exported bonuses.
	virtual std::shared_ptr<const BonusList> getMoraleBonuses() const;

private:
	int moraleValAndBonusList(std::shared_ptr<const BonusList> & bonusList, int32_t additionalMorale) const;
};
