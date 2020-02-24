
#include "pch.h"
#include "MFVideoDecoder.h"

#include <atomic>
#include <cmath>

#include <mfapi.h>
#include <mferror.h>
#include <evr.h>
#include <propvarutil.h>
#include <amvideo.h>
#include <d3d11.h>
#include <shlwapi.h>
//#include "unknownbase.h"
//#include "timer.h"

#include <boost\format.hpp>

#include "..\Share\Logger.h"
#include "..\MFMediaPropDump\Helper.h"

#pragma comment(lib,"mf.lib")
#pragma comment(lib,"mfplat.lib")
#pragma comment(lib,"mfuuid.lib")
#pragma comment(lib,"strmiids.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "Propsys.lib")
#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "Dxva2.lib")
#pragma comment(lib, "Shlwapi.lib")

namespace {

	std::atomic<int>		g_MFInitialized = 0;

#define IF_FAILED_THROW(hr) \
	if (FAILED(hr)) { \
		ATLASSERT(FALSE);	\
		ERROR_LOG << L"failed func: " << __func__ << L" line: " <<  __LINE__ << L" hr: " << hr; \
		throw hr;	\
	}	

#define IF_FAILED_ERRORLOG(hr) \
	if (FAILED(hr)) { \
		ATLASSERT(FALSE);	\
		ERROR_LOG << L"failed func: " << __func__ << L" line: " <<  __LINE__ << L" hr: " << hr; \
	}

#define CHECK_HR

// This function creates a D3D Device and a D3D Device Manager, sets the manager
// to use the device, and returns the manager. It also initializes the D3D
// device. This function is used by mfdecoder.cc during the call to
// MFDecoder::GetDXVA2AttributesForSourceReader().
// Returns: The D3D manager object if successful. Otherwise, NULL is returned.
static IDirect3DDeviceManager9* CreateD3DDevManager(HWND video_window,
	IDirect3DDevice9** device) {

	int ret = -1;
	CComPtr<IDirect3DDeviceManager9> dev_manager;
	CComPtr<IDirect3D9> d3d;
	d3d.Attach(Direct3DCreate9(D3D_SDK_VERSION));
	if (d3d == NULL) {
		//LOG(ERROR) << "Failed to create D3D9";
		return NULL;
	}
	D3DPRESENT_PARAMETERS present_params = { 0 };
	// Not sure if these values are correct, or if
	// they even matter. (taken from DXVA_HD sample code)
	present_params.BackBufferWidth = 0;
	present_params.BackBufferHeight = 0;
	present_params.BackBufferFormat = D3DFMT_UNKNOWN;
	present_params.BackBufferCount = 1;
	present_params.SwapEffect = D3DSWAPEFFECT_DISCARD;
	present_params.hDeviceWindow = video_window;
	present_params.Windowed = TRUE;
	present_params.Flags = D3DPRESENTFLAG_VIDEO;
	present_params.FullScreen_RefreshRateInHz = 0;
	present_params.PresentationInterval = 0;
	CComPtr<IDirect3DDevice9> temp_device;
	// D3DCREATE_HARDWARE_VERTEXPROCESSING specifies hardware vertex processing.
	HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT,
		D3DDEVTYPE_HAL,
		video_window,
		(D3DCREATE_HARDWARE_VERTEXPROCESSING |
			D3DCREATE_MULTITHREADED),
		&present_params,
		&temp_device);
	if (FAILED(hr)) {
		//LOG(ERROR) << "Failed to create D3D Device";
		return NULL;
	}
	UINT dev_manager_reset_token = 0;
	hr = DXVA2CreateDirect3DDeviceManager9(&dev_manager_reset_token,
		&dev_manager);
	if (FAILED(hr)) {
		//LOG(ERROR) << "Couldn't create D3D Device manager";
		return NULL;
	}
	hr = dev_manager->ResetDevice(temp_device.p, dev_manager_reset_token);
	if (FAILED(hr)) {
		//LOG(ERROR) << "Failed to set device to device manager";
		return NULL;
	}
	*device = temp_device.Detach();
	return dev_manager.Detach();
}

// Resets the D3D device to prevent scaling from happening because it was
// created with window before resizing occurred. We need to change the back
// buffer dimensions to the actual video frame dimensions.
// Both the decoder and device should be initialized before calling this method.
// Returns: true if successful.
static bool AdjustD3DDeviceBackBufferDimensions(UINT width, UINT height,
	IDirect3DDevice9* device,
	HWND video_window) {
	//CHECK(decoder != NULL);
	//CHECK(decoder->initialized());
	//CHECK(decoder->use_dxva2());
	//CHECK(device != NULL);
	D3DPRESENT_PARAMETERS present_params = { 0 };
	present_params.BackBufferWidth = width;
	present_params.BackBufferHeight = height;
	present_params.BackBufferFormat = D3DFMT_UNKNOWN;
	present_params.BackBufferCount = 1;
	present_params.SwapEffect = D3DSWAPEFFECT_DISCARD;
	present_params.hDeviceWindow = video_window;
	present_params.Windowed = TRUE;
	present_params.Flags = D3DPRESENTFLAG_VIDEO;
	present_params.FullScreen_RefreshRateInHz = 0;
	present_params.PresentationInterval = 0;
	return SUCCEEDED(device->Reset(&present_params)) ? true : false;
}

HRESULT ConfigureDecoder(IMFSourceReader* pReader, DWORD dwStreamIndex)
{
	CComPtr<IMFMediaType> pNativeType;
	CComPtr<IMFMediaType> pType;

	// Find the native format of the stream.
	HRESULT hr = pReader->GetNativeMediaType(dwStreamIndex, 0, &pNativeType);
	if (FAILED(hr)) {
		return hr;
	}

	GUID majorType, subtype;

	// Find the major type.
	hr = pNativeType->GetGUID(MF_MT_MAJOR_TYPE, &majorType);
	if (FAILED(hr)) {
		goto done;
	}

	// Define the output type.
	hr = MFCreateMediaType(&pType);
	if (FAILED(hr)) {
		goto done;
	}

	hr = pType->SetGUID(MF_MT_MAJOR_TYPE, majorType);
	if (FAILED(hr)) {
		goto done;
	}

	// Select a subtype.
	if (majorType == MFMediaType_Video) {
		subtype = MFVideoFormat_YUY2;//MFVideoFormat_RGB32;
		//subtype = MFVideoFormat_NV12;
		hr = pType->SetGUID(MF_MT_SUBTYPE, subtype);
		if (FAILED(hr)) {
			goto done;
		}
	} else if (majorType == MFMediaType_Audio) {
		subtype = MFAudioFormat_PCM;

		UINT32 nativeAudioChannels = 0;
		hr = pNativeType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &nativeAudioChannels);
		if (FAILED(hr)) {
			goto done;
		}
		enum { kStereoChannelNum = 2 };
		if (kStereoChannelNum < nativeAudioChannels) {
			WARN_LOG << L"ConfigureDecoder audio channel change - nativeAudioChannels: " 
															<< nativeAudioChannels << L" -> 2";
			pType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
		}
	} else {
		// Unrecognized type. Skip.
		goto done;
	}

	hr = pType->SetGUID(MF_MT_SUBTYPE, subtype);
	if (FAILED(hr)) {
		goto done;
	}

	// Set the uncompressed format.
	hr = pReader->SetCurrentMediaType(dwStreamIndex, NULL, pType);
	if (FAILED(hr)) {
		goto done;
	}
done:
	return hr;
}

double ConvertSecFrom100ns(LONGLONG nsTime)
{
	double sec = (nsTime / 10000000.0);
	return sec;
}

