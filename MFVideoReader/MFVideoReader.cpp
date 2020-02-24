// MFVideoReader.cpp : main source file for MFVideoReader.exe
//

#include "pch.h"

#include "MainDlg.h"

#include "..\Share\Logger.h"
#include "..\Share\IPC.h"
#include "..\Share\DecoderPluginWrapper.h"

CAppModule _Module;
HMODULE g_hModule;

bool	ProcessIPC(LPCWSTR lpCmdLine)
{
	std::wstring cmdLine = lpCmdLine;//::GetCommandLine();
	INFO_LOG << L"CommandLine: " << cmdLine;
	if (cmdLine.find(L"-sharedMemory") == std::wstring::npos) {
		return false;
	}

	auto llhandle = std::stoll(cmdLine);
	HANDLE hEvent = (HANDLE)llhandle;

	std::wstring kJobName = L"MFVideoReaderPlugin_Job" + std::to_wstring((uint64_t)hEvent);
	HANDLE hJob = ::OpenJobObject(JOB_OBJECT_ASSIGN_PROCESS, FALSE, kJobName.c_str());
	assert(hJob);
	if (hJob == NULL) {
		return false;
	}
	::AssignProcessToJobObject(hJob, GetCurrentProcess());
	::CloseHandle(hJob);

	size_t spacePos = cmdLine.find(L' ');
	// INFO_LOG << L"spacePos: " << spacePos;
	assert(spacePos != std::wstring::npos);
	std::wstring pipeName = cmdLine.substr(spacePos + 1);
	size_t tailSpacePos = pipeName.find(L' ');
	if (tailSpacePos != std::wstring::npos) {
		pipeName.resize(tailSpacePos);
	}

	std::wstring randomString;
	SharedMemory	videoSharedMemory;
	SharedMemory	audioSharedMemory;
	std::pair<void*, int>	videoSharedBuffer = { nullptr, 0 };
	std::pair<void*, int>	audioSharedBuffer = { nullptr, 0 };

	auto randPos = cmdLine.find(L'_');
	assert(randPos != std::wstring::npos);
	auto randPosEnd = cmdLine.find(L'_', randPos + 1);
	assert(randPosEnd != std::wstring::npos);
	randomString = cmdLine.substr(randPos, randPosEnd - randPos + 1);

	bool bUseDXVA2 = cmdLine.find(L"-useDXVA2") != std::wstring::npos;

	// TODO: ここにコードを挿入してください。
	//InitHook();
	NamedPipe namedPipe;
	bool success = namedPipe.OpenNamedPipe(pipeName);
	if (!success) {
		MessageBox(NULL, L"OpenNamedPipe に失敗しました。", L"MFVideoReader - エラー", MB_ICONERROR);
		return true;
	}

	// for Debug
	CallFunc	lastCallFunc;
	for (;;) {
		std::vector<BYTE> readData = namedPipe.Read(kToWindDataHeaderSize);
		if (readData.size() == 0) {
			assert(false);
			break;
		}
		ToWinputData* toData = (ToWinputData*)readData.data();
		lastCallFunc = toData->header.callFunc;

		std::vector<BYTE> dataBody = namedPipe.Read(toData->header.paramSize);
		switch (toData->header.callFunc) {
		case CallFunc::kOpen:
		{
			LPSTR file = (LPSTR)dataBody.data();
			INPUT_HANDLE ih = MFVideoDecoder_func_open(file, bUseDXVA2);
			//INFO_LOG << L"kOpen: " << ih;

			auto fromData = GenerateFromInputData(CallFunc::kOpen, ih, 0);
			namedPipe.Write((const BYTE*)fromData.get(), FromWinputDataTotalSize(*fromData));
			//INFO_LOG << L"Write: " << FromWinputDataTotalSize(*fromData) << L" bytes";
		}
		break;

		case CallFunc::kClose:
		{
			StandardParamPack* spp = (StandardParamPack*)dataBody.data();
			BOOL b = MFVideoDecoder_func_close(spp->ih);
			//INFO_LOG << L"kClose: " << spp->ih;

			auto fromData = GenerateFromInputData(CallFunc::kClose, b, 0);
			namedPipe.Write((const BYTE*)fromData.get(), FromWinputDataTotalSize(*fromData));
			//INFO_LOG << L"Write: " << FromWinputDataTotalSize(*fromData) << L" bytes";
		}
		break;

		case CallFunc::kInfoGet:
		{

			StandardParamPack* spp = (StandardParamPack*)dataBody.data();
			INPUT_INFO inputInfo = {};
			BOOL b = MFVideoDecoder_func_info_get(spp->ih, &inputInfo);
			assert(b);
			//INFO_LOG << L"kInfoGet: " << spp->ih;
			if (b) {
				int totalInputInfoSize = sizeof(INPUT_INFO) + inputInfo.format_size + inputInfo.audio_format_size;
				std::vector<BYTE> entireInputInfo(totalInputInfoSize);
				errno_t e = ::memcpy_s(entireInputInfo.data(), totalInputInfoSize, &inputInfo, sizeof(INPUT_INFO));
				e = ::memcpy_s(entireInputInfo.data() + sizeof(INPUT_INFO),
					inputInfo.format_size,
					inputInfo.format, inputInfo.format_size);
				e = ::memcpy_s(entireInputInfo.data() + sizeof(INPUT_INFO) + inputInfo.format_size,
					inputInfo.audio_format_size,
					inputInfo.audio_format, inputInfo.audio_format_size);

				auto fromData = GenerateFromInputData(CallFunc::kInfoGet, b, entireInputInfo.data(), totalInputInfoSize);
				namedPipe.Write((const BYTE*)fromData.get(), FromWinputDataTotalSize(*fromData));
				//INFO_LOG << L"Write: " << FromWinputDataTotalSize(*fromData) << L" bytes";

			} else {
				auto fromData = GenerateFromInputData(CallFunc::kInfoGet, b, 0);
				namedPipe.Write((const BYTE*)fromData.get(), FromWinputDataTotalSize(*fromData));
				//INFO_LOG << L"Write: " << FromWinputDataTotalSize(*fromData) << L" bytes";
			}
		}
		break;

		case CallFunc::kReadVideo:
		{
			StandardParamPack* spp = (StandardParamPack*)dataBody.data();
			void* buf;
			bool sharedMemoryReopen = false;
			if (videoSharedBuffer.second < spp->perBufferSize) {
				videoSharedMemory.CloseHandle();
				std::wstring sharedMemoryName = kVideoSharedMemoryPrefix + randomString + std::to_wstring(spp->perBufferSize);
				videoSharedBuffer.first = videoSharedMemory.CreateSharedMemory(sharedMemoryName.c_str(), spp->perBufferSize);
				if (!videoSharedBuffer.first) {
					DWORD error = ::GetLastError();
					std::wstring errorMsg = L"videoSharedMemory.CreateSharedMemoryに失敗\nsharedMemoryName: " + sharedMemoryName + L"\nGetLastError: " + std::to_wstring(error);
					MessageBox(NULL, errorMsg.c_str(), L"MFVideoReader - エラー", MB_ICONERROR);;
				}
				videoSharedBuffer.second = spp->perBufferSize;
				sharedMemoryReopen = true;
			}
			buf = videoSharedBuffer.first;
			
			INPUT_INFO inputInfo = {};
			const int frame = spp->param1;
			int readBytes = MFVideoDecoder_func_read_video(spp->ih, spp->param1, buf);
			//INFO_LOG << L"kReadVideo: " << spp->ih;

			namedPipe.Write((const BYTE*)&toData->header.callFunc, sizeof(toData->header.callFunc));
			std::int32_t totalSize = sizeof(int) + readBytes;
			namedPipe.Write((const BYTE*)&totalSize, sizeof(totalSize));
			namedPipe.Write((const BYTE*)&readBytes, sizeof(readBytes));
			namedPipe.Write((const BYTE*)&sharedMemoryReopen, sizeof(sharedMemoryReopen));

			//auto fromData = GenerateFromInputData(toData->header.callFunc, readBytes, g_readVideoBuffer.data(), readBytes);
			//namedPipe.Write((const BYTE*)fromData.get(), FromWinputDataTotalSize(*fromData));
			//INFO_LOG << L"Write: " << FromWinputDataTotalSize(*fromData) << L" bytes";
		}
		break;

		case CallFunc::kReadAudio:
		{
			StandardParamPack* spp = (StandardParamPack*)dataBody.data();
			const int PerAudioSampleBufferSize = spp->perBufferSize;
			const int requestReadBytes = PerAudioSampleBufferSize * spp->param2;
			void* buf;
			bool sharedMemoryReopen = false;
			if (audioSharedBuffer.second < requestReadBytes) {
				audioSharedMemory.CloseHandle();
				std::wstring sharedMemoryName = kAudioSharedMemoryPrefix + randomString + std::to_wstring(requestReadBytes);
				audioSharedBuffer.first = audioSharedMemory.CreateSharedMemory(sharedMemoryName.c_str(), requestReadBytes);
				if (!audioSharedBuffer.first) {
					DWORD error = ::GetLastError();
					std::wstring errorMsg = L"audioSharedMemory.CreateSharedMemoryに失敗\nsharedMemoryName: " + sharedMemoryName + L"\nGetLastError: " + std::to_wstring(error);
					MessageBox(NULL, errorMsg.c_str(), L"InputPipeMainエラー", MB_ICONERROR);;
				}
				audioSharedBuffer.second = requestReadBytes;
				sharedMemoryReopen = true;
			}
			buf = audioSharedBuffer.first;			

			int readSample = MFVideoDecoder_func_read_audio(spp->ih, spp->param1, spp->param2, buf);
			assert(readSample > 0);
			//INFO_LOG << L"kReadAudio: " << spp->ih << L" readSample: " << readSample;
			const int readBufferSize = PerAudioSampleBufferSize * readSample;

			namedPipe.Write((const BYTE*)&toData->header.callFunc, sizeof(toData->header.callFunc));
			std::int32_t totalSize = sizeof(int) + readBufferSize;
			namedPipe.Write((const BYTE*)&totalSize, sizeof(totalSize));
			namedPipe.Write((const BYTE*)&readSample, sizeof(readSample));
			namedPipe.Write((const BYTE*)&sharedMemoryReopen, sizeof(sharedMemoryReopen));
		
			//auto fromData = GenerateFromInputData(toData->header.callFunc, readSample, g_readAudioBuffer.data(), readBufferSize);
			//namedPipe.Write((const BYTE*)fromData.get(), FromWinputDataTotalSize(*fromData));
			//INFO_LOG << L"Write: " << FromWinputDataTotalSize(*fromData) << L" bytes";
		}
		break;

		case CallFunc::kIsKeyframe:
		{
			//INFO_LOG << L"kIsKeyframe";

			StandardParamPack* spp = (StandardParamPack*)dataBody.data();
			BOOL b = TRUE; // g_winputPluginTable->func_is_keyframe(spp->ih, spp->param1);

			auto fromData = GenerateFromInputData(CallFunc::kIsKeyframe, b, 0);
			namedPipe.Write((const BYTE*)fromData.get(), FromWinputDataTotalSize(*fromData));
		}
		break;

		case CallFunc::kExit:
		{
			//INFO_LOG << L"kExit";
			auto fromData = GenerateFromInputData(CallFunc::kExit, 0, 0);
			namedPipe.Write((const BYTE*)fromData.get(), FromWinputDataTotalSize(*fromData));

			namedPipe.Disconnect();
			return true;
		}
		break;

		default:
			assert(false);
			break;
		}

	}
	return true;
}

