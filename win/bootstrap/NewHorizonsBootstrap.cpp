/* Part of New Horizons. GPL-2.0-or-later; see license.txt. */
// This entry point must load using Windows system DLLs only. The game and its
// app-local dependencies stay together below runtime/, before their main runs.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include "BootstrapArguments.h"

namespace
{
constexpr wchar_t argumentVariable[] = L"NH_WINDOWS_BOOTSTRAP_ARGUMENTS_V1";

std::wstring modulePath()
{
	std::wstring buffer(512, L'\0');
	while(buffer.size() <= 32768)
	{
		const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if(!length)
			return {};
		if(length < buffer.size())
			return buffer.substr(0, length);
		buffer.resize(buffer.size() * 2);
	}
	return {};
}

bool ordinaryFile(const std::wstring & path)
{
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES
		&& !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}

int failure(const wchar_t * message)
{
	MessageBoxW(nullptr, message, L"New Horizons", MB_OK | MB_ICONERROR);
	return 1;
}

std::wstring previousArguments()
{
	const DWORD length = GetEnvironmentVariableW(argumentVariable, nullptr, 0);
	if(!length)
		return {};
	std::wstring value(length, L'\0');
	const DWORD copied = GetEnvironmentVariableW(argumentVariable, value.data(), length);
	return copied && copied < length ? value.substr(0, copied) : std::wstring{};
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	const std::wstring ownPath = modulePath();
	const size_t separator = ownPath.find_last_of(L"\\/");
	if(separator == std::wstring::npos)
		return failure(L"Cannot locate the New Horizons package.");
	const std::wstring runtime = ownPath.substr(0, separator) + L"\\runtime";
	const std::wstring script = runtime + L"\\Start-New-Horizons.ps1";
	if(!ordinaryFile(script) || !ordinaryFile(runtime + L"\\Launch-Process.ps1")
		|| !ordinaryFile(runtime + L"\\Setup-Progress.ps1")
		|| !ordinaryFile(runtime + L"\\new-horizons.exe"))
		return failure(L"The runtime folder is missing or incomplete. Extract the entire matching New Horizons ZIP, then open new-horizons.exe.");

	wchar_t systemDirectory[MAX_PATH + 1] = {};
	const UINT length = GetSystemDirectoryW(systemDirectory, MAX_PATH + 1);
	if(!length || length > MAX_PATH)
		return failure(L"Cannot locate Windows PowerShell.");
	const std::wstring powershell = std::wstring(systemDirectory, length)
		+ L"\\WindowsPowerShell\\v1.0\\powershell.exe";
	if(!ordinaryFile(powershell))
		return failure(L"Windows PowerShell 5.1 is required. New Horizons supports Windows 10 and 11.");

	int count = 0;
	LPWSTR * argv = CommandLineToArgvW(GetCommandLineW(), &count);
	if(!argv)
		return failure(L"Cannot read the launch arguments.");
	std::vector<std::wstring> arguments;
	for(int index = 1; index < count; ++index)
		arguments.emplace_back(argv[index]);
	LocalFree(argv);
#ifdef NH_SETUP_ONLY
	if(!arguments.empty())
		return failure(L"Open Setup.exe without arguments to choose your original Heroes III assets.");
	const std::wstring envelope;
#else
	const std::wstring envelope = newHorizonsBootstrap::argumentEnvelope(arguments);
#endif
	const std::wstring previous = previousArguments();
	SetEnvironmentVariableW(L"NH_WINDOWS_BOOTSTRAP_UI", L"1");
	if(envelope.size() >= 32767 || !SetEnvironmentVariableW(argumentVariable, envelope.empty() ? nullptr : envelope.c_str()))
		return failure(L"The launch arguments exceed the supported Windows environment size.");

	std::wstring command = newHorizonsBootstrap::quoteWindowsArgument(powershell)
		+ L" -NoLogo -NoProfile -STA -ExecutionPolicy Bypass -File "
		+ newHorizonsBootstrap::quoteWindowsArgument(script);
#ifdef NH_SETUP_ONLY
	// These belong to setup, never to the engine's argument envelope.
	command += L" -SelectAssets -SetupOnly";
#endif
	const BOOL console = AttachConsole(ATTACH_PARENT_PROCESS);
	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	if(console)
	{
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
		startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	}
	PROCESS_INFORMATION process = {};
	const BOOL started = CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr,
		TRUE, console ? 0 : CREATE_NO_WINDOW, nullptr, runtime.c_str(), &startup, &process);
	SetEnvironmentVariableW(argumentVariable, previous.empty() ? nullptr : previous.c_str());
	if(!started)
		return failure(L"Windows could not start New Horizons setup. Check the runtime folder and Windows PowerShell installation.");
	CloseHandle(process.hThread);
	const DWORD waited = WaitForSingleObject(process.hProcess, INFINITE);
	DWORD exitCode = 1;
	const BOOL readExit = waited == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exitCode);
	CloseHandle(process.hProcess);
	return readExit ? static_cast<int>(exitCode) : 1;
}