LONGLONG Convert100nsFromSec(double sec)
{
	LONGLONG nsTime = static_cast<LONGLONG>(sec * 10000000);
	return nsTime;
}

// timestamp -> frame
int ConvertFrameFromTimeStamp(LONGLONG nsTimeStamp, UINT32 nume, UINT32 denom)
{
	double frame = (ConvertSecFrom100ns(nsTimeStamp) * nume) / denom;
	int numFrame = static_cast<int>(std::round(frame));
	return numFrame;
}

// frame -> timestamp
LONGLONG ConvertTimeStampFromFrame(LONGLONG frame, UINT32 nume, UINT32 denom)
{
	double frameSec = (static_cast<double>(frame * denom) / nume);
	LONGLONG nsTime = Convert100nsFromSec(frameSec);
	return nsTime;
}

// timestamp -> Sample
int ConvertSampleFromTimeStamp(LONGLONG nsTimeStamp, DWORD nSamplesPerSec)
{
	double sample = ConvertSecFrom100ns(nsTimeStamp) * nSamplesPerSec;
	int numSample = static_cast<int>(std::round(sample));
	return numSample;
}

// Sample -> timestamp
LONGLONG ConvertTimeStampFromSample(int startSample, DWORD nSamplesPerSec)
{
	double sampleSec = static_cast<double>(startSample) / nSamplesPerSec;
	LONGLONG nsTime = Convert100nsFromSec(sampleSec);
	return nsTime;
}

DWORD SampleCopyToBuffer(IMFSample* pSample, BYTE* buf, int copyBufferPos, int copyBufferSize)
{
	CComPtr<IMFMediaBuffer> spBuffer;
	HRESULT hr = pSample->ConvertToContiguousBuffer(&spBuffer);
	ATLASSERT(SUCCEEDED(hr));
	BYTE* pData = nullptr;
	DWORD currentLength = 0;
	hr = spBuffer->Lock(&pData, nullptr, &currentLength);
	ATLASSERT(SUCCEEDED(hr));

	//ATLTRACE(L"currentLength: %d\n", currentLength);
	ATLASSERT((copyBufferPos + copyBufferSize) <= static_cast<UINT32>(currentLength));
	memcpy(buf, pData + copyBufferPos, copyBufferSize/*currentLength*/);

	hr = spBuffer->Unlock();
	ATLASSERT(SUCCEEDED(hr));

	return copyBufferSize;
}


DWORD SampleCopyToBuffer(IMFSample* pSample, BYTE* buf, int copyBufferSize)
{
	return SampleCopyToBuffer(pSample, buf, 0, copyBufferSize);
}

void	SelectStream(IMFSourceReader* pReader, const GUID& selectMajorType)
{
	HRESULT hr = S_OK;
	for (DWORD streamIndex = 0; SUCCEEDED(hr); ++streamIndex) {
		CComPtr<IMFMediaType> currentMediaType;
		hr = pReader->GetCurrentMediaType(streamIndex, &currentMediaType);
		if (FAILED(hr)) {
			break;
		}
		BOOL selected = FALSE;
		pReader->GetStreamSelection(streamIndex, &selected);
		if (!selected) {
			continue;
		}

		GUID majorType;
		hr = currentMediaType->GetMajorType(&majorType);
		IF_FAILED_THROW(hr);
		if (::IsEqualGUID(majorType, selectMajorType)) {
			pReader->SetStreamSelection(streamIndex, TRUE);
		} else {
			pReader->SetStreamSelection(streamIndex, FALSE);
		}
	}
}

void Seek(IMFSourceReader* pReader, LONGLONG destTimePosition)
{
	PROPVARIANT var;
	HRESULT hr = InitPropVariantFromInt64(destTimePosition, &var);
	ATLASSERT(SUCCEEDED(hr));
	hr = pReader->SetCurrentPosition(GUID_NULL, var);
	ATLASSERT(SUCCEEDED(hr));
	PropVariantClear(&var);
}

HRESULT PropVariantToString(
	__in PROPVARIANT varPropVal,
	__in LPWSTR pwszPropName,
	__out LPWSTR pwszPropVal)
{
	HRESULT hr = S_OK;

	if (VT_UI8 == varPropVal.vt)
	{
		MFRatio mfRatio = { (UINT32)(varPropVal.uhVal.QuadPart >> 32), (UINT32)(varPropVal.uhVal.QuadPart) };
		if (0 == wcscmp(L"MF_MT_FRAME_SIZE", pwszPropName))
		{
			StringCchPrintf(
				pwszPropVal,
				MAX_LEN_ONELINE,
				L"%lux%lu",
				mfRatio.Numerator,
				mfRatio.Denominator);
		} else if (0 == wcscmp(L"MF_MT_FRAME_RATE", pwszPropName))
		{
			if (0 != mfRatio.Denominator)
			{
				StringCchPrintf(
					pwszPropVal,
					MAX_LEN_ONELINE,
					L"%3.5ffps",
					(float)mfRatio.Numerator / (float)mfRatio.Denominator);
			} else
			{
				StringCchPrintf(
					pwszPropVal,
					MAX_LEN_ONELINE,
					L"%lu/%lu",
					mfRatio.Numerator,
					mfRatio.Denominator);
			}
		} else if (0 == wcscmp(L"MF_MT_PIXEL_ASPECT_RATIO", pwszPropName))
		{
			StringCchPrintf(
				pwszPropVal,
				MAX_LEN_ONELINE,
				L"%lu:%lu",
				mfRatio.Numerator,
				mfRatio.Denominator);
		} else if (0 == wcscmp(L"MF_PD_DURATION", pwszPropName) ||
			0 == wcscmp(L"MF_PD_ASF_FILEPROPERTIES_PLAY_DURATION", pwszPropName) ||
			0 == wcscmp(L"MF_PD_ASF_FILEPROPERTIES_SEND_DURATION", pwszPropName))
		{
			CHECK_HR(hr = CDumperHelper::TimeToString(
				varPropVal.uhVal,
				pwszPropVal));
		} else
		{
			CHECK_HR(hr = CDumperHelper::PropVariantToString(
				varPropVal,
				pwszPropVal));
		}
	} else if (VT_CLSID == varPropVal.vt)
	{
		WCHAR pwszValGUID[MAX_LEN_ONELINE] = L"";
		CHECK_HR(hr = CDumperHelper::MFGUIDToString(
			*(varPropVal.puuid),
			pwszValGUID));
		StringCchPrintf(
			pwszPropVal,
			MAX_LEN_ONELINE,
			L"%ls",
			pwszValGUID);
	} else if ((VT_VECTOR | VT_UI1) == varPropVal.vt)
	{
		if (0 == wcscmp(L"MF_PD_ASF_FILEPROPERTIES_CREATION_TIME", pwszPropName) ||
			0 == wcscmp(L"MF_PD_LAST_MODIFIED_TIME", pwszPropName))
		{
			ULARGE_INTEGER ul = *(ULARGE_INTEGER*)(varPropVal.caub.pElems);
			FILETIME ft;
			ft.dwHighDateTime = ul.HighPart;
			ft.dwLowDateTime = ul.LowPart;
			CHECK_HR(hr = CDumperHelper::FileTimeToString(
				&ft,
				pwszPropVal));
		} else
		{
			CHECK_HR(hr = CDumperHelper::HexToString(
				varPropVal,
				pwszPropVal));
		}
	} else if (VT_UI4 == varPropVal.vt)
	{
		if (0 == wcscmp(L"MF_MT_INTERLACE_MODE", pwszPropName))
		{
			CHECK_HR(CDumperHelper::VideoInterlaceModeToString(
				(MFVideoInterlaceMode)varPropVal.ulVal,
				pwszPropVal));
		} else if (0 == wcscmp(L"MF_MT_MPEG2_LEVEL", pwszPropName))
		{
			CHECK_HR(CDumperHelper::MPEG2LevelToString(
				(eAVEncH264VLevel)varPropVal.ulVal,
				pwszPropVal));
		} else if (0 == wcscmp(L"MF_MT_MPEG2_PROFILE", pwszPropName))
		{
			CHECK_HR(CDumperHelper::MPEG2ProfileToString(
				(eAVEncH264VProfile)varPropVal.ulVal,
				pwszPropVal));
		} else
		{
			CHECK_HR(hr = CDumperHelper::PropVariantToString(
				varPropVal,
				pwszPropVal));
		}
	} else
	{
		CHECK_HR(hr = CDumperHelper::PropVariantToString(
			varPropVal,
			pwszPropVal));
	}

	return hr;
}

