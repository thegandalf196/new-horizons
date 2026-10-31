/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedProspectiveCreatureDomain.h"
#include <stdexcept>
namespace battle
{
std::vector<const Creature *> battleFormSpeciesPool(const CreatureService & creatures,
	BattleFormCategory target, const BattleFormCategoryLookup & category)
{
	std::vector<const Creature *> result;
	creatures.forEach([&](const Creature * creature, bool & stop)
	{
		(void)stop;
		if(!creature || creature->getBaseHitPoints() <= 0)
			return;

		const auto candidateCategory = category(creature->getId());
		if(candidateCategory && *candidateCategory == target)
			result.push_back(creature);
	});
	return result;
}
OwnedProspectiveCreatureDomain::OwnedProspectiveCreatureDomain(std::vector<Species> species,
	std::vector<CreationPrototype> prototypes)
{
	std::vector<OwnedUnitCreatureResolver::Creature> definitions;
	for(auto & value : species)
	{
		order.push_back(value.descriptor.id);
		categories.emplace(value.descriptor.id, value.category);
		definitions.push_back(std::move(value.descriptor));
	}
	resolver = std::make_shared<OwnedUnitCreatureResolver>(std::move(definitions));
	for(const auto & prototype : prototypes)
	{
		if(prototype.side != BattleSide::ATTACKER && prototype.side != BattleSide::DEFENDER)
			throw std::invalid_argument("Prospective creature prototype requires a battle side");
		(void)resolver->creature(prototype.species);
		if(!blueprints.emplace(std::pair{prototype.side, prototype.species}, prototype.blueprint).second)
			throw std::invalid_argument("Duplicate prospective creature prototype");
	}
}
std::optional<BattleFormCategory> OwnedProspectiveCreatureDomain::category(CreatureID id) const
{
	const auto found = categories.find(id);
	if(found == categories.end() || !found->second)
		throw std::logic_error("Prospective creature category was not captured");
	return *found->second;
}
std::vector<const Creature *> OwnedProspectiveCreatureDomain::formsFor(CreatureID current) const
{
	const auto target = category(current);
	if(!target) return {};
	return battleFormSpeciesPool(*this, *target, [this](CreatureID id) { return category(id); });
}
std::optional<size_t> OwnedProspectiveCreatureDomain::blueprintFor(CreatureID species, BattleSide side) const
{
	const auto found = blueprints.find({side, species});
	return found == blueprints.end() ? std::nullopt : std::optional<size_t>(found->second);
}
const Entity * OwnedProspectiveCreatureDomain::getBaseByIndex(int32_t index) const { return getByIndex(index); }
const Creature * OwnedProspectiveCreatureDomain::getById(const CreatureID & id) const { return resolver->creature(id); }
const Creature * OwnedProspectiveCreatureDomain::getByIndex(int32_t index) const { return getById(CreatureID(index)); }
const Creature * OwnedProspectiveCreatureDomain::getByName(const std::string & key) const
{
	return getById(CreatureID(resolver->decode(key)));
}
void OwnedProspectiveCreatureDomain::forEachBase(const std::function<void(const Entity *, bool &)> & callback) const
{
	forEach([&](const Creature * value, bool & stop) { callback(value, stop); });
}
void OwnedProspectiveCreatureDomain::forEach(const std::function<void(const Creature *, bool &)> & callback) const
{
	bool stop = false;
	for(const auto id : order)
	{
		callback(getById(id), stop);
		if(stop) break;
	}
}
}
