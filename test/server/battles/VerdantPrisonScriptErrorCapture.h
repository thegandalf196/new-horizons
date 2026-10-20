/*
 * VerdantPrisonScriptErrorCapture.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include <vstd/CLoggerBase.h>

#include <string>

// This fixture casts synchronously, without AI/script workers. Keep the proxy
// inside that cast's scope: logScript is a process-global, non-atomic pointer.
// Forwarding preserves the actual logger's exact targets and threshold; no
// target-removal API or expired target is needed after the capture ends.
class VerdantPrisonScriptErrorCapture final : public vstd::CLoggerBase
{
	vstd::CLoggerBase * const previous = logScript;
	mutable std::string captured;

public:
	VerdantPrisonScriptErrorCapture()
	{
		logScript = this;
	}

	~VerdantPrisonScriptErrorCapture() override
	{
		logScript = previous;
	}

	VerdantPrisonScriptErrorCapture(const VerdantPrisonScriptErrorCapture &) = delete;
	VerdantPrisonScriptErrorCapture & operator=(const VerdantPrisonScriptErrorCapture &) = delete;

	void log(ELogLevel::ELogLevel level, const std::string & message) const override
	{
		constexpr size_t limit = 16384;
		if(level == ELogLevel::ERROR && captured.size() < limit)
		{
			const auto available = limit - captured.size();
			captured.append(message, 0, available);
			if(message.size() > available)
				captured += " [script error capture truncated]";
			captured += '\n';
		}
		previous->log(level, message);
	}

	void log(ELogLevel::ELogLevel level, const boost::format & message) const override
	{
		log(level, message.str());
	}

	ELogLevel::ELogLevel getEffectiveLevel() const override { return previous->getEffectiveLevel(); }
	bool isDebugEnabled() const override { return previous->isDebugEnabled(); }
	bool isTraceEnabled() const override { return previous->isTraceEnabled(); }
	const std::string & errors() const { return captured; }
};
