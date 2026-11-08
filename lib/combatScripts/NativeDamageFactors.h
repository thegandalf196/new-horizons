/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <array>
#include <utility>

struct DamageAttackInfo;
namespace nativeDamage
{
struct Facts;
std::pair<double, double> baseDamage(const DamageAttackInfo &, const Facts &);
std::array<double, 24> factors(const DamageAttackInfo &, const Facts &);
}