std::wstring PrintMFAttributes(IMFAttributes* pAttrs)
{
	std::wstring text;
	HRESULT hr = S_OK;

	GUID itemGUID = GUID_NULL;
	PROPVARIANT varValue;
	WCHAR pwszAttrName[MAX_LEN_ONELINE] = L"";
	UINT32 cAttributes = 0;
	WCHAR pwszPropVal[MAX_LEN_MULTILINE] = L"";

	PropVariantInit(&varValue);

	hr = pAttrs->GetCount(&cAttributes);

	// Go through attributes
	for (DWORD i = 0; i < cAttributes; i++)
	{
		PropVariantClear(&varValue);
		if ('\0' != pwszAttrName[0])
		{
			pwszAttrName[0] = '\0';
		}
		if ('\0' != pwszPropVal[0])
		{
			pwszPropVal[0] = '\0';
		}
		hr = pAttrs->GetItemByIndex(i,
			&itemGUID,
			&varValue);
		if (S_OK != hr)
		{
			break;
		}

		hr = CDumperHelper::MFGUIDToString(
			itemGUID,
			pwszAttrName);
		if (S_OK != hr)
		{
			break;
		}

		hr = PropVariantToString(
			varValue,
			pwszAttrName,
			pwszPropVal);
		if (S_OK != hr)
		{
			break;
		}
		text += pwszAttrName;
		text += L": ";
		text += pwszPropVal;
		text += L"\n";
		//CDumperHelper::PrintColor(
		//	COLOR_DEFAULT,
		//	L"        %ls: %ls\r\n",
		//	pwszAttrName,
		//	pwszPropVal);

	}

	PropVariantClear(&varValue);

	return text;
}

}	// namespace



bool	MFInitialize()
{
	int initializedCount = ++g_MFInitialized;
	if (initializedCount > 1) {
		return true;
	}

	HRESULT hRes = ::CoInitialize(NULL);
	ATLASSERT(SUCCEEDED(hRes));

	HRESULT hr = ::MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
	if (FAILED(hr)) {
		ATLASSERT(FALSE);
		ERROR_LOG << L"MFStartup failed hr: " << hr;
		return false;
	}
	return true;
}

void	MFFinalize()
{
	int initializedCount = --g_MFInitialized;
	if (initializedCount > 0) {
		return;
	}

	HRESULT hr = ::MFShutdown();
	ATLASSERT(SUCCEEDED(hr));
	::CoUninitialize();
}

///////////////////////////////////////////////////
// MediaInfo

std::wstring MediaInfo::GetMediaInfoText() const
{
	std::wstring mediaInfoText;

	double decimal, y;
	decimal = std::modf(ConvertSecFrom100ns(nsDuration), &y);
	int secDuration = static_cast<int>(y);
	int hour = secDuration / 3600;
	int min = (secDuration % 3600) / 60;
	double sec = ((secDuration % 3600) % 60) + decimal;
	mediaInfoText += (boost::wformat(L"play time: %1$02d:%2$02d:%3$02.3f\n") % hour % min % sec).str();

	if (videoStreamIndex != -1) {
		double fps = static_cast<double>(nume) / denom;
		mediaInfoText += (boost::wformat(
			L"videoStreamIndex: %1%\n"
			L"  fps: %2$.3f\n"
			L"  totalFrameCount: %3%\n"
			L"  FrameSize: %4%x%5%\n") 
			% videoStreamIndex % fps % totalFrameCount % imageFormat.biWidth % imageFormat.biHeight).str();
	}
	if (audioStreamIndex != -1) {
		mediaInfoText += (boost::wformat(
			L"audioStreamIndex: %1%\n"
			L"  nChannels: %2%\n"
			L"  nSamplesPerSec: %3%\n"
			L"  nAvgBytesPerSec: %4%\n"
			L"  nBlockAlign: %5%\n"
			L"  wBitsPerSample: %6%\n"
			L"  totalAudioSampleCount: %7%\n") 
			% audioStreamIndex % audioFormat.nChannels % audioFormat.nSamplesPerSec % audioFormat.nAvgBytesPerSec % audioFormat.nBlockAlign % audioFormat.wBitsPerSample % totalAudioSampleCount).str();
	}
	return mediaInfoText;
}


///////////////////////////////////////////////////
// SampleCache


void SampleCache::ResetVideo()
{
	m_videoCircularBuffer.clear();
	m_videoCircularBuffer.set_capacity(4);
}

void SampleCache::ResetAudio(WORD nBlockAlign)
{
	m_nBlockAlign = nBlockAlign;
	m_audioCircularBuffer.clear();
	m_audioCircularBuffer.set_capacity(kMaxAudioBufferSize);
}

void SampleCache::AddFrameSample(int frame, IMFSample* pSample)
{
	const int lastFrameNum = LastFrameNumber();
	if (lastFrameNum != -1) {
		if (std::abs((lastFrameNum + 1) - frame) > kFrameWaringGapCount) {
 			WARN_LOG << L"frame error - frame: " << frame << L" actual frame: " << (lastFrameNum + 1);
		}
#if 0
		if (frame != (lastFrameNum + 1)) {
			INFO_LOG << L"frame error - frame: " << frame << L" actual frame: " << (lastFrameNum + 1);
		}
#endif
		frame = lastFrameNum + 1;
	}

	VideoCache videoCache;
	videoCache.frame = frame;
	videoCache.spSample = pSample;
	m_videoCircularBuffer.push_back(videoCache);
}

void SampleCache::AddAudioSample(int startSample, IMFSample* pSample)
{
	const int lastAudioSampleNum = LastAudioSampleNumber();
	if (lastAudioSampleNum != -1) {
		const int actualAudioSampleNum = lastAudioSampleNum + m_audioCircularBuffer.back().audioSampleCount;
		if (std::abs(startSample  - actualAudioSampleNum) > kAudioSampleWaringGapCount) {
			WARN_LOG << L"sample laggin - lag: " << (startSample - actualAudioSampleNum) << L" startSample: " << startSample
						<< L" lastAudioSampleNum: " << actualAudioSampleNum;
		}

		startSample = lastAudioSampleNum + m_audioCircularBuffer.back().audioSampleCount;
	}

	DWORD totalLength = 0;
	HRESULT hr = pSample->GetTotalLength(&totalLength);
	IF_FAILED_THROW(hr);
	int audioSampleCount = totalLength / m_nBlockAlign;
	ATLASSERT((totalLength % m_nBlockAlign) == 0);

	AudioCache audioCache;
	audioCache.startSampleNum = startSample;
	audioCache.spSample = pSample;
	audioCache.audioSampleCount = audioSampleCount;
	m_audioCircularBuffer.push_back(audioCache);
}

