
#include "Common.h"
#include "ptreeWrapper.h"

fs::path GetExeDirectory()
{
	WCHAR exePath[MAX_PATH] = L"";
	GetModuleFileName(g_hModule, exePath, MAX_PATH);
	fs::path exeFolder = exePath;
	return exeFolder.parent_path();
}


int Config::get_severity_level()
{
	switch (logLevel) {
	case 0:	// none
		return 10;
	case 1:	// info
		return 2;
	case 2: // warning
		return 3;
	case 3: // error
		return 4;

	default:
		assert(false);
		return 10;
	}
	return 0;
}

bool Config::LoadConfig()
{
	auto ptree = ptreeWrapper::LoadIniPtree(kConfigFileName);
	bUseDXVA2 = ptree.get<bool>(L"Config.bUseDXVA2", true);
	bEnableHandleCache = ptree.get<bool>(L"Config.bEnableHandleCache", true);
	bEnableIPC = ptree.get<bool>(L"Config.bEnableIPC", false);
	logLevel = ptree.get<int>(L"Config.logLevel", 3);

	return true;
}

bool Config::SaveConfig()
{
	ptreeWrapper::wptree ptree;
	ptree.put(L"Config.bUseDXVA2", bUseDXVA2);
	ptree.put(L"Config.bEnableHandleCache", bEnableHandleCache);
	ptree.put(L"Config.bEnableIPC", bEnableIPC);
	ptree.put(L"Config.logLevel", logLevel);

	bool success = ptreeWrapper::SaveIniPtree(kConfigFileName, ptree);
	return success;
}