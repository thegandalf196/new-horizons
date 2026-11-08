/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "LegacyHotaImport.h"
#include <cctype>

namespace scripting::nativeMap::legacy
{
[[noreturn]] void unsupported(const std::string & reason)
{
	throw std::runtime_error("Unsupported legacy map event source: " + reason + ". Only unchanged generated HotA programs can be migrated; arbitrary Lua is unsupported.");
}
Parser::Parser(const std::string & source)
{
	for(size_t i = 0; i < source.size();)
	{
		const auto c = static_cast<unsigned char>(source[i]);
		if(std::isspace(c)) { ++i; continue; }
		if(source.compare(i, 2, "--") == 0) { i = source.find('\n', i); if(i == std::string::npos) break; continue; }
		if(c == '"')
		{
			std::string value;
			for(++i; i < source.size() && source[i] != '"'; ++i)
			{
				if(source[i] == '\\') unsupported("escaped string outside generated grammar");
				value += source[i];
			}
			if(i == source.size()) unsupported("unterminated string");
			++i; tokens.push_back({std::move(value), true}); continue;
		}
		if(std::isalnum(c) || c == '_')
		{
			const auto begin = i++;
			while(i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_')) ++i;
			tokens.push_back({source.substr(begin, i - begin)}); continue;
		}
		const auto pair = source.substr(i, 2);
		if(pair == "==" || pair == "~=" || pair == "<=" || pair == ">=") { tokens.push_back({pair}); i += 2; }
		else { tokens.push_back({source.substr(i, 1)}); ++i; }
	}
	tokens.push_back({"<eof>"});
}
const Token & Parser::peek(size_t offset) const { return tokens.at(std::min(position + offset, tokens.size() - 1)); }
Token Parser::take() { if(peek().text == "<eof>") unsupported("unexpected end of source"); return tokens.at(position++); }
bool Parser::accept(const std::string & text) { if(!peek().quoted && peek().text == text) { ++position; return true; } return false; }
void Parser::expect(const std::string & text) { if(!accept(text)) unsupported("expected '" + text + "', got '" + peek().text + "'"); }
void Parser::signature(const std::string & source)
{
	Parser expected(source);
	while(expected.peek().text != "<eof>")
	{
		const auto a = take(), b = expected.take();
		if(a.text != b.text || a.quoted != b.quoted) unsupported("modified generated helper prelude");
	}
}
namespace
{
int priority(const std::string & op)
{
	if(op == "or") return 1;
	if(op == "and") return 2;
	if(op == "==" || op == "~=" || op == "<" || op == ">" || op == "<=" || op == ">=") return 3;
	if(op == "+" || op == "-") return 4;
	if(op == "*" || op == "/" || op == "%") return 5;
	return 0;
}
}
JsonNode Parser::expression(int precedence)
{
	JsonNode left;
	if(peek().text == "-" || peek().text == "not")
	{
		left["kind"].String() = "unary"; left["text"].String() = take().text;
		left["args"].Vector().push_back(expression(6));
	}
	else left = atom();
	while(priority(peek().text) > precedence)
	{
		JsonNode result;
		result["kind"].String() = "binary"; result["text"].String() = take().text;
		const int next = priority(result["text"].String());
		result["args"].Vector().push_back(std::move(left));
		result["args"].Vector().push_back(expression(next));
		left = std::move(result);
	}
	return left;
}
JsonNode Parser::atom()
{
	JsonNode result;
	if(accept("(")) { result = expression(); expect(")"); return result; }
	if(peek().text == "{") return table();
	if(accept("function"))
	{
		result["kind"].String() = "function"; expect("("); expect(")");
		result["body"] = block(); expect("end"); return result;
	}
	const auto token = take();
	result["kind"].String() = token.quoted ? "string" : std::isdigit(static_cast<unsigned char>(token.text[0])) ? "number" : "name";
	result["text"].String() = token.text;
	if(token.quoted || result["kind"].String() == "number") return result;
	while(peek().text == "." || peek().text == ":")
	{
		const auto separator = take().text;
		const auto member = take();
		if(member.quoted) unsupported("quoted member name");
		result["text"].String() += separator + member.text;
	}
	if(accept("["))
	{
		result["kind"].String() = "index"; result["args"].Vector().push_back(expression()); expect("]"); return result;
	}
	if(peek().text == "(" || peek().text == "{")
	{
		result["kind"].String() = "call";
		if(accept("("))
		{
			result["tableCall"].Bool() = false;
			if(!accept(")")) { do { result["args"].Vector().push_back(expression()); } while(accept(",")); expect(")"); }
		}
		else { result["tableCall"].Bool() = true; result["args"].Vector().push_back(table()); }
		// The generated calendar query is the only call on a returned object.
		if(accept(":"))
		{
			requireCall(result, "game:getCalendar", 0);
			expect("getCurrentDay"); expect("("); expect(")"); result["text"].String() = "game:getCalendar():getCurrentDay";
		}
	}
	return result;
}
JsonNode Parser::table()
{
	expect("{"); JsonNode result; result["kind"].String() = "table";
	while(!accept("}"))
	{
		if(accept("["))
		{
			const auto key = expression(); expect("]"); expect("=");
			const auto name = key["kind"].String() == "string" ? literal(key) : std::to_string(integer(key));
			if(!result["fields"][name].isNull()) unsupported("duplicate table field " + name);
			result["keyKinds"][name].String() = key["kind"].String() == "string" ? "string" : "number";
			result["fields"][name] = expression();
		}
		else if(peek(1).text == "=")
		{
			const auto token = take(); if(token.quoted) unsupported("quoted assignment field");
			const auto & key = token.text;
			if(!result["fields"][key].isNull()) unsupported("duplicate table field " + key);
			result["keyKinds"][key].String() = "name";
			expect("="); result["fields"][key] = expression();
		}
		else result["args"].Vector().push_back(expression());
		if(!accept(",")) { expect("}"); break; }
	}
	return result;
}
JsonNode Parser::block()
{
	JsonNode result; result.Vector();
	while(peek().text != "end" && peek().text != "else" && peek().text != "elseif") result.Vector().push_back(statement());
	return result;
}
JsonNode Parser::statement()
{
	JsonNode result;
	if(accept("if"))
	{
		result["kind"].String() = "if";
		do
		{
			JsonNode branch; branch["condition"] = expression(); expect("then"); branch["body"] = block();
			result["branches"].Vector().push_back(std::move(branch));
		} while(accept("elseif"));
		if(accept("else")) result["otherwise"] = block();
		expect("end"); return result;
	}
	if(accept("for"))
	{
		result["kind"].String() = "for"; expect("__i"); expect("=");
		result["from"] = expression(); expect(","); result["to"] = expression(); expect("do");
		result["body"] = block(); expect("end"); return result;
	}
	if(accept("return")) { result["kind"].String() = "return"; result["value"] = expression(); return result; }
	result = expression();
	if(result["kind"].String() != "call") unsupported("non-call action");
	return result;
}
int64_t integer(const JsonNode & value)
{
	if(value["kind"].String() == "unary" && value["text"].String() == "-") return -integer(value["args"].Vector().at(0));
	if(value["kind"].String() != "number") unsupported("expected integer literal");
	if(value["text"].String().find_first_not_of("0123456789") != std::string::npos) unsupported("malformed integer literal");
	return std::stoll(value["text"].String());
}
std::string literal(const JsonNode & value)
{
	if(value["kind"].String() != "string") unsupported("expected string literal");
	return value["text"].String();
}
std::string entity(const JsonNode & value, const std::string & lookup)
{
	requireCall(value, "LIBRARY:" + lookup, 1);
	return literal(value["args"].Vector().at(0));
}
}
