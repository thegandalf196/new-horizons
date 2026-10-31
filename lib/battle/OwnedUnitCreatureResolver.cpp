/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedUnitCreatureResolver.h"
#include <stdexcept>
namespace battle
{
class OwnedUnitCreatureResolver::Descriptor final : public CCreature
{
	OwnedUnitCreatureResolver::Creature value;
	[[noreturn]] static void missing() { throw std::logic_error("Owned creature descriptor has no bonus/stat/translation domain"); }
public:
	explicit Descriptor(OwnedUnitCreatureResolver::Creature data) : CCreature(true), value(std::move(data)) { warMachine = value.warMachine; }
	CreatureID getId() const override { return value.id; }
	int32_t getIndex() const override { return value.id.getNum(); }
	int32_t getLevel() const override { return value.level; }
	FactionID getFactionID() const override { return value.faction; }
	bool isDoubleWide() const override { return value.doubleWide; }
	int32_t getIconIndex() const override { return value.icon; }
	const TResources & getFullRecruitCost() const override { return value.cost; }
	int32_t getRecruitCost(GameResID id) const override
	{
		return id.getNum() >= 0 && id.getNum() < value.cost.size() ? value.cost[id] : 0;
	}
	std::string getJsonKey() const override { return value.key; }
	std::string getModScope() const override { return value.key.substr(0, value.key.find(':')); }
	std::string getNamePluralTextID() const override { return value.pluralTextID; }
	std::string getNameSingularTextID() const override { return value.singularTextID; }
	std::string getNameTextID() const override { return value.singularTextID; }
	std::string getNameTranslated() const override { missing(); }
	std::string getNamePluralTranslated() const override { missing(); }
	std::string getNameSingularTranslated() const override { missing(); }
	const IBonusBearer * getBonusBearer() const override { missing(); }
	TConstBonusListPtr getAllBonuses(const CSelector &, const std::string &) const override { missing(); }
	TConstBonusListPtr getUnstackedBonuses(const CSelector &) const override { missing(); }
	int32_t getAdvMapAmountMin() const override { missing(); }
	int32_t getAdvMapAmountMax() const override { missing(); }
	int32_t getAIValue() const override
	{
		if(!value.aiValue) missing();
		return *value.aiValue;
	}
	int32_t getFightValue() const override { missing(); }
	int32_t getGrowth() const override { missing(); }
	int32_t getHorde() const override { missing(); }
	int32_t getBaseAttack() const override { missing(); }
	int32_t getBaseDefense() const override { missing(); }
	int32_t getBaseDamageMin() const override { missing(); }
	int32_t getBaseDamageMax() const override { missing(); }
	int32_t getBaseHitPoints() const override
	{
		if(!value.baseHitPoints) missing();
		return *value.baseHitPoints;
	}
	int32_t getBaseSpellPoints() const override { missing(); }
	int32_t getBaseSpeed() const override { missing(); }
	int32_t getBaseInitiative() const override { missing(); }
	int32_t getBaseShots() const override { missing(); }
	bool hasUpgrades() const override { missing(); }
};
OwnedUnitCreatureResolver::OwnedUnitCreatureResolver(std::vector<Creature> inputs)
{
	for(auto & value : inputs)
	{
		if(!value.id.hasValue() || value.key.empty() || descriptors.contains(value.id) || identifiers.contains(value.key))
			throw std::invalid_argument("Invalid or duplicate owned creature descriptor identity");
		identifiers.emplace(value.key, value.id);
		const auto id = value.id;
		descriptors.emplace(id, std::make_unique<Descriptor>(std::move(value)));
	}
}
OwnedUnitCreatureResolver::~OwnedUnitCreatureResolver() = default;
const CCreature * OwnedUnitCreatureResolver::creature(CreatureID id) const
{
	const auto found = descriptors.find(id);
	if(found == descriptors.end()) throw std::logic_error("Owned creature descriptor was not captured");
	return found->second.get();
}
int32_t OwnedUnitCreatureResolver::decode(const std::string & key) const
{
	const auto found = identifiers.find(key);
	if(found == identifiers.end()) throw std::logic_error("Owned creature identifier was not captured");
	return found->second.getNum();
}
std::string OwnedUnitCreatureResolver::encode(int32_t id) const { return creature(CreatureID(id))->getJsonKey(); }
}