int SampleCache::LastFrameNumber() const
{
	if (m_videoCircularBuffer.size() > 0u) {
		const VideoCache& prevVideoCache = m_videoCircularBuffer.back();
		return prevVideoCache.frame;
	}
	return -1;
}

int SampleCache::LastAudioSampleNumber() const
{
	if (m_audioCircularBuffer.size() > 0u) {
		const AudioCache& prevAudioCache = m_audioCircularBuffer.back();
		return prevAudioCache.startSampleNum;
	}
	return -1;
}

IMFSample* SampleCache::SearchFrameSample(int frame)
{
	for (auto it = m_videoCircularBuffer.rbegin(); it != m_videoCircularBuffer.rend(); ++it) {
		auto& videoCache = *it;
		if (videoCache.frame == frame) {
			return videoCache.spSample;
		}
	}
	return nullptr;
}

bool SampleCache::AudioCache::CopyBuffer(int& startSample, int& copySampleLength, BYTE*& buffer, WORD nBlockAlign)
{
	const int querySampleEndPos = startSample + copySampleLength;
	const int cacheSampleEndPos = startSampleNum + audioSampleCount;
	// キャッシュ内に startSample位置があるかどうか
	if (startSampleNum <= startSample && startSample < cacheSampleEndPos) {
		// 要求サイズがキャッシュを超えるかどうか
		if (querySampleEndPos <= cacheSampleEndPos) {
			// キャッシュ内に収まる
			const int actualBufferPos = (startSample - startSampleNum) * nBlockAlign;
			const int actualBufferSize = copySampleLength * nBlockAlign;
			SampleCopyToBuffer(spSample, buffer, actualBufferPos, actualBufferSize);

			startSample += copySampleLength;
			copySampleLength = 0;
			buffer += actualBufferSize;
			return true;

		} else {
			// 現在のキャッシュ内のデータをコピーする
			const int actualBufferPos = (startSample - startSampleNum) * nBlockAlign;
			const int leftSampleCount = cacheSampleEndPos - startSample;
			const int actualleftBufferSize = leftSampleCount * nBlockAlign;
			SampleCopyToBuffer(spSample, buffer, actualBufferPos, actualleftBufferSize);

			startSample += leftSampleCount;
			copySampleLength -= leftSampleCount;
			buffer += actualleftBufferSize;
			return true;
		}
	}
	return false;
}

bool SampleCache::SearchAudioSampleAndCopyBuffer(int startSample, int copySampleLength, BYTE* buffer)
{
	const size_t bufferSize = m_audioCircularBuffer.size();
	for (size_t i = 0; i < bufferSize; ++i) {
		auto& audioCache = m_audioCircularBuffer[i];
		if (audioCache.CopyBuffer(startSample, copySampleLength, buffer, m_nBlockAlign)) {
			if (copySampleLength == 0) {
				return true;
			}
		}
	}
	return false;
}



///////////////////////////////////////////////////
// MFVideoDecoder

bool MFVideoDecoder::Initialize(bool bUseDXVA2)
{
	INFO_LOG << L"MFVideoDecoder::Initialize - bUseDXVA2: " << bUseDXVA2;

	MFInitialize();

	m_bUseDXVA2 = bUseDXVA2;
	if (m_bUseDXVA2) {
		HRESULT hr = S_OK;
#if 0
		D3D_FEATURE_LEVEL d3dFeatureLevels[] = {
			D3D_FEATURE_LEVEL_11_1
		};
		D3D_FEATURE_LEVEL featureLevel;
		hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_VIDEO_SUPPORT, d3dFeatureLevels, 1, D3D11_SDK_VERSION, &m_spDevice, &featureLevel, NULL);
		ATLASSERT(SUCCEEDED(hr));
		CComQIPtr<ID3D10Multithread> spMultithread = m_spDevice.p;
		BOOL b = spMultithread->SetMultithreadProtected(TRUE);

		UINT resetToken = 0;
		hr = ::MFCreateDXGIDeviceManager(&resetToken, &m_spDevManager);
		ATLASSERT(SUCCEEDED(hr));
		hr = m_spDevManager->ResetDevice(m_spDevice.p, resetToken);
		ATLASSERT(SUCCEEDED(hr));
#endif
#if 1
		m_spDevManager.Attach(CreateD3DDevManager(GetDesktopWindow(), &m_spDevice));
		if (!m_spDevManager) {
			ERROR_LOG << L"CreateD3DDevManager failed";
			m_bUseDXVA2 = false;
		}
#endif
		hr = ::CoCreateInstance(CLSID_VideoProcessorMFT, nullptr, 
										CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_spVPMFTransform));
		if (FAILED(hr)) {
			ERROR_LOG << L"::CoCreateInstance(CLSID_VideoProcessorMFT failed";
			return false;
		}
	}

	return true;
}

void MFVideoDecoder::Finalize()
{
	INFO_LOG << L"MFVideoDecoder::Finalize";

	m_sampleCache.ResetVideo();
	m_sampleCache.ResetAudio(0);

	if (m_bUseDXVA2) {
		if (m_spMFOutBufferSample) {
			HRESULT hr = m_spVPMFTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
			IF_FAILED_ERRORLOG(hr);
			m_spMFOutBufferSample.Release();
		}
	}
	m_spVPMFTransform.Release();

	m_spDevice.Release();
	m_spDevManager.Release();

	//m_spSourceReader.Release();
	m_spVideoSourceReader.Release();
	m_spAudioSourceReader.Release();

	MFFinalize();
}

bool MFVideoDecoder::OpenMediaFile(const std::wstring& filePath)
{
	HRESULT hr = S_OK;
	CComPtr<IMFAttributes> spAttributes = NULL;
	UINT32 attributesSize = m_bUseDXVA2 ? 3 : 1;
	hr = ::MFCreateAttributes(&spAttributes, attributesSize);
	if (FAILED(hr)) {
		ERROR_LOG << L"MFCreateAttributes failed";
		return false;
	}
	if (m_bUseDXVA2) {
		hr = spAttributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, m_spDevManager);
		if (FAILED(hr)) {
			ERROR_LOG << L"SetUnknown(MF_SOURCE_READER_D3D_MANAGER failed";
			return false;
		}
		hr = spAttributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, FALSE);
		if (FAILED(hr)) {
			ERROR_LOG << L"SetUINT32(MF_SOURCE_READER_DISABLE_DXVA failed";
			return false;
		}
		hr = spAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
		if (FAILED(hr)) {
			ERROR_LOG << L"SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING failed";
			return false;
		}
	} else {
		hr = spAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
		if (FAILED(hr)) {
			ERROR_LOG << L"SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING failed";
			return false;
		}
	}

	//LPCWSTR videoPath = LR"(G:\共有フォルダ\新しいフォルダー\2020-01-17_04-59-36_.mkv)";
	CComPtr<IMFSourceReader> spReader;
#if 0
	CComPtr<IStream> spFileStream;
	hr = ::SHCreateStreamOnFile(filePath.c_str(), STGM_READ, &spFileStream);
	if (FAILED(hr)) {
		INFO_LOG << L"::SHCreateStreamOnFile(filePath.c_str() failed filePath: " << filePath;
		return false;
	}
	CComPtr<IMFByteStream> spByteStream;
	hr = ::MFCreateMFByteStreamOnStream(spFileStream, &spByteStream);
	IF_FAILED_THROW(hr);

	hr = ::MFCreateSourceReaderFromByteStream(spByteStream, spAttributes, &m_spSourceReader);
