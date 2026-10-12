/*
 * Private Wine child-environment bootstrap, part of New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 * Test prerequisite only: never staged in a player package.
 */
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../clientapp/PrivateWinePathPolicy.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{
constexpr size_t MAX_ENVIRONMENT = 131072;

enum class Stage2Detail : unsigned
{
	NONE = 0,
	READ_LOCAL = 1,
	READ_TARGET = 2,
	READ_FACADE = 3,
	ROUTE_LOCAL = 4,
	ROUTE_TARGET = 5,
	ROUTE_FACADE = 6,
	LOCAL_ANCESTORS = 7,
	TARGET_FILE = 8,
	FACADE_FILE = 9,
	TARGET_NAME = 10,
	FACADE_PAIR = 11,
	GATE_ANCESTORS = 12,
};

template<typename Predicate>
bool stage2Check(Stage2Detail & detail, Stage2Detail slot, Predicate predicate)
{
	detail = slot;
	return predicate();
}

std::string ancestorRefusalMessage(unsigned depth, unsigned long attrs, unsigned long error)
{
	char buffer[128];
	std::snprintf(buffer, sizeof(buffer), "NH_WINE_BOOTSTRAP_ANCESTOR_REFUSAL depth=%u attrs=%lu error=%lu\n", depth, attrs, error);
	return buffer;
}

std::wstring variableName(const std::wstring & entry)
{
	// Drive-current-directory pseudo variables begin with '='; keep that key.
	const auto end = entry.find(L'=', entry.front() == L'=' ? 1 : 0);
	if(end == std::wstring::npos || end == 0)
		throw std::invalid_argument("Invalid environment entry");
	return entry.substr(0, end);
}

int compareNames(const std::wstring & left, const std::wstring & right)
{
#ifdef _WIN32
	const int result = CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
		right.c_str(), static_cast<int>(right.size()), TRUE);
	if(!result)
		throw std::runtime_error("Environment comparison failed");
	return result - CSTR_EQUAL;
#else
	// Process-free test build uses ASCII names; values retain arbitrary Unicode.
	const auto upper = [](std::wstring value)
	{
		for(auto & ch : value)
			if(ch >= L'a' && ch <= L'z') ch -= L'a' - L'A';
		return value;
	};
	return upper(left).compare(upper(right));
#endif
}

bool boundedRoute(const std::wstring & value)
{
	return value.size() >= 4 && value.size() < 260 && value.substr(0, 3) == L"Z:\\"
		&& value.find(L"..") == std::wstring::npos && value.find_first_of(L"\r\n\0", 0, 3) == std::wstring::npos
		&& value.back() != L'\\' && value.find(L'"') == std::wstring::npos;
}

