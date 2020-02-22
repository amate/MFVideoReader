//----------------------------------------------------------------------------------
//		サンプルAVI(vfw経由)入力プラグイン  for AviUtl ver0.98以降
//----------------------------------------------------------------------------------
#include "pch.h"

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <tuple>
#include <algorithm>
#include <random>
#include <mutex>
#include <chrono>

#include <windows.h>
//#include	<vfw.h>
//#pragma comment(lib, "Vfw32.lib")

#include <shellapi.h>
#pragma comment(lib, "Shell32.lib")

#include	"input.h"

#include "MFVideoDecoder.h"

#include "..\Share\Logger.h"
#include "..\Share\IPC.h"
#include "..\Share\Common.h"
#include "..\Share\CodeConvert.h"

extern HMODULE g_hModule;

// for Logger
std::string	LogFileName()
{
	return (GetExeDirectory() / L"MFVideoReaderPlugin.log").string();
}


//#define NO_REMOTE
//#define DEBUG_PROCESSINGTIME


//---------------------------------------------------------------------
//		入力プラグイン構造体定義
//---------------------------------------------------------------------
INPUT_PLUGIN_TABLE input_plugin_table = {
	INPUT_PLUGIN_FLAG_VIDEO|INPUT_PLUGIN_FLAG_AUDIO,	//	フラグ
														//	INPUT_PLUGIN_FLAG_VIDEO	: 画像をサポートする
														//	INPUT_PLUGIN_FLAG_AUDIO	: 音声をサポートする
	"MFVideoReaderPlugin",									//	プラグインの名前
	"any files (*.*)\0*.*\0",							//	入力ファイルフィルタ
	"MediaFoundation Video Reader by amate version " PLUGIN_VERSION,		//	プラグインの情報
	func_init,											//	DLL開始時に呼ばれる関数へのポインタ (NULLなら呼ばれません)
	func_exit,											//	DLL終了時に呼ばれる関数へのポインタ (NULLなら呼ばれません)
	func_open,											//	入力ファイルをオープンする関数へのポインタ
	func_close,											//	入力ファイルをクローズする関数へのポインタ
	func_info_get,										//	入力ファイルの情報を取得する関数へのポインタ
	func_read_video,									//	画像データを読み込む関数へのポインタ
	func_read_audio,									//	音声データを読み込む関数へのポインタ
	func_is_keyframe,									//	キーフレームか調べる関数へのポインタ (NULLなら全てキーフレーム)
	func_config,										//	入力設定のダイアログを要求された時に呼ばれる関数へのポインタ (NULLなら呼ばれません)
};


//---------------------------------------------------------------------
//		入力プラグイン構造体のポインタを渡す関数
//---------------------------------------------------------------------
EXTERN_C INPUT_PLUGIN_TABLE __declspec(dllexport) * __stdcall GetInputPluginTable( void )
{

	return &input_plugin_table;
}


#if 0
//---------------------------------------------------------------------
//		ファイルハンドル構造体
//---------------------------------------------------------------------
typedef struct {
	int				flag;
	PAVIFILE		pfile;
	PAVISTREAM		pvideo,paudio;
	AVIFILEINFO		fileinfo;
	AVISTREAMINFO	videoinfo,audioinfo;
	void			*videoformat;
	LONG			videoformatsize;
	void			*audioformat;
	LONG			audioformatsize;
} FILE_HANDLE;
#define FILE_HANDLE_FLAG_VIDEO		1
#define FILE_HANDLE_FLAG_AUDIO		2
#endif

//---------------------------------------------------------------------
//		初期化
//---------------------------------------------------------------------
BOOL func_init( void )
{
	INFO_LOG << L"func_init";
	if (!MFInitialize()) {
		return FALSE;
	}
	return TRUE;
}


//---------------------------------------------------------------------
//		終了
//---------------------------------------------------------------------
BOOL func_exit( void )
{
	INFO_LOG << L"func_exit";
	MFFinalize();
	return TRUE;
}


//---------------------------------------------------------------------
//		ファイルオープン
//---------------------------------------------------------------------
INPUT_HANDLE func_open( LPSTR file )
{
	INFO_LOG << L"func_open: " << CodeConvert::UTF16fromShiftJIS(file);

	auto decoder = new MFVideoDecoder();
	if (!decoder->Initialize(true)) {
		delete decoder;
		return nullptr;
	}
	if (!decoder->OpenMediaFile(CodeConvert::UTF16fromShiftJIS(file))) {
		delete decoder;
		return nullptr;
	}
	return static_cast<INPUT_HANDLE>(decoder);
}