#else
	hr = ::MFCreateSourceReaderFromURL(filePath.c_str(), spAttributes, &spReader);
#endif
	if (FAILED(hr)) {
		INFO_LOG << L"MFCreateSourceReaderFromURL failed hr: " << hr << L" filePath: " << filePath;
		return false;
	}

	// 出力形式指定
	hr = ConfigureDecoder(spReader, MF_SOURCE_READER_FIRST_VIDEO_STREAM);
	hr = ConfigureDecoder(spReader, MF_SOURCE_READER_FIRST_AUDIO_STREAM);

	bool success = CheckMediaInfo(spReader);
	if (!success) {
		ERROR_LOG << L"CheckMediaInfo failed";
		return false;
	}
	spReader.Release();

	if (m_mediaInfo.videoStreamIndex != -1) {
		hr = ::MFCreateSourceReaderFromURL(filePath.c_str(), spAttributes, &m_spVideoSourceReader);
		IF_FAILED_THROW(hr);
		SelectStream(m_spVideoSourceReader, MFMediaType_Video);

		hr = ConfigureDecoder(m_spVideoSourceReader, m_mediaInfo.videoStreamIndex);
		if (FAILED(hr)) {
			ERROR_LOG << L"ConfigureDecoder(m_spVideoSourceReader, m_mediaInfo.videoStreamIndex) failed";
			m_mediaInfo.videoStreamIndex = -1;
		}
		m_sampleCache.ResetVideo();
	}
	if (m_mediaInfo.audioStreamIndex != -1) {
		hr = ::MFCreateSourceReaderFromURL(filePath.c_str(), spAttributes, &m_spAudioSourceReader);
		IF_FAILED_THROW(hr);
		SelectStream(m_spAudioSourceReader, MFMediaType_Audio);

		hr = ConfigureDecoder(m_spAudioSourceReader, m_mediaInfo.audioStreamIndex);
		if (FAILED(hr)) {
			ERROR_LOG << L"ConfigureDecoder(m_spAudioSourceReader, m_mediaInfo.audioStreamIndex) failed";
			m_mediaInfo.audioStreamIndex = -1;
		}
		m_sampleCache.ResetAudio(m_mediaInfo.audioFormat.nBlockAlign);
	}

	if (m_mediaInfo.videoStreamIndex == -1 && m_mediaInfo.audioStreamIndex == -1) {
		INFO_LOG << L"ファイルには映像も音声も含まれていません";
		return false;
	}

	if (m_bUseDXVA2 && m_spVideoSourceReader) {
#if 0
		if (!AdjustD3DDeviceBackBufferDimensions(
			m_mediaInfo.imageFormat.biWidth, 
			m_mediaInfo.imageFormat.biHeight,
			m_spDevice.p, 
			GetDesktopWindow())) 
		{
			ERROR_LOG << L"AdjustD3DDeviceBackBufferDimensions failed";
			return false;
		}
#endif
		// Send a message to the decoder to tell it to use DXVA2.
		CComPtr<IMFTransform> spVideoDecoder;
		hr = m_spVideoSourceReader->GetServiceForStream(m_mediaInfo.videoStreamIndex, GUID_NULL, IID_PPV_ARGS(&spVideoDecoder));
		if (FAILED(hr)) {
			ERROR_LOG << L"GetServiceForStream(MF_SOURCE_READER_FIRST_VIDEO_STREAM failed";
			return false;
		}
		hr = spVideoDecoder->ProcessMessage(
			MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(m_spDevManager.p));
		if (FAILED(hr)) {
			m_bUseDXVA2 = false;
			WARN_LOG << L"ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER failed";
			//ERROR_LOG << L"ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER failed";
			//return false;
		} else {
			ChangeColorConvertSettingAndCreateBuffer();
		}
	}

	TestFirstReadSample();

	return true;
}

int MFVideoDecoder::ReadFrame(const int frame, BYTE* buf)
{
	if (m_mediaInfo.videoStreamIndex == -1) {
		return 0;
	}
	ATLASSERT(m_mediaInfo.videoStreamIndex != -1);

#if 0
	const int firstFrameNum = ConvertFrameFromTimeStamp(m_firstVideoTimeStamp, m_mediaInfo.nume, m_mediaInfo.denom);
	if (frame < firstFrameNum) {
		INFO_LOG << L"ReadFrame: discard frame - frame: " << frame;
		return 0;
	}
#endif
	auto funcCopyBuffer = [this, buf](IMFSample* pSample) -> int {
		IMFSample* pYUY2Sample = pSample;
		if (m_bUseDXVA2 && m_spMFOutBufferSample) {
			ConvertColor(pSample);
			pYUY2Sample = m_spMFOutBufferSample;
		}
		DWORD copySize = SampleCopyToBuffer(pYUY2Sample, buf, m_mediaInfo.outImageBufferSize);
		return static_cast<int>(copySize);
	};
#if 1
	if (auto spSample = m_sampleCache.SearchFrameSample(frame)) {
		//INFO_LOG << L"Sample cache found!";
		return funcCopyBuffer(spSample);
	}
#endif
	int currentFrame = m_sampleCache.LastFrameNumber();
	if (currentFrame == -1) {
		currentFrame = ConvertFrameFromTimeStamp(m_currentVideoTimeStamp, m_mediaInfo.nume, m_mediaInfo.denom);
	}
	if (frame < currentFrame || (currentFrame + kThresholdFrameCount) < frame) {
		LONGLONG destTimePosition = ConvertTimeStampFromFrame(frame, m_mediaInfo.nume, m_mediaInfo.denom);
		SeekVideo(destTimePosition);
		INFO_LOG << L"ReadFrame Seek currentFrame: " << currentFrame << L" destFrame: " << frame << L" - destTimePos: " << ConvertSecFrom100ns(destTimePosition) << L" relativeFrame: " << (frame - currentFrame);
	}

	HRESULT hr = S_OK;
	int skipCount = 0;
	auto spSample = ReadSample(m_mediaInfo.videoStreamIndex, hr);
	while (spSample && SUCCEEDED(hr)) {
		const int readSampleFrame = m_sampleCache.LastFrameNumber();
#if 0
		if (skipCount == 0 && frame < readSampleFrame) {
			// シークをしても読み込んだフレームが目標のフレームを追い越していた場合
			WARN_LOG << L"ReadFrame: skipCount == 0 && frame < readSampleFrame - frame: " << frame << L" readSampleFrame: " << readSampleFrame;
			LONGLONG destTimePosition = ConvertTimeStampFromFrame(frame, m_mediaInfo.nume, m_mediaInfo.denom);
			SeekVideo(destTimePosition);
			INFO_LOG << L"ReadFrame Seek currentFrame: " << currentFrame << L" destFrame: " << frame << L" - destTimePos: " << ConvertSecFrom100ns(destTimePosition) << L" relativeFrame: " << (frame - currentFrame);
			return 0;
		}
#endif
		if (frame <= readSampleFrame) {
			if ((readSampleFrame - frame) > 0) {
				WARN_LOG << L"wrong frame currentFrame: " << currentFrame << " targetFrame: " << frame 
					<< L" readSampleFrame: " << readSampleFrame << L" distance: " << (readSampleFrame - frame);
			}
			if (skipCount > 0) {
				//INFO_LOG << L"ReadFrame skipCount: " << skipCount;
			}
			return funcCopyBuffer(spSample);
		}
		spSample = ReadSample(m_mediaInfo.videoStreamIndex, hr);
		++skipCount;
	}
	return 0;
}

