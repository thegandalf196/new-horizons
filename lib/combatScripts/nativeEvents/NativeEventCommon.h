/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../ICombatEventScript.h"
#include "../../battle/CBattleInfoCallback.h"
#include "../../battle/IBattleState.h"
#include "../../battle/Unit.h"
#include "../../bonuses/Bonus.h"
#include "../../json/JsonNode.h"
#include <vcmi/ServerCallback.h>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

class CSpell;
class Creature;
class MetaString;
namespace nativeEvents
{
struct Context
{
	ServerCallback & server;
	const CBattleInfoCallback & battle;
	const battle::Unit * self;
	const battle::Unit * other;
	const JsonNode & parameters;
	const CombatEventPayload & payload;
};
using Handler = void (*)(const Context &);
std::shared_ptr<ICombatEventScript> event(Handler handler, std::initializer_list<CombatEventType> events,
	CombatEventForecastImplementation forecast = CombatEventForecastImplementation::UNKNOWN);
std::shared_ptr<ICombatEventScript> attackEvent(const std::string & path);
std::shared_ptr<ICombatEventScript> recoveryEvent(const std::string & path);
std::shared_ptr<ICombatEventScript> reflectEvent(const std::string & path);
std::shared_ptr<ICombatEventScript> setupEvent(const std::string & path);
std::shared_ptr<ICombatEventScript> focusMagicEvent(const std::string & path);

double number(const JsonNode & parameters, const std::string & key, double fallback = 0);
bool truthy(const JsonNode & value);
std::string string(const JsonNode & parameters, const std::string & key, const std::string & fallback = {});
const CSpell * spell(const std::string & name);
const Creature * creature(const std::string & name);
std::string creatureName(const battle::Unit * unit, int count);
void log(const Context & context, const MetaString & message);
void perish(const Context & context, const battle::Unit * victim, int64_t killed);
void spellDamageLog(const Context & context, const CSpell & spell, const battle::Unit * victim, int64_t damage, int64_t killed);
void lifeDrainedLog(const Context & context, int64_t healed, int64_t resurrected, int originalCount);

std::pair<int64_t, int64_t> damage(const Context & context, const battle::Unit & unit, int64_t amount,
	const battle::Unit * source, bool magical = false, const CSpell * spell = nullptr, bool destroyRemains = false);
std::pair<int64_t, int64_t> heal(const Context & context, const battle::Unit & unit, int64_t amount,
	EHealLevel level, EHealPower power);
void animation(const Context & context, const battle::Unit & unit, const std::string & animation,
	const std::string & sound, double transparency = 1.0, bool deferred = false);
void addBonus(const Context & context, const battle::Unit & unit, const Bonus & bonus, bool cumulative);
void removeBonuses(const Context & context, const battle::Unit & unit, const BonusList & bonuses);
void clearFrozen(const Context & context, const battle::Unit & unit);
void changeUnit(const Context & context, battle::Unit & state, int64_t healthDelta = 0);
void moveUnit(const Context & context, const battle::Unit & unit, BattleHex destination, bool teleport);
const battle::Unit * addUnit(const Context & context, battle::UnitInfo info);
void removeUnit(const Context & context, const battle::Unit & unit);
void refreshUnits(const Context & context);

int hostileBinomial(const Context & context, const battle::Unit & actor, const battle::Unit & recipient,
	int trials, double probability, int cap);
bool receptive(const Context & context, const battle::Unit & caster, const CSpell & spell, const battle::Unit & recipient);
void cast(const Context & context, const battle::Unit & caster, const CSpell & spell,
	const std::vector<const battle::Unit *> & targets, int64_t effectValue);
void applyEffects(const Context & context, const battle::Unit & caster, const CSpell & spell,
	const std::vector<const battle::Unit *> & targets, int level, int duration, bool ignoreImmunity);
int64_t adjustDamage(const Context & context, const CSpell & spell, const battle::Unit & source,
	const battle::Unit & target, int64_t rawDamage);
}
