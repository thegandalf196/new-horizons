/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "OwnedUnitCreatureResolver.h"
#include "BattleSide.h"
#include "../entities/creature/NewHorizonsCreatureCategoryRules.h"
#include <vcmi/CreatureService.h>
#include <functional>
#include <cstddef>
#include <vector>
namespace battle
{
using BattleFormCategory = newHorizonsCreatures::CreatureCategory;
using BattleFormCategoryLookup = std::function<std::optional<BattleFormCategory>(CreatureID)>;
/// Canonical complete pool; preserves service order and HP-before-category query order.
DLL_LINKAGE std::vector<const Creature *> battleFormSpeciesPool(const CreatureService & creatures,
	BattleFormCategory target, const BattleFormCategoryLookup & category);
/// Copied PUBLIC species definitions, not action legality, source admission or unit state.
class DLL_LINKAGE OwnedProspectiveCreatureDomain final : public CreatureService, boost::noncopyable
{
public:
	struct Species
	{
		OwnedUnitCreatureResolver::Creature descriptor;
		// Disengaged outer value is UNKNOWN; engaged empty inner value is known unmapped.
		std::optional<std::optional<BattleFormCategory>> category;
	};
	struct CreationPrototype
	{
		BattleSide side = BattleSide::NONE;
		CreatureID species = CreatureID::NONE;
		size_t blueprint; // NONQUERYABLE index in the retaining capture's private blueprint store.
	};
	explicit OwnedProspectiveCreatureDomain(std::vector<Species> species,
		std::vector<CreationPrototype> prototypes = std::vector<CreationPrototype>());
	const std::shared_ptr<const OwnedUnitCreatureResolver> & creatures() const { return resolver; }
	std::vector<const Creature *> formsFor(CreatureID current) const;
	/// Absence means uncaptured, NEVER proof that an existing legal action is illegal.
	std::optional<size_t> blueprintFor(CreatureID species, BattleSide side) const;
	const Entity * getBaseByIndex(int32_t index) const override;
	const Creature * getById(const CreatureID & id) const override;
	const Creature * getByIndex(int32_t index) const override;
	const Creature * getByName(const std::string & key) const override;
	void forEachBase(const std::function<void(const Entity *, bool &)> & callback) const override;
	void forEach(const std::function<void(const Creature *, bool &)> & callback) const override;
private:
	std::shared_ptr<const OwnedUnitCreatureResolver> resolver;
	std::vector<CreatureID> order;
	std::map<CreatureID, std::optional<std::optional<BattleFormCategory>>> categories;
	std::map<std::pair<BattleSide, CreatureID>, size_t> blueprints;
	std::optional<BattleFormCategory> category(CreatureID id) const;
};
}