int MFVideoDecoder::ReadAudio(int start, int length, void* buf)
{
#if 0
	const int firstAudioSampleNum = ConvertSampleFromTimeStamp(m_firstAudioTimeStamp, m_mediaInfo.audioFormat.nSamplesPerSec);
	if (start < firstAudioSampleNum) {
		INFO_LOG << L"ReadAudio: discard sample - start: " << start;
		return 0;
	}
#endif

	bool hitCache = m_sampleCache.SearchAudioSampleAndCopyBuffer(start, length, static_cast<BYTE*>(buf));
	if (hitCache) {
		//INFO_LOG << L"ReadAudio cache hit! start: " << start;
		return length;
	}

	int currentSample = m_sampleCache.LastAudioSampleNumber();
	if (currentSample == -1) {
		currentSample = ConvertSampleFromTimeStamp(m_currentAudioTimeStamp, m_mediaInfo.audioFormat.nSamplesPerSec);
	}
	if (start < currentSample || (currentSample + kThresholdSampleCount) < start) {
		LONGLONG destTimePosition = ConvertTimeStampFromSample(start, m_mediaInfo.audioFormat.nSamplesPerSec);
		SeekAudio(destTimePosition);
		INFO_LOG << L"ReadAudio Seek currentTimestamp: " << ConvertSecFrom100ns(m_currentAudioTimeStamp) << L" - destTimePos: " << ConvertSecFrom100ns(destTimePosition) << L" relativeSample: " << (start - currentSample);
	}

 	HRESULT hr = S_OK;
	int skipCount = 0;
	int nohitChacheCount = 0;
	auto spSample = ReadSample(m_mediaInfo.audioStreamIndex, hr);
	while (spSample && SUCCEEDED(hr)) {
		const int readSampleNum = m_sampleCache.LastAudioSampleNumber();
#if 0
		if (skipCount == 0 && start < readSampleNum) {
			// シークをしても読み込んだサンプルが目標のサンプルを追い越していた場合
			WARN_LOG << L"ReadAudio: skipCount == 0 && start < readSampleNum - start: " << start << L" readSampleNum: " << readSampleNum;
			LONGLONG destTimePosition = ConvertTimeStampFromSample(start, m_mediaInfo.audioFormat.nSamplesPerSec);
			SeekAudio(destTimePosition);
			INFO_LOG << L"ReadAudio Seek currentTimestamp: " << ConvertSecFrom100ns(m_currentAudioTimeStamp) << L" - destTimePos: " << ConvertSecFrom100ns(destTimePosition) << L" relativeSample: " << (start - currentSample);
			return 0;
		}
#endif
		if (start <= readSampleNum) {
			if (m_sampleCache.SearchAudioSampleAndCopyBuffer(start, length, static_cast<BYTE*>(buf))) {
				if (skipCount > 0) {
					//INFO_LOG << L"ReadAudio skipCount: " << skipCount;
				}
				return length;
			} else {
				if (nohitChacheCount > 1) {
					//ERROR_LOG << L"nohitChacheCount > 1 : " << nohitChacheCount;
				}
				++nohitChacheCount;
			}
		}
		spSample = ReadSample(m_mediaInfo.audioStreamIndex, hr);
		++skipCount;
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Converts a rectangle from one pixel aspect ratio (PAR) to another PAR.
// Returns the corrected rectangle.
//
// For example, a 720 x 486 rect with a PAR of 9:10, when converted to 1x1 PAR,
// must be stretched to 720 x 540.
//-----------------------------------------------------------------------------

RECT CorrectAspectRatio(const RECT& src, const MFRatio& srcPAR, const MFRatio& destPAR)
{
	// Start with a rectangle the same size as src, but offset to (0,0).
	RECT rc = { 0, 0, src.right - src.left, src.bottom - src.top };

	// If the source and destination have the same PAR, there is nothing to do.
	// Otherwise, adjust the image size, in two steps:
	//  1. Transform from source PAR to 1:1
	//  2. Transform from 1:1 to destination PAR.

	if ((srcPAR.Numerator != destPAR.Numerator) ||
		(srcPAR.Denominator != destPAR.Denominator))
	{
		// Correct for the source's PAR.

		if (srcPAR.Numerator > srcPAR.Denominator)
		{
			// The source has "wide" pixels, so stretch the width.
			rc.right = MulDiv(rc.right, srcPAR.Numerator, srcPAR.Denominator);
		} else if (srcPAR.Numerator < srcPAR.Denominator)
		{
			// The source has "tall" pixels, so stretch the height.
			rc.bottom = MulDiv(rc.bottom, srcPAR.Denominator, srcPAR.Numerator);
		}
		// else: PAR is 1:1, which is a no-op.

		// Next, correct for the target's PAR. This is the inverse operation of 
		// the previous.

		if (destPAR.Numerator > destPAR.Denominator)
		{
			// The destination has "wide" pixels, so stretch the height.
			rc.bottom = MulDiv(rc.bottom, destPAR.Numerator, destPAR.Denominator);
		} else if (destPAR.Numerator < destPAR.Denominator)
		{
			// The destination has "tall" pixels, so stretch the width.
			rc.right = MulDiv(rc.right, destPAR.Denominator, destPAR.Numerator);
		}
		// else: PAR is 1:1, which is a no-op.
	}
	return rc;
}

bool MFVideoDecoder::CheckMediaInfo(IMFSourceReader* pReader)
{
	HRESULT hr = S_OK;

	m_mediaInfo.videoStreamIndex = -1;
	m_mediaInfo.audioStreamIndex = -1;
	for (DWORD streamIndex = 0; SUCCEEDED(hr); ++streamIndex) {
		CComPtr<IMFMediaType> currentMediaType;
		hr = pReader->GetCurrentMediaType(streamIndex, &currentMediaType);
		if (FAILED(hr)) {
			break;
		}
		BOOL selected = FALSE;
		pReader->GetStreamSelection(streamIndex, &selected);
		if (!selected) {
			continue;
		}

		GUID majorType;
		hr = currentMediaType->GetMajorType(&majorType);
		IF_FAILED_THROW(hr);
		if (::IsEqualGUID(majorType, MFMediaType_Video)) {
			m_mediaInfo.videoStreamIndex = streamIndex;
		} else if (::IsEqualGUID(majorType, MFMediaType_Audio)) {
			m_mediaInfo.audioStreamIndex = streamIndex;
		} else {
			ATLASSERT(FALSE);
		}
	}

	// 再生時間取得
	if (m_mediaInfo.videoStreamIndex != -1 || m_mediaInfo.audioStreamIndex != -1) {
		PROPVARIANT var;
		hr = pReader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var);
		if (FAILED(hr)) {
			ERROR_LOG << L"GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION failed";
			return false;
		}
		hr = ::PropVariantToInt64(var, &m_mediaInfo.nsDuration);
		PropVariantClear(&var);
	} else {
		ERROR_LOG << L"ファイルには映像も音声も存在しません";
		return false;
	}
	if (m_mediaInfo.videoStreamIndex != -1) {	// video
		BITMAPINFOHEADER bih = { sizeof(BITMAPINFOHEADER) };

		CComPtr<IMFMediaType> mediaType;
		hr = pReader->GetCurrentMediaType(m_mediaInfo.videoStreamIndex, &mediaType);
		IF_FAILED_THROW(hr);

		MFVIDEOFORMAT* pmfVideoFormat;
		UINT32 mfVideoFormatSize;
		hr = ::MFCreateMFVideoFormatFromMFMediaType(mediaType, &pmfVideoFormat, &mfVideoFormatSize);
		IF_FAILED_THROW(hr);

		bih.biWidth = pmfVideoFormat->videoInfo.dwWidth;
		bih.biHeight = pmfVideoFormat->videoInfo.dwHeight;

		RECT rcSrc = { 0, 0, static_cast<LONG>(bih.biWidth), static_cast<LONG>(bih.biHeight) };
		RECT destRect = CorrectAspectRatio(rcSrc, pmfVideoFormat->videoInfo.PixelAspectRatio, { 1 , 1 });
		bih.biWidth = destRect.right;
		bih.biHeight = destRect.bottom;
			
		m_mediaInfo.nume = pmfVideoFormat->videoInfo.FramesPerSecond.Numerator;
		m_mediaInfo.denom = pmfVideoFormat->videoInfo.FramesPerSecond.Denominator;

		::CoTaskMemFree(pmfVideoFormat);

		GUID subType = {};
		hr = mediaType->GetGUID(MF_MT_SUBTYPE, &subType);
		if (FAILED(hr)) {
			ERROR_LOG << L"GetGUID(MF_MT_SUBTYPE failed";
			return false;
		}
		//if (subType == MFVideoFormat_YUY2) {
			bih.biCompression = FCC('YUY2');
			bih.biBitCount = 16;
		//} else {
		//	ERROR_LOG << L"subType != MFVideoFormat_YUY2";
		//	return false;
		//}
		m_mediaInfo.imageFormat = bih;
		m_mediaInfo.totalFrameCount = 
			ConvertFrameFromTimeStamp(m_mediaInfo.nsDuration, m_mediaInfo.nume, m_mediaInfo.denom);
		m_mediaInfo.outImageBufferSize = bih.biWidth * bih.biHeight * (bih.biBitCount / 8);
	}
	if (m_mediaInfo.audioStreamIndex != -1) {	// audio
		CComPtr<IMFMediaType> mediaType;
		hr = pReader->GetCurrentMediaType(m_mediaInfo.audioStreamIndex, &mediaType);
		IF_FAILED_THROW(hr);

		WAVEFORMATEX* pwfm;
		UINT32 wfmSize;
		hr = ::MFCreateWaveFormatExFromMFMediaType(mediaType, &pwfm, &wfmSize);
		IF_FAILED_THROW(hr);

		pwfm->wFormatTag = WAVE_FORMAT_PCM;

		m_mediaInfo.audioFormat = *pwfm;
		::CoTaskMemFree(pwfm);

		m_mediaInfo.totalAudioSampleCount =
			ConvertSampleFromTimeStamp(m_mediaInfo.nsDuration, m_mediaInfo.audioFormat.nSamplesPerSec);
	}

	INFO_LOG << L"ChechMediaInfo: \n" << m_mediaInfo.GetMediaInfoText();
	return true;
}

void MFVideoDecoder::ChangeColorConvertSettingAndCreateBuffer()
{	
	m_spMFOutBufferSample.Release();

	if (!m_bUseDXVA2) {
		return;
	}

	ATLASSERT(m_spVPMFTransform);

	HRESULT hr = S_OK;
	CComPtr<IMFMediaType> mediaType;
	hr = m_spVideoSourceReader->GetCurrentMediaType(m_mediaInfo.videoStreamIndex, &mediaType);
	IF_FAILED_THROW(hr);

	//INFO_LOG << L"m_spVideoSourceReader->GetCurrentMediaType: \n" << PrintMFAttributes(mediaType);

	GUID subType = {};
	hr = mediaType->GetGUID(MF_MT_SUBTYPE, &subType);

	WCHAR subTypeText[MAX_LEN_ONELINE] = L"";
	CDumperHelper::MFGUIDToString(subType, subTypeText);
	INFO_LOG << L"GetCurrentMediaType subType: " << subTypeText;

	UINT32 pixelNume, pixelDenom;
	hr = ::MFGetAttributeRatio(mediaType, MF_MT_PIXEL_ASPECT_RATIO, &pixelNume, &pixelDenom);
	IF_FAILED_THROW(hr);

	UINT32 width, height;
	hr = ::MFGetAttributeSize(mediaType, MF_MT_FRAME_SIZE, &width, &height);
	IF_FAILED_THROW(hr);

	RECT rcSrc = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
	MFRatio srcPAR = { pixelNume, pixelDenom };
	RECT destRect = CorrectAspectRatio(rcSrc, srcPAR, { 1 , 1 });
	UINT32 destWidth = destRect.right;
	UINT32 destHeight = destRect.bottom;

	// 高さが16の倍数、幅が2の倍数になるように調節する
	enum { 
		kAlignHeightSize = 16,
		kAlignWidth = 2,
	};
	auto funcAlign = [](UINT32 value, UINT32 align) -> UINT32 {
		if (value % align) {
			UINT32 alignedValue = ((value / align) * align) + align;
			value = alignedValue;
		}
		return value;
	};
	height = funcAlign(height, kAlignHeightSize);
	destHeight = funcAlign(destHeight, kAlignHeightSize);
	destWidth = funcAlign(destWidth, kAlignWidth);
	
	// 色変換 NV12 -> YUY2
	// 動画の幅は 2の倍数、高さは 16の倍数でないとダメっぽい？
	CComPtr<IMFMediaType> spInputMediaType;
	hr = ::MFCreateMediaType(&spInputMediaType);
	IF_FAILED_THROW(hr);
	spInputMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	spInputMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
	spInputMediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE); // UnCompressed
	spInputMediaType->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE); // UnCompressed
	::MFSetAttributeRatio(spInputMediaType, MF_MT_PIXEL_ASPECT_RATIO, pixelNume, pixelDenom);
	MFSetAttributeSize(spInputMediaType, MF_MT_FRAME_SIZE, width, height);
	hr = m_spVPMFTransform->SetInputType(0, spInputMediaType, 0);
	IF_FAILED_THROW(hr);

	CComPtr<IMFMediaType> spOutputMediaType;
	hr = ::MFCreateMediaType(&spOutputMediaType);
	IF_FAILED_THROW(hr);
	spOutputMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	spOutputMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_YUY2);
	spOutputMediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE); // UnCompressed
	spOutputMediaType->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE); // UnCompressed
	::MFSetAttributeRatio(spInputMediaType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	MFSetAttributeSize(spOutputMediaType, MF_MT_FRAME_SIZE, destWidth, destHeight);
	hr = m_spVPMFTransform->SetOutputType(0, spOutputMediaType, 0);
	IF_FAILED_THROW(hr);

	hr = m_spVPMFTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
	
	hr = m_spVPMFTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
	IF_FAILED_THROW(hr);

	// 出力先IMFSample作成
	MFT_OUTPUT_STREAM_INFO streamInfo = {};
	hr = m_spVPMFTransform->GetOutputStreamInfo(0, &streamInfo);
	IF_FAILED_THROW(hr);

	hr = ::MFCreateSample(&m_spMFOutBufferSample);
	IF_FAILED_THROW(hr);
	CComPtr<IMFMediaBuffer> spMediaBuffer;
	hr = ::MFCreateMemoryBuffer(streamInfo.cbSize, &spMediaBuffer);
	IF_FAILED_THROW(hr);
	hr = m_spMFOutBufferSample->AddBuffer(spMediaBuffer);
	IF_FAILED_THROW(hr);
}

