/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "../../Global.h"
#include <cstdlib>
#include <iostream>

// Standalone geometry tests do not bootstrap the game's logging service. Keep
// this adapter out of full-engine targets, and fail on any unexpected log path.
vstd::CLoggerBase::~CLoggerBase() = default;

namespace
{
class FailFastLogger final : public vstd::CLoggerBase
{
public:
	void log(ELogLevel::ELogLevel, const std::string & message) const override
	{
		std::cerr << "Unexpected engine log in standalone movement test: " << message << '\n';
		std::abort();
	}
	void log(ELogLevel::ELogLevel level, const boost::format & message) const override
	{
		log(level, message.str());
	}
	ELogLevel::ELogLevel getEffectiveLevel() const override { return ELogLevel::TRACE; }
	bool isDebugEnabled() const override { return false; }
	bool isTraceEnabled() const override { return false; }
};
FailFastLogger logger;
}
vstd::CLoggerBase * logGlobal = &logger;
