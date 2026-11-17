/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

namespace nativeRules
{
class CapturedRulePool;
/// Per serializer transaction only; no global or callback-owned lookup.
struct CapturedRuleWireContext
{
	CapturedRulePool * pool = nullptr;
};
}