//---------------------------------------------------------------------
//		ファイルクローズ
//---------------------------------------------------------------------
BOOL func_close( INPUT_HANDLE ih )
{
	INFO_LOG << L"func_close: " << ih;
	MFVideoDecoder* decoder = static_cast<MFVideoDecoder*>(ih);
	decoder->Finalize();
	delete decoder;
	return TRUE;
}


//---------------------------------------------------------------------
//		ファイルの情報
//---------------------------------------------------------------------
BOOL func_info_get( INPUT_HANDLE ih,INPUT_INFO *iip )
{
	INFO_LOG << L"func_info_get";
	MFVideoDecoder* decoder = static_cast<MFVideoDecoder*>(ih);
	auto& mi = decoder->GetMediaInfo();
	iip->flag = 0;
	if (mi.videoStreamIndex != -1) {
		iip->flag |= INPUT_INFO_FLAG_VIDEO | INPUT_INFO_FLAG_VIDEO_RANDOM_ACCESS;
		if (mi.nume > std::numeric_limits<int>::max()) {
			iip->rate = static_cast<int>(mi.nume / 10);
			iip->scale = static_cast<int>(mi.denom / 10);
		} else {
			iip->rate = mi.nume;
			iip->scale = mi.denom;
		}
		iip->n = mi.totalFrameCount;
		iip->format = const_cast<BITMAPINFOHEADER*>(&mi.imageFormat);
		iip->format_size = sizeof(mi.imageFormat);
	}
	if (mi.audioStreamIndex != -1) {	
		iip->flag |= INPUT_INFO_FLAG_AUDIO;	
		iip->audio_n = mi.totalAudioSampleCount;
		iip->audio_format = const_cast<WAVEFORMATEX*>(&mi.audioFormat);
		iip->audio_format_size = sizeof(WAVEFORMATEX);
		if (mi.videoStreamIndex == -1) {	
			// なぜかオーディオのみでも映像を出力しないと読み込みに失敗したとみなされてしまうので
			// ダミー映像を挿入する
			iip->flag |= INPUT_INFO_FLAG_VIDEO | INPUT_INFO_FLAG_VIDEO_RANDOM_ACCESS;
			// 30fps
			iip->rate = 30;
			iip->scale = 1;
			iip->n = (static_cast<double>(mi.totalAudioSampleCount) / mi.audioFormat.nSamplesPerSec) * 30;
			mi.imageFormat.biWidth = 320;
			mi.imageFormat.biHeight = 280;
			mi.imageFormat.biCompression = 0x32595559;
			mi.imageFormat.biBitCount = 16;
			iip->format = const_cast<BITMAPINFOHEADER*>(&mi.imageFormat);
			iip->format_size = sizeof(mi.imageFormat);

		}
	}
	iip->handler = 0;

	//iip->audio_format = nullptr;
	//iip->audio_format_size = 0;	// 
	return TRUE;
}

//---------------------------------------------------------------------
//		画像読み込み
//---------------------------------------------------------------------
int func_read_video( INPUT_HANDLE ih,int frame,void *buf )
{
	//INFO_LOG << L"func_read_video" << L" frame: " << frame;
	MFVideoDecoder* decoder = static_cast<MFVideoDecoder*>(ih);
	int n = decoder->ReadFrame(frame, static_cast<BYTE*>(buf));
	return n;
}

//---------------------------------------------------------------------
//		音声読み込み
//---------------------------------------------------------------------
int func_read_audio(INPUT_HANDLE ih, int start, int length, void* buf)
{
	//INFO_LOG << L"func_read_audio: " << ih << L" start: " << start << L" length: " << length;
	MFVideoDecoder* decoder = static_cast<MFVideoDecoder*>(ih);
	int n = decoder->ReadAudio(start, length, buf);
 	return n;
}


//---------------------------------------------------------------------
//		キーフレーム情報
//---------------------------------------------------------------------
BOOL func_is_keyframe(INPUT_HANDLE ih, int frame)
{
	INFO_LOG << L"func_is_keyframe" << L" frame:" << frame;

	return TRUE;
}


//---------------------------------------------------------------------
//		設定ダイアログ
//---------------------------------------------------------------------
BOOL func_config( HWND hwnd, HINSTANCE dll_hinst )
{
	INFO_LOG << L"func_config";

	auto InputPipeMainPath = GetExeDirectory() / L"InputPipeMain.exe";
	::ShellExecute(NULL, NULL, InputPipeMainPath.c_str(), L" -config", NULL, SW_SHOWNORMAL);
	return TRUE;
}