int Run(LPTSTR /*lpstrCmdLine*/ = NULL, int nCmdShow = SW_SHOWDEFAULT)
{
	CMessageLoop theLoop;
	_Module.AddMessageLoop(&theLoop);

	CMainDlg dlgMain;

	if(dlgMain.Create(NULL) == NULL)
	{
		ATLTRACE(_T("Main dialog creation failed!\n"));
		return 0;
	}

	dlgMain.ShowWindow(nCmdShow);

	int nRet = theLoop.Run();

	_Module.RemoveMessageLoop();
	return nRet;
}

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPTSTR lpstrCmdLine, int nCmdShow)
{
	if (ProcessIPC(lpstrCmdLine)) {
		return 0;
	}

	HINSTANCE hRich = ::LoadLibrary(CRichEditCtrl::GetLibraryName());
	if (hRich == NULL) {
		AtlMessageBox(NULL, _T("リッチエディットコントロール初期化失敗"), _T("エラー"), MB_OK | MB_ICONERROR);
		return 0;
	}

	HRESULT hRes = ::CoInitialize(NULL);
	ATLASSERT(SUCCEEDED(hRes));

	AtlInitCommonControls(ICC_BAR_CLASSES);	// add flags to support other controls

	hRes = _Module.Init(NULL, hInstance);
	ATLASSERT(SUCCEEDED(hRes));
	g_hModule = hInstance;

	int nRet = Run(lpstrCmdLine, nCmdShow);

	_Module.Term();
	::CoUninitialize();

	return nRet;
}
