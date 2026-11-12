#pragma once
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <fwpmu.h>
#include <string>
DWORD VerifyStandaloneNetworkPolicy(const wchar_t* executable);
DWORD InstallStandaloneNetworkPolicy(const wchar_t* executable);
DWORD GetStandaloneAppIdentity(const wchar_t* executable,std::wstring& path,FWP_BYTE_BLOB** app);
const GUID& StandalonePolicySublayer();
DWORD ServeStandalonePeerPolicy(const wchar_t* executable);
