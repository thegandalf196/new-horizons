/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/logging/CLogger.h"
#include <cstdlib>
#include <string_view>
#include <chrono>
#include <string>
#include <vector>

namespace nativeBenchmark
{
class LogScope
{
	CLogger * global = CLogger::getGlobalLogger();
	CLogger * ai = CLogger::getLogger(CLoggerDomain("ai"));
	ELogLevel::ELogLevel oldGlobal = global->getLevel(), oldAi = ai->getLevel();
public:
	LogScope()
	{
		const char * value = std::getenv("NH_ORDINARY_DIAGNOSTICS");
		const auto level = value && std::string_view(value) == "1" ? ELogLevel::DEBUG : ELogLevel::INFO;
		global->setLevel(level);
		ai->setLevel(level);
	}
	~LogScope() { ai->setLevel(oldAi); global->setLevel(oldGlobal); }
};
template<typename Value> std::vector<std::byte> bytes(const Value & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}
inline std::string hex(const std::vector<std::byte> & data)
{
	constexpr char digits[] = "0123456789abcdef";
	std::string result;
	result.reserve(data.size() * 2);
	for(auto byte : data)
	{
		const auto value = std::to_integer<unsigned char>(byte);
		result += digits[value >> 4];
		result += digits[value & 15];
	}
	return result;
}
using Clock = std::chrono::steady_clock;
inline auto microseconds(Clock::time_point started)
{
	return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count();
}
}
