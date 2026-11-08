/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "RunicCreatureCaster.h"
#include "AbilityCaster.h"
#include "BonusCaster.h"
#include "../CStack.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/BattleInfo.h"
#include "../battle/IBattleState.h"
#include "../networkPacks/PacksForClientBattle.h"
#include <vcmi/spells/Caster.h>
#include <vcmi/spells/Spell.h>
#include <array>
#include <typeinfo>
#include <set>

namespace spells
{
namespace
{
class RunicCreatureCaster final : public Caster
{
	SpellSchool school = SpellSchool::ANY;
	int32_t id, schoolLevel, effectLevel, power, divisor, enchant, mana;
	int64_t value, range;
	PlayerColor owner;
	std::string name;
	MetaString description;
	std::array<int32_t, 4> elemental;
public:
	RunicCreatureCaster(const Caster & source, const Spell & spell, const battle::Units & recipients)
		: id(source.getCasterUnitId()), schoolLevel(source.getSpellSchoolLevel(&spell, &school)),
		  effectLevel(source.getEffectLevel(&spell)), power(source.getEffectPower(&spell)),
		  divisor(source.getEffectPowerDivisor(&spell)), enchant(source.getEnchantPower(&spell)),
		  mana(source.manaLimit()), value(source.getEffectValue(&spell)), range(source.getEffectRange(&spell)),
		  owner(source.getCasterOwner()), name(source.getCasterNameTextID())
	{
		source.getCastDescription(&spell, recipients, description);
		for(size_t i = 0; i < elemental.size(); ++i)
			elemental[i] = source.getElementalSpellDamageBonus(static_cast<SpellDamageElement>(i));
	}
	int32_t getCasterUnitId() const override { return id; }
	int32_t getSpellSchoolLevel(const Spell *, SpellSchool * selected = nullptr) const override
	{ if(selected) *selected = school; return schoolLevel; }
	int32_t getEffectLevel(const Spell *) const override { return effectLevel; }
	// These are identity operations in the admitted native creature/ability
	// caster implementations. Never retain their borrowed original-unit graph.
	int64_t getSpellBonus(const Spell *, int64_t base, const battle::Unit *) const override { return base; }
	int64_t getSpecificSpellBonus(const Spell *, int64_t base) const override { return base; }
	int32_t getElementalSpellDamageBonus(SpellDamageElement element) const override
	{ return element == SpellDamageElement::NONE ? 0 : elemental.at(static_cast<size_t>(element)); }
	int32_t getEffectPower(const Spell *) const override { return power; }
	int32_t getEffectPowerDivisor(const Spell *) const override { return divisor; }
	int32_t getEnchantPower(const Spell *) const override { return enchant; }
	int64_t getEffectValue(const Spell *) const override { return value; }
	int64_t getEffectRange(const Spell *) const override { return range; }
	PlayerColor getCasterOwner() const override { return owner; }
	std::string getCasterNameTextID() const override { return name; }
	void getCastDescription(const Spell *, const battle::Units &, MetaString & text) const override
	{ text = description; }
	void spendMana(ServerCallback *, int32_t) const override
	{ throw std::logic_error("A reflected caster snapshot cannot spend another resource"); }
	int32_t manaLimit() const override { return mana; }
	const CGHeroInstance * getHeroCaster() const override { return nullptr; }
};
}
bool admitsRunicCreatureCaster(const Caster & source, const CBattleInfoCallback & callback)
{
	const auto * native = dynamic_cast<const BattleInfo *>(callback.getBattle());
	if(!native) return false;
	const Caster * cursor = &source;
	std::set<const Caster *> visited;
	for(int depth = 0; cursor && depth < 8; ++depth)
	{
		if(!visited.insert(cursor).second) return false;
		if(typeid(*cursor) == typeid(CStack))
		{
			const auto * unit = static_cast<const CStack *>(cursor);
			return !unit->isGhost() && unit->getBattle() == native
				&& native->battleGetStackByID(unit->unitId(), false) == unit;
		}
		if(typeid(*cursor) != typeid(AbilityCaster) && typeid(*cursor) != typeid(BonusCaster)) return false;
		cursor = static_cast<const ProxyCaster *>(cursor)->actualCaster;
	}
	return false;
}

std::unique_ptr<Caster> copyRunicCreatureCaster(const Caster & source)
{
	const auto * snapshot = dynamic_cast<const RunicCreatureCaster *>(&source);
	return snapshot ? std::make_unique<RunicCreatureCaster>(*snapshot) : nullptr;
}

std::unique_ptr<Caster> captureRunicCreatureCaster(const Caster & source, const Spell & spell,
	const CBattleInfoCallback & battle, const battle::Units & recipients)
{
	return admitsRunicCreatureCaster(source, battle)
		? std::make_unique<RunicCreatureCaster>(source, spell, recipients) : nullptr;
}
}
