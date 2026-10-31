/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/json/JsonNode.h"
#include <gtest/gtest.h>
namespace capturedSpecialtySnapshotTest
{
using Version = ESerializationVersion;
template<class T> std::vector<std::byte> encode(T & value, Version epoch = Version::CURRENT)
{
	CMemorySerializer wire;
	wire.oser.version = epoch;
	value.serialize(wire.oser);
	return wire.extractBuffer();
}
// A constructed CURRENT snapshot, not a naturally played save. The normal
// owner codec controls field order and every ordinary BinarySerializer write.
// Only the JsonNode at one explicit public getter identity is substituted.
class RuleSnapshotWriter
{
	BinarySerializer & base;
	const JsonNode * identity;
	const JsonNode & replacement;
public:
	using Version = ESerializationVersion;
	static constexpr bool saving = true;
	bool loadingGamestate = false;
	int substitutions = 0;
	RuleSnapshotWriter(BinarySerializer & writer, const JsonNode * target, const JsonNode & rules)
		: base(writer), identity(target), replacement(rules) {}
	bool hasFeature(Version epoch) const { return base.hasFeature(epoch); }
	template<class T> RuleSnapshotWriter & operator&(T & value)
	{
		base & value;
		return *this;
	}
	RuleSnapshotWriter & operator&(JsonNode & value)
	{
		if(&value == identity)
		{
			base & replacement;
			++substitutions;
		}
		else base & value;
		return *this;
	}
};
template<class T> std::vector<std::byte> constructedSnapshot(T & owner,
	const JsonNode & capturedIdentity, const JsonNode & replacement)
{
	CMemorySerializer passthrough;
	RuleSnapshotWriter ordinary(passthrough.oser, nullptr, replacement);
	owner.serialize(ordinary);
	EXPECT_EQ(ordinary.substitutions, 0);
	EXPECT_EQ(passthrough.extractBuffer(), encode(owner));
	CMemorySerializer changed;
	RuleSnapshotWriter injection(changed.oser, &capturedIdentity, replacement);
	owner.serialize(injection);
	EXPECT_EQ(injection.substitutions, 1);
	return changed.extractBuffer();
}
}
