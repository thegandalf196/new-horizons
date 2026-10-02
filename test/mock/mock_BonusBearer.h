/*
 * mock_BonusBearer.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once

#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/IBonusBearer.h"


class BonusBearerMock : public IBonusBearer
{
public:
	BonusBearerMock();
	virtual ~BonusBearerMock();

	void addNewBonus(const std::shared_ptr<Bonus> & b);

	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & cachingStr = "") const override;
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override;

	int32_t getTreeVersion() const override;
private:
	BonusList bonuses;
	mutable BonusList cachedBonuses;
	mutable BonusList cachedUnstackedBonuses;

	mutable int32_t cachedLast;
	int32_t treeVersion;
};
