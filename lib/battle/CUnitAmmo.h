/*
 * CUnitAmmo.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "Unit.h"
#include "../bonuses/BonusCache.h"

class JsonSerializeFormat;

namespace battle
{
class UnitStateValueFields;

class DLL_LINKAGE CAmmo
{
	friend class UnitStateValueFields;
public:
	explicit CAmmo(const battle::Unit * Owner, CSelector totalSelector);

	CAmmo & operator=(const CAmmo & other);
	CAmmo & operator=(CAmmo && other) = delete;

	int32_t available() const;
	bool canUse(int32_t amount = 1) const;
	virtual bool isLimited() const;
	virtual void reset();
	virtual int32_t total() const;
	virtual void use(int32_t amount = 1);

	virtual void serializeJson(JsonSerializeFormat & handler);
protected:
	int32_t used;
	const battle::Unit * owner;
	BonusValueCache totalProxy;
};

class DLL_LINKAGE CShots : public CAmmo
{
public:
	explicit CShots(const battle::Unit * Owner);
	CShots & operator=(const CShots & other);

	bool isLimited() const override;
	int32_t total() const override;

	void setEnv(const IUnitEnvironment * env_);
private:
	const IUnitEnvironment * env;

	BonusValueCache shooter;
};

class DLL_LINKAGE CCasts : public CAmmo
{
public:
	explicit CCasts(const battle::Unit * Owner);
};

class DLL_LINKAGE CRetaliations : public CAmmo
{
	friend class UnitStateValueFields;
public:
	explicit CRetaliations(const battle::Unit * Owner);
	CRetaliations & operator=(const CRetaliations & other);

	bool isLimited() const override;
	int32_t total() const override;
	void reset() override;
	void setEnv(const IUnitEnvironment * env_);

	void serializeJson(JsonSerializeFormat & handler) override;
private:
	mutable int32_t totalCache;
	const IUnitEnvironment * env;

	BonusValueCache noRetaliation;
	BonusValueCache unlimited;
};

}