void MFVideoDecoder::TestFirstReadSample()
{
	HRESULT hr = S_OK;
	LONGLONG firstVideoTimeStamp = 0;
	LONGLONG firstAudioTimeStamp = 0;
	m_firstGapTimeStamp;
	if (m_mediaInfo.videoStreamIndex != -1) {
		firstVideoTimeStamp = 0;
		auto spSample = ReadSample(m_mediaInfo.videoStreamIndex, hr);
		if (FAILED(hr) || !spSample) {
			IF_FAILED_THROW(hr);
		}
		INFO_LOG << L"TestFirstReadSample m_firstVideoTimeStamp: " << m_currentVideoTimeStamp << L" (" << ConvertSecFrom100ns(m_currentVideoTimeStamp) << L")";
		firstVideoTimeStamp = m_currentVideoTimeStamp;
		SeekVideo(0);
		m_currentVideoTimeStamp = 0;
	}
	if (m_mediaInfo.audioStreamIndex != -1) {
		firstAudioTimeStamp = 0;
		auto spSample = ReadSample(m_mediaInfo.audioStreamIndex, hr);
		if (FAILED(hr) || !spSample) {
			IF_FAILED_THROW(hr);
		}
		INFO_LOG << L"TestFirstReadSample m_firstAudioTimeStamp: " << m_currentAudioTimeStamp << L" (" << ConvertSecFrom100ns(m_currentAudioTimeStamp) << L")";
		firstAudioTimeStamp = m_currentAudioTimeStamp;
		SeekAudio(0);
		m_currentAudioTimeStamp = 0;
	}
	if (m_mediaInfo.videoStreamIndex != -1 && m_mediaInfo.audioStreamIndex != -1) {
		if (firstVideoTimeStamp != firstAudioTimeStamp) {
			//ATLASSERT(FALSE);
			WARN_LOG << L"fisrt timestamp gapped - firstVideoTimeStamp: " << firstVideoTimeStamp 
												<< L" firstAudioTimestamp: " << firstAudioTimeStamp;
		}
	}
	m_firstGapTimeStamp = std::max(firstVideoTimeStamp, firstAudioTimeStamp);
	INFO_LOG << L"TestFirstReadSample - m_firstGapTimeStamp: " << m_firstGapTimeStamp;
}

