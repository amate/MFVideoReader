
#include "pch.h"
#include "DecoderPluginWrapper.h"
#include "..\MFVideoReaderPlugin\MFVideoDecoder.h"

#include "CodeConvert.h"
#include "Logger.h"
#include "..\MFMediaPropDump\MFFriendlyErrors.h"

INPUT_HANDLE MFVideoDecoder_func_open(LPSTR file, bool bUseDXVA2)
{
	try {
		auto decoder = new MFVideoDecoder();
		if (!decoder->Initialize(bUseDXVA2)) {
			delete decoder;
			return nullptr;
		}
		if (!decoder->OpenMediaFile(CodeConvert::UTF16fromShiftJIS(file))) {
			delete decoder;
			return nullptr;
		}
		return static_cast<INPUT_HANDLE>(decoder);
	}
	catch (HRESULT hr) {
		LPCWSTR errorText = nullptr;
		MFFriendlyConvertHRESULT(hr, &errorText);
		if (!errorText) {
			errorText = L"";
		}
		ERROR_LOG << L"MFVideoDecoder_func_open failed\nfile: " << CodeConvert::UTF16fromShiftJIS(file) << "\nhr: " << hr << L" [" << errorText << L"]";
	}
	return nullptr;
}

BOOL MFVideoDecoder_func_close(INPUT_HANDLE ih)
{
	try {
		MFVideoDecoder* decoder = static_cast<MFVideoDecoder*>(ih);
		decoder->Finalize();
		delete decoder;
		return TRUE;
	}
	catch (HRESULT hr) {
		LPCWSTR errorText = nullptr;
		MFFriendlyConvertHRESULT(hr, &errorText);
		if (!errorText) {
			errorText = L"";
		}
		ERROR_LOG << L"MFVideoDecoder_func_close failed - ih: " << ih << "hr: " << hr << L" [" << errorText << L"]";
	}
	return FALSE;
}

BOOL MFVideoDecoder_func_info_get(INPUT_HANDLE ih, INPUT_INFO* iip)
{
	try {
		MFVideoDecoder* decoder = static_cast<MFVideoDecoder*>(ih);
		auto& mi = decoder->GetMediaInfo();
		iip->flag = 0;
		if (mi.videoStreamIndex != -1) {
			iip->flag |= INPUT_INFO_FLAG_VIDEO | INPUT_INFO_FLAG_VIDEO_RANDOM_ACCESS;
			if (mi.nume > static_cast<UINT32>(std::numeric_limits<int>::max())) {
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
				iip->n = static_cast<int>(static_cast<double>(mi.totalAudioSampleCount) / mi.audioFormat.nSamplesPerSec) * 30;
				mi.imageFormat.biWidth = 320;
				mi.imageFormat.biHeight = 280;
				mi.imageFormat.biCompression = 0x32595559;
				mi.imageFormat.biBitCount = 16;
				iip->format = const_cast<BITMAPINFOHEADER*>(&mi.imageFormat);
				iip->format_size = sizeof(mi.imageFormat);

			}
		}
		iip->handler = 0;
		return TRUE;
	} 
	catch (HRESULT hr) {
		LPCWSTR errorText = nullptr;
		MFFriendlyConvertHRESULT(hr, &errorText);
		if (!errorText) {
			errorText = L"";
		}
		ERROR_LOG << L"MFVideoDecoder_func_info_get failed - ih: " << ih << "hr: " << hr << L" [" << errorText << L"]";
	}
	return FALSE;
}

int MFVideoDecoder_func_read_video(INPUT_HANDLE ih, int frame, void* buf)
{
	try {
		MFVideoDecoder* decoder = static_cast<MFVideoDecoder*>(ih);
		int n = decoder->ReadFrame(frame, static_cast<BYTE*>(buf));
		return n;
	}
	catch (HRESULT hr) {
		LPCWSTR errorText = nullptr;
		MFFriendlyConvertHRESULT(hr, &errorText);
		if (!errorText) {
			errorText = L"";
		}
		ERROR_LOG << L"MFVideoDecoder_func_read_video failed - ih: " << ih << "hr: " << hr << L" [" << errorText << L"]";
	}
	return 0;
}

int MFVideoDecoder_func_read_audio(INPUT_HANDLE ih, int start, int length, void* buf)
{
	try {
		MFVideoDecoder* decoder = static_cast<MFVideoDecoder*>(ih);
		int n = decoder->ReadAudio(start, length, buf);
		return n;
	}
	catch (HRESULT hr) {
		LPCWSTR errorText = nullptr;
		MFFriendlyConvertHRESULT(hr, &errorText);
		if (!errorText) {
			errorText = L"";
		}
		ERROR_LOG << L"MFVideoDecoder_func_read_audio failed - ih: " << ih << "hr: " << hr << L" [" << errorText << L"]";
	}
	return 0;
}