/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../json/JsonNode.h"

namespace scripting::nativeMap
{
// Recognizes the pinned converter's generated source; no Lua runtime or evaluation.
DLL_LINKAGE JsonNode importLegacyHotaEvents(const std::string & source);
// Pure translation, separated from resolving the active game's resource catalogue.
DLL_LINKAGE JsonNode importLegacyHotaProgram(const std::string & source, const std::vector<std::string> & resourceOrder);
namespace legacy
{
struct Token { std::string text; bool quoted = false; };
class Parser
{
	std::vector<Token> tokens;
	size_t position = 0;
	JsonNode atom();
	JsonNode table();
	JsonNode statement();
public:
	explicit Parser(const std::string & source);
	const Token & peek(size_t offset = 0) const;
	Token take();
	bool accept(const std::string & text);
	void expect(const std::string & text);
	void signature(const std::string & source);
	JsonNode expression(int precedence = 0);
	JsonNode block();
};
[[noreturn]] void unsupported(const std::string & reason);
int64_t integer(const JsonNode & value);
std::string literal(const JsonNode & value);
std::string entity(const JsonNode & value, const std::string & lookup);
void requireCall(const JsonNode & value, const std::string & name, size_t arguments, bool tableCall = false);
void requireName(const JsonNode & value, const std::string & name);
void requireArray(const JsonNode & value, std::optional<size_t> size = std::nullopt);
void requireRecord(const JsonNode & value, const std::vector<std::string> & required, const std::vector<std::string> & optional = {});
bool booleanLiteral(const JsonNode & value);
int enumLiteral(const JsonNode & value, const std::string & group);
std::string handlerBucket(const std::string & name);
void validateActionCall(const JsonNode & value, const std::string & bucket);
JsonNode expression(const JsonNode & value);
JsonNode condition(const JsonNode & value);
JsonNode actions(const JsonNode & value, const std::string & bucket);
JsonNode action(const JsonNode & value, const std::string & bucket);
JsonNode images(const JsonNode & value);
std::string text(const JsonNode & value);
}
}