void MFVideoDecoder::SeekVideo(LONGLONG destTimePosition)
{
	m_sampleCache.ResetVideo();
	Seek(m_spVideoSourceReader, destTimePosition);
}

void MFVideoDecoder::SeekAudio(LONGLONG destTimePosition)
{
	m_sampleCache.ResetAudio(m_mediaInfo.audioFormat.nBlockAlign);
	Seek(m_spAudioSourceReader, destTimePosition);
}

CComPtr<IMFSample> MFVideoDecoder::ReadSample(DWORD streamIndex, HRESULT& hr)
{
	CComPtr<IMFSample> spSample;
	DWORD actualStreamIndex;
	DWORD flags;
	LONGLONG llTimeStamp;

	IMFSourceReader* pReader;
	if (streamIndex == m_mediaInfo.videoStreamIndex) {
		pReader = m_spVideoSourceReader;
	} else if (streamIndex == m_mediaInfo.audioStreamIndex) {
		pReader = m_spAudioSourceReader;
	} else {
		ATLASSERT(FALSE);
		ERROR_LOG << L"MFVideoDecoder::ReadSample: streamIndex is invalid";
		return nullptr;
	}

	hr = pReader->ReadSample(
		streamIndex /*MF_SOURCE_READER_ANY_STREAM*/,    // Stream index.
		0,                              // Flags.
		&actualStreamIndex,                   // Receives the actual stream index. 
		&flags,                         // Receives status flags.
		&llTimeStamp,                   // Receives the time stamp.
		&spSample                        // Receives the sample or NULL.
	);

	if (FAILED(hr)) {
		ERROR_LOG << L"ReadSample failed hr: " << hr;
		return nullptr;
	}
#ifdef _DEBUG_HYOUJISINAI
	if (actualStreamIndex == m_mediaInfo.videoStreamIndex) {
		double frame = (ConvertSecFrom100ns(llTimeStamp) * m_mediaInfo.nume) / m_mediaInfo.denom;
		ATLTRACE(L"Stream %d frame: %.3f (%.3f)\n", actualStreamIndex,
			frame/*ConvertFrameFromTimeStamp(llTimeStamp, m_mediaInfo.nume, m_mediaInfo.denom)*/,
			ConvertSecFrom100ns(llTimeStamp));
	} else if (actualStreamIndex == m_mediaInfo.audioStreamIndex) {
		ATLTRACE(L"Stream %d sample: %d (%.3f)\n", actualStreamIndex,
			ConvertSampleFromTimeStamp(llTimeStamp, m_mediaInfo.audioFormat.nSamplesPerSec), 
			ConvertSecFrom100ns(llTimeStamp));
	}
#endif

	if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
	{
		ATLTRACE(L"\tEnd of stream - stream: %d\n", streamIndex);
		return nullptr;
	}
	if (flags & MF_SOURCE_READERF_NEWSTREAM)
	{
		ATLTRACE(L"\tNew stream\n");
	}
	if (flags & MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)
	{
		ATLTRACE(L"\tNative type changed\n");
	}
	if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
	{
		ATLTRACE(L"\tCurrent type changed\n");
		if (actualStreamIndex == m_mediaInfo.videoStreamIndex) {
			ChangeColorConvertSettingAndCreateBuffer();
		} else {
			ATLASSERT(FALSE);
			ERROR_LOG << L"unknown CurrentMediaTypeChanged";
		}

	}
	if (flags & MF_SOURCE_READERF_STREAMTICK)
	{
		ATLTRACE(L"\tStream tick\n");
	}

	if (flags & MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)
	{
		// The format changed. Reconfigure the decoder.
		hr = ConfigureDecoder(pReader, actualStreamIndex);
		if (FAILED(hr)) {
			ATLASSERT(FALSE);
			ERROR_LOG << L"MFVideoDecoder::ReadSample MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED ConfigureDecoder failed";
			return nullptr;
		}
	}

	// success!
	// add cache
	llTimeStamp -= m_firstGapTimeStamp;
	if (actualStreamIndex == m_mediaInfo.videoStreamIndex) {
		//llTimeStamp -= m_firstVideoTimeStamp;
		const int frame = ConvertFrameFromTimeStamp(llTimeStamp, m_mediaInfo.nume, m_mediaInfo.denom);
		m_sampleCache.AddFrameSample(frame, spSample);
		m_currentVideoTimeStamp = llTimeStamp;

	} else if (actualStreamIndex == m_mediaInfo.audioStreamIndex) {
		//llTimeStamp -= m_firstAudioTimeStamp;
		int sampleNum = ConvertSampleFromTimeStamp(llTimeStamp, m_mediaInfo.audioFormat.nSamplesPerSec);
		m_sampleCache.AddAudioSample(sampleNum, spSample);
		m_currentAudioTimeStamp = llTimeStamp;
	} else {
		ATLASSERT(FALSE);
	}

	return spSample;
}

// NV12 -> YUY2
void MFVideoDecoder::ConvertColor(IMFSample* pSample)
{
	HRESULT hr = m_spVPMFTransform->ProcessInput(0, pSample, 0);
	IF_FAILED_THROW(hr);

	MFT_OUTPUT_DATA_BUFFER mftOutputDataBuffer = {};
	mftOutputDataBuffer.pSample = m_spMFOutBufferSample;
	DWORD dwStatus = 0;
	hr = m_spVPMFTransform->ProcessOutput(0, 1, &mftOutputDataBuffer, &dwStatus);
	IF_FAILED_THROW(hr);
}