bool fixtureName(const std::wstring & name)
{
	const auto ascii = [](wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'); };
	if(name.size() < 6 || name.size() > 69 || name.substr(name.size() - 5) != L".vmap" || !ascii(name.front()))
		return false;
	std::wstring stem = name.substr(0, name.size() - 5);
	for(auto & c : stem)
	{
		if(!ascii(c) && c != L'_' && c != L'-') return false;
		if(c >= L'a' && c <= L'z') c -= L'a' - L'A';
	}
	if(stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL") return false;
	return !(stem.size() == 4 && (stem.substr(0, 3) == L"COM" || stem.substr(0, 3) == L"LPT")
		&& stem[3] >= L'1' && stem[3] <= L'9');
}

std::wstring fixtureCommand(int argc, wchar_t ** argv)
{
	if(argc == 1) return {};
	if(argc != 3 || std::wstring(argv[1]) != L"--fixture-map" || !fixtureName(argv[2]))
		throw std::invalid_argument("Arguments refused");
	return L" --testmap Maps/" + std::wstring(argv[2]) + L" --spectate --savefrequency 0";
}

std::vector<wchar_t> childEnvironment(const std::vector<std::wstring> & inherited, const std::wstring & local)
{
	if(!boundedRoute(local) || local.size() < 13 || local.substr(local.size() - 13) != L"\\localappdata"
		|| inherited.size() > 4096)
		throw std::invalid_argument("Invalid private environment");
	std::vector<std::wstring> entries;
	size_t size = local.size() + 15;
	for(const auto & entry : inherited)
	{
		if(entry.empty() || entry.find(L'\0') != std::wstring::npos)
			throw std::invalid_argument("Invalid environment entry");
		const auto name = variableName(entry);
		if(compareNames(name, L"LOCALAPPDATA") == 0)
			continue;
		if(entry.size() + 1 > MAX_ENVIRONMENT - size)
			throw std::invalid_argument("Environment too large");
		size += entry.size() + 1;
		entries.push_back(entry);
	}
	entries.push_back(L"LOCALAPPDATA=" + local);
	std::stable_sort(entries.begin(), entries.end(), [](const auto & a, const auto & b)
	{
		return compareNames(variableName(a), variableName(b)) < 0;
	});
	std::vector<wchar_t> result;
	result.reserve(size);
	for(const auto & entry : entries)
	{
		result.insert(result.end(), entry.begin(), entry.end());
		result.push_back(0);
	}
	result.push_back(0);
	return result;
}

#ifdef _WIN32
struct Handle
{
	HANDLE value = INVALID_HANDLE_VALUE;
	~Handle() { if(value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

std::wstring environment(const wchar_t * name)
{
	wchar_t value[2048];
	const DWORD size = GetEnvironmentVariableW(name, value, 2048);
	if(!size || size >= 2048)
		throw std::invalid_argument("Missing environment");
	return std::wstring(value, size);
}

bool plainDirectoryAncestors(std::wstring path)
{
	unsigned depth = 0;
	while(path.size() >= 3)
	{
		const DWORD attrs = GetFileAttributesW(path.c_str());
		const DWORD error = attrs == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
		if(!privateWinePathPolicy::isDirectoryAllowed(path, attrs))
		{
			const auto message = ancestorRefusalMessage(depth, static_cast<unsigned long>(attrs), static_cast<unsigned long>(error));
			std::fputs(message.c_str(), stderr);
			return false;
		}
		if(path.size() == 3) return true;
		const auto slash = path.find_last_of(L'\\');
		if(slash == std::wstring::npos) return false;
		path.resize(slash == 2 ? 3 : slash);
		++depth;
	}
	return false;
}

bool plainFile(const std::wstring & path)
{
	const DWORD attrs = GetFileAttributesW(path.c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
		&& plainDirectoryAncestors(path.substr(0, path.find_last_of(L'\\')));
}

struct Attributes
{
	std::vector<unsigned char> storage;
	LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
	~Attributes() { if(list) DeleteProcThreadAttributeList(list); }
};

int bootstrap(int argc, wchar_t ** argv)
{
	unsigned stage = 1;
	Stage2Detail detail = Stage2Detail::NONE;
	try
	{
		const auto fixture = fixtureCommand(argc, argv);
		// Kept in the PE for admission; this helper never initializes SDL.
		const auto nonce = environment(L"NH_PRIVATE_WINE_NONCE");
		if(nonce.size() != 64 || nonce.find_first_not_of(L"0123456789abcdef") != std::wstring::npos)
			throw std::invalid_argument("Invalid challenge");
		stage = 2;
		detail = Stage2Detail::READ_LOCAL;
		const auto local = environment(L"NH_PRIVATE_WINE_LOCALAPPDATA");
		detail = Stage2Detail::READ_TARGET;
		const auto target = environment(L"NH_PRIVATE_WINE_EXECUTABLE");
		detail = Stage2Detail::READ_FACADE;
		const auto facade = environment(L"NH_PRIVATE_WINE_FACADE");
		if(!stage2Check(detail, Stage2Detail::ROUTE_LOCAL, [&] { return boundedRoute(local); })
			|| !stage2Check(detail, Stage2Detail::ROUTE_TARGET, [&] { return boundedRoute(target); })
			|| !stage2Check(detail, Stage2Detail::ROUTE_FACADE, [&] { return boundedRoute(facade); })
			|| !stage2Check(detail, Stage2Detail::LOCAL_ANCESTORS, [&] { return plainDirectoryAncestors(local); })
			|| !stage2Check(detail, Stage2Detail::TARGET_FILE, [&] { return plainFile(target); })
			|| !stage2Check(detail, Stage2Detail::FACADE_FILE, [&] { return plainFile(facade); }))
			throw std::invalid_argument("Invalid private path");
		const auto cwd = target.substr(0, target.find_last_of(L'\\'));
		if(!stage2Check(detail, Stage2Detail::TARGET_NAME, [&] { return target.substr(target.find_last_of(L'\\') + 1) == L"new-horizons.exe"; })
			|| !stage2Check(detail, Stage2Detail::FACADE_PAIR, [&] { return facade == cwd + L"\\VCMI_lib.dll"; })
			|| !stage2Check(detail, Stage2Detail::GATE_ANCESTORS, [&] { return plainDirectoryAncestors(local + L"\\.nh-private-wine-gate"); }))
			throw std::invalid_argument("Invalid selected pair");
		if(!fixture.empty())
		{
			const auto map = local + L"\\HeroesIII-NewHorizons\\content\\Maps\\" + argv[2];
			if(!boundedRoute(map) || !plainFile(map))
				throw std::invalid_argument("Invalid staged fixture");
		}
		detail = Stage2Detail::NONE;
		stage = 3;
		if(environment(L"SDL_AUDIODRIVER") != L"dummy" || environment(L"SDL_AUDIO_DRIVER") != L"dummy"
			|| environment(L"SDL_VIDEODRIVER") != L"windows" || environment(L"SDL_VIDEO_DRIVER") != L"windows")
			throw std::invalid_argument("Invalid silent route");
		stage = 4;
		std::vector<std::wstring> inherited;
		wchar_t * block = GetEnvironmentStringsW();
		if(!block) throw std::runtime_error("Environment read failed");
		try
		{
			size_t consumed = 0;
			for(auto cursor = block; *cursor;)
			{
				const std::wstring entry(cursor);
				consumed += entry.size() + 1;
				if(consumed >= MAX_ENVIRONMENT) throw std::invalid_argument("Environment too large");
				inherited.push_back(entry);
				cursor += entry.size() + 1;
			}
		}
		catch(...) { FreeEnvironmentStringsW(block); throw; }
		FreeEnvironmentStringsW(block);
		auto childEnv = childEnvironment(inherited, local);
		stage = 5;
		Handle out, err, input;
		const auto duplicate = [](DWORD which, Handle & result)
		{
			const HANDLE source = GetStdHandle(which);
			if(!source || source == INVALID_HANDLE_VALUE || !DuplicateHandle(GetCurrentProcess(), source,
				GetCurrentProcess(), &result.value, 0, TRUE, DUPLICATE_SAME_ACCESS))
				throw std::runtime_error("Stdio duplication failed");
		};
		duplicate(STD_OUTPUT_HANDLE, out);
		duplicate(STD_ERROR_HANDLE, err);
		SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
		input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
		if(input.value == INVALID_HANDLE_VALUE) throw std::runtime_error("NUL input failed");
		Attributes attributes;
		SIZE_T bytes = 0;
		InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
		if(!bytes) throw std::runtime_error("Attribute sizing failed");
		attributes.storage.resize(bytes);
		auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.storage.data());
		if(!InitializeProcThreadAttributeList(list, 1, 0, &bytes)) throw std::runtime_error("Attributes failed");
		attributes.list = list;
		HANDLE handles[]{input.value, out.value, err.value};
		if(!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr))
			throw std::runtime_error("Handle list failed");
		STARTUPINFOEXW startup{};
		startup.StartupInfo.cb = sizeof(startup);
		startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
		startup.StartupInfo.hStdInput = input.value;
		startup.StartupInfo.hStdOutput = out.value;
		startup.StartupInfo.hStdError = err.value;
		startup.lpAttributeList = list;
		PROCESS_INFORMATION process{};
		auto command = L"\"" + target + L"\"" + fixture;
		stage = 6;
		if(!CreateProcessW(target.c_str(), command.data(), nullptr, nullptr, TRUE,
			CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT, childEnv.data(), cwd.c_str(), &startup.StartupInfo, &process))
			throw std::runtime_error("Child creation failed");
		Handle child, thread;
		child.value = process.hProcess;
		thread.value = process.hThread;
		stage = 7;
		// The armed external owner bounds this wait and the complete child graph
		// at 600/640 seconds. This helper alone is not a supervision contract.
		if(WaitForSingleObject(child.value, INFINITE) != WAIT_OBJECT_0) throw std::runtime_error("Child wait failed");
		DWORD exit = 0;
		if(!GetExitCodeProcess(child.value, &exit)) throw std::runtime_error("Child exit failed");
		return static_cast<int>(exit);
	}
	catch(...)
	{
		std::fprintf(stderr, "NH_WINE_BOOTSTRAP_REFUSAL stage=%u detail=%u\n", stage, static_cast<unsigned>(detail));
		std::fflush(stderr);
		return 87;
	}
}
#endif
}

#ifdef NH_WINE_BOOTSTRAP_ENV_TEST
int main()
{
	for(const auto name : {L"playback-cabir.vmap", L"a.vmap", L"Cabir_2.vmap"})
		if(!fixtureName(name)) return 11;
	for(const auto name : {L"../bad.vmap", L"Maps/bad.vmap", L"CON.vmap", L"lpt9.vmap", L"a b.vmap", L"x.vmap\"", L"x.h3m", L".vmap", L"x.VMAP"})
		if(fixtureName(name)) return 12;
	wchar_t program[] = L"bootstrap";
	wchar_t option[] = L"--fixture-map";
	wchar_t map[] = L"playback-cabir.vmap";
	wchar_t * arguments[]{program, option, map};
	if(!fixtureCommand(1, arguments).empty()
		|| fixtureCommand(3, arguments) != L" --testmap Maps/playback-cabir.vmap --spectate --savefrequency 0") return 13;
	try { fixtureCommand(2, arguments); return 14; } catch(const std::invalid_argument &) {}
	using namespace privateWinePathPolicy;
	if(!isDirectoryAllowed(L"Z:\\", DIRECTORY | REPARSE_POINT)
		|| !isDirectoryAllowed(L"Z:\\", DIRECTORY)
		|| !isDirectoryAllowed(L"Z:\\private\\localappdata", DIRECTORY)
		|| !isDirectoryAllowed(L"C:\\", DIRECTORY)) return 8;
	for(const auto path : {L"Z:", L"z:\\", L"Z:\\\\", L"Z:/", L"Z:\\.", L"Z:\\private", L"C:\\", L"\\\\server\\share\\"})
		if(isDirectoryAllowed(path, DIRECTORY | REPARSE_POINT) || isCanonicalRoot(path)) return 9;
	for(const auto attrs : {INVALID_ATTRIBUTES, REPARSE_POINT, std::uint32_t(0)})
		if(isDirectoryAllowed(L"Z:\\", attrs)) return 10;
	if(ancestorRefusalMessage(0, 0xffffffffUL, 3)
		!= "NH_WINE_BOOTSTRAP_ANCESTOR_REFUSAL depth=0 attrs=4294967295 error=3\n"
		|| ancestorRefusalMessage(8, 0x410UL, 0)
		!= "NH_WINE_BOOTSTRAP_ANCESTOR_REFUSAL depth=8 attrs=1040 error=0\n") return 7;
	// These process-free controls exercise numbering, short-circuiting and
	// exception attribution without calling any Windows filesystem or process API.
	Stage2Detail detail = Stage2Detail::NONE;
	unsigned calls = 0;
	if(!stage2Check(detail, Stage2Detail::ROUTE_LOCAL, [&] { ++calls; return false; })
		|| !stage2Check(detail, Stage2Detail::ROUTE_TARGET, [&] { ++calls; return true; }))
	{
		if(calls != 1 || static_cast<unsigned>(detail) != 4) return 4;
	}
	else return 4;
	if(!stage2Check(detail, Stage2Detail::TARGET_NAME, [] { return true; })
		|| !stage2Check(detail, Stage2Detail::FACADE_PAIR, [] { return false; })
		|| !stage2Check(detail, Stage2Detail::GATE_ANCESTORS, [] { return true; }))
	{
		if(static_cast<unsigned>(detail) != 11) return 5;
	}
	else return 5;
	try
	{
		stage2Check(detail, Stage2Detail::TARGET_FILE, []() -> bool { throw std::runtime_error("synthetic"); });
		return 6;
	}
	catch(const std::runtime_error &)
	{
		if(static_cast<unsigned>(detail) != 8) return 6;
	}
	const auto block = childEnvironment({L"=C:=C:\\private", L"SDL_AUDIODRIVER=dummy", L"LOCALAPPDATA=old",
		L"localappdata=duplicate", L"LOCALAPPDATA_EXTRA=keep", L"UNICODE=\u03a9\u4e2d", L"NH_PRIVATE_WINE_NONCE=unchanged"}, L"Z:\\private\\localappdata");
	std::vector<std::wstring> entries;
	for(auto cursor = block.data(); *cursor; cursor += entries.back().size() + 1) entries.emplace_back(cursor);
	if(block.size() < 2 || block.back() || block[block.size() - 2] || entries.size() != 6
		|| std::count(entries.begin(), entries.end(), L"LOCALAPPDATA=Z:\\private\\localappdata") != 1
		|| std::find(entries.begin(), entries.end(), L"=C:=C:\\private") == entries.end()
		|| std::find(entries.begin(), entries.end(), L"UNICODE=\u03a9\u4e2d") == entries.end()
		|| std::find(entries.begin(), entries.end(), L"LOCALAPPDATA_EXTRA=keep") == entries.end()
		|| std::find(entries.begin(), entries.end(), L"SDL_AUDIODRIVER=dummy") == entries.end()
		|| std::find(entries.begin(), entries.end(), L"NH_PRIVATE_WINE_NONCE=unchanged") == entries.end()
		|| !std::is_sorted(entries.begin(), entries.end(), [](const auto & a, const auto & b)
			{ return compareNames(variableName(a), variableName(b)) < 0; })) return 1;
	for(const auto & invalid : {L"C:\\private\\localappdata", L"Z:\\..\\localappdata", L"Z:\\private\\localappdata\\"})
	{
		try { childEnvironment({}, invalid); return 2; } catch(const std::invalid_argument &) {}
	}
	try { childEnvironment({}, L"Z:\\" + std::wstring(260, L'x') + L"\\localappdata"); return 3; }
	catch(const std::invalid_argument &) {}
	for(const auto & invalid : {std::wstring(), std::wstring(L"MALFORMED"), std::wstring(131072, L'x') + L"=large"})
	{
		try { childEnvironment({invalid}, L"Z:\\private\\localappdata"); return 4; }
		catch(const std::invalid_argument &) {}
	}
	return 0;
}
#elif defined(_WIN32)
int wmain(int argc, wchar_t ** argv)
{
	// Volatile prevents this fixed admission marker from being optimized away.
	const wchar_t * volatile protocol = L"NH_WINE_BOOTSTRAP_V1";
	(void)protocol;
	const wchar_t * volatile fixtureProtocol = L"NH_WINE_BOOTSTRAP_FIXTURE_V1";
	(void)fixtureProtocol;
	return bootstrap(argc, argv);
}
#endif
