
#include "stdafx.h"
#include "MainDlg.h"

#include <cmath>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <evr.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <amvideo.h>
#include <d3d11.h>
#include <d3d9.h>
#include <dxva2api.h>
#include <Codecapi.h>
//#include "unknownbase.h"
#include "timer.h"

#pragma comment(lib,"mf.lib")
#pragma comment(lib,"mfplat.lib")
#pragma comment(lib,"mfuuid.lib")
#pragma comment(lib,"strmiids.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "Propsys.lib")
#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "Dxva2.lib")
#pragma comment(lib, "DXGI.lib")

UINT32 g_nume, g_denom;
UINT32 g_nSamplesPerSec;

CComPtr<IDirect3DDeviceManager9> g_dev_manager;

void GetVideoInfo(IMFSourceReader* reader, UINT32* width, UINT32* height, double* fps);


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

int ConvertFrameFromTimeStamp(LONGLONG nsTimeStamp)
{
	double frame = (ConvertSecFrom100ns(nsTimeStamp) * g_nume) / g_denom;
	int numFrame = static_cast<int>(std::round(frame));
	return numFrame;
}

LONGLONG ConvertTimeStampFromFrame(LONGLONG frame)
{
	double frameSec = (static_cast<double>(frame * g_denom) / g_nume);
	LONGLONG nsTime = Convert100nsFromSec(frameSec);
	return nsTime;
}

int ConvertSampleFromTimeStamp(LONGLONG nsTimeStamp)
{
	double sample = ConvertSecFrom100ns(nsTimeStamp)* g_nSamplesPerSec;
    int numSample = static_cast<int>(std::round(sample));
	return numSample;
}

HRESULT EnumerateTypesForStream(IMFSourceReader* pReader, DWORD dwStreamIndex)
{
	HRESULT hr = S_OK;
	DWORD dwMediaTypeIndex = 0;

	while (SUCCEEDED(hr))
	{
		IMFMediaType* pType = NULL;
		hr = pReader->GetNativeMediaType(dwStreamIndex, dwMediaTypeIndex, &pType);
		if (hr == MF_E_NO_MORE_TYPES)
		{
			hr = S_OK;
			break;
		} else if (SUCCEEDED(hr))
		{
			// Examine the media type. (Not shown.)

			pType->Release();
		}
		++dwMediaTypeIndex;
	}
	return hr;
}

HRESULT ConfigureDecoder(IMFSourceReader* pReader, DWORD dwStreamIndex)
{
	CComPtr<IMFMediaType> pNativeType = NULL;
	CComPtr<IMFMediaType> pType = NULL;

	// Find the native format of the stream.
	HRESULT hr = pReader->GetNativeMediaType(dwStreamIndex, 0, &pNativeType);
	if (FAILED(hr))
	{
		return hr;
	}

	GUID majorType, subtype;

	// Find the major type.
	hr = pNativeType->GetGUID(MF_MT_MAJOR_TYPE, &majorType);
	if (FAILED(hr))
	{
		goto done;
	}

	// Define the output type.
	hr = MFCreateMediaType(&pType);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = pType->SetGUID(MF_MT_MAJOR_TYPE, majorType);
	if (FAILED(hr))
	{
		goto done;
	}

	// Select a subtype.
	if (majorType == MFMediaType_Video)
	{
		subtype = MFVideoFormat_YUY2;//MFVideoFormat_RGB32;
	} else if (majorType == MFMediaType_Audio)
	{
		subtype = MFAudioFormat_PCM;
	} else
	{
		// Unrecognized type. Skip.
		goto done;
	}

	hr = pType->SetGUID(MF_MT_SUBTYPE, subtype);
	if (FAILED(hr))
	{
		goto done;
	}

	// Set the uncompressed format.
	hr = pReader->SetCurrentMediaType(dwStreamIndex, NULL, pType);
	if (FAILED(hr))
	{
		goto done;
	}

done:

	return hr;
}

void	TestReadSample(IMFSourceReader* pReader)
{
	DWORD streamIndex, flags;
	LONGLONG llTimeStamp;
	IMFSample* pSample = nullptr;

	HRESULT hr = pReader->ReadSample(
		MF_SOURCE_READER_FIRST_VIDEO_STREAM/*MF_SOURCE_READER_ANY_STREAM*/,    // Stream index.
		0,                              // Flags.
		&streamIndex,                   // Receives the actual stream index. 
		&flags,                         // Receives status flags.
		&llTimeStamp,                   // Receives the time stamp.
		&pSample                        // Receives the sample or NULL.
	);

	if (FAILED(hr))
	{
		//break;
	}

	ATLTRACE(L"Stream %d frame: %d (%.2f)\n", streamIndex, ConvertFrameFromTimeStamp(llTimeStamp), ConvertSecFrom100ns(llTimeStamp));
	//ATLTRACE(L"Stream %d sample: %d (%.2f)\n", streamIndex, ConvertSampleFromTimeStamp(llTimeStamp), ConvertSecFrom100ns(llTimeStamp));
	if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
	{
		ATLTRACE(L"\tEnd of stream\n");
		//quit = true;
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
	}
	if (flags & MF_SOURCE_READERF_STREAMTICK)
	{
		ATLTRACE(L"\tStream tick\n");
	}

	if (flags & MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)
	{
		// The format changed. Reconfigure the decoder.
		hr = ConfigureDecoder(pReader, streamIndex);
		if (FAILED(hr))
		{
			//break;
		}
	}

	if (pSample)
	{
		CComPtr<IMFMediaBuffer> spBuffer;
		pSample->ConvertToContiguousBuffer(&spBuffer);
		BYTE* pData = nullptr;
		DWORD currentLength = 0;
		spBuffer->Lock(&pData, nullptr, &currentLength);
		ATLTRACE(L"currentLength: %d\n", currentLength);
		spBuffer->Unlock();

		//++cSamples;
	}

	pSample->Release();
}

HRESULT ProcessSamples(IMFSourceReader* pReader)
{
	HRESULT hr = S_OK;
	CComPtr<IMFSample> pSample = NULL;
	size_t  cSamples = 0;



	CComPtr<IMFTransform> spVPMFT;
	hr = ::CoCreateInstance(CLSID_VideoProcessorMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&spVPMFT));

	CComPtr<IMFAttributes> spVPAttributes;
	spVPMFT->GetAttributes(&spVPAttributes);
	UINT32 dxva;
	hr = spVPAttributes->GetUINT32(MF_SA_D3D_AWARE, &dxva);

	CComPtr<IMFMediaType> spInputMediaType;
	::MFCreateMediaType(&spInputMediaType);
	spInputMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	spInputMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
	spInputMediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE); // UnCompressed
	spInputMediaType->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE); // UnCompressed
	MFSetAttributeSize(spInputMediaType, MF_MT_FRAME_SIZE, 1280, 720);

	hr = spVPMFT->SetInputType(0, spInputMediaType, 0);

	CComPtr<IMFMediaType> spOutputMediaType;
	::MFCreateMediaType(&spOutputMediaType);
	spOutputMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	spOutputMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_YUY2);
	spOutputMediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE); // UnCompressed
	spOutputMediaType->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE); // UnCompressed
	MFSetAttributeSize(spOutputMediaType, MF_MT_FRAME_SIZE, 1280, 720);
	hr = spVPMFT->SetOutputType(0, spOutputMediaType, 0);

	hr = spVPMFT->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);


	bool quit = false;
	while (!quit)
	{
		DWORD streamIndex, flags;
		LONGLONG llTimeStamp;

		hr = pReader->ReadSample(
			/*MF_SOURCE_READER_FIRST_VIDEO_STREAM*/MF_SOURCE_READER_ANY_STREAM,    // Stream index.
			0,                              // Flags.
			&streamIndex,                   // Receives the actual stream index. 
			&flags,                         // Receives status flags.
			&llTimeStamp,                   // Receives the time stamp.
			&pSample                        // Receives the sample or NULL.
		);

		if (FAILED(hr))
		{
			break;
		}

		ATLTRACE(L"Stream %d frame: %d (%.2f)\n", streamIndex, ConvertFrameFromTimeStamp(llTimeStamp), ConvertSecFrom100ns(llTimeStamp));
		//ATLTRACE(L"Stream %d sample: %d (%.2f)\n", streamIndex, ConvertSampleFromTimeStamp(llTimeStamp), ConvertSecFrom100ns(llTimeStamp));
		if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
		{
			ATLTRACE(L"\tEnd of stream\n");
			quit = true;
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
			UINT32 width, height;
			double fps;
			GetVideoInfo(pReader, &width, &height, &fps);
			int a = 0;

		}
		if (flags & MF_SOURCE_READERF_STREAMTICK)
		{
			ATLTRACE(L"\tStream tick\n");
		}

		if (flags & MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)
		{
			// The format changed. Reconfigure the decoder.
			hr = ConfigureDecoder(pReader, streamIndex);
			if (FAILED(hr))
			{
				break;
			}
		}

		if (pSample)
		{
#if 1

			MFT_OUTPUT_STREAM_INFO streamInfo = {};
			hr = spVPMFT->GetOutputStreamInfo(0, &streamInfo);

			CComPtr<IMFSample> spOutSample;
			hr = ::MFCreateSample(&spOutSample);
			CComPtr<IMFMediaBuffer> spMediaBuffer;
			hr = MFCreateMemoryBuffer(streamInfo.cbSize, &spMediaBuffer);
			hr = spOutSample->AddBuffer(spMediaBuffer);

			hr = spVPMFT->ProcessInput(0, pSample, 0);

			MFT_OUTPUT_DATA_BUFFER mftOutputDataBuffer = {};
			mftOutputDataBuffer.pSample = spOutSample.p;
			DWORD dwStatus = 0;
			hr = spVPMFT->ProcessOutput(0, 1, &mftOutputDataBuffer, &dwStatus);

			{
				CComPtr<IMFMediaBuffer> spBuffer;
				mftOutputDataBuffer.pSample->ConvertToContiguousBuffer(&spBuffer);
				BYTE* pData = nullptr;
				DWORD currentLength = 0;
				spBuffer->Lock(&pData, nullptr, &currentLength);
				ATLTRACE(L"currentLength: %d\n", currentLength);
				spBuffer->Unlock();
			}
#endif
#if 1
			CComPtr<IMFMediaBuffer> spBuffer;
			pSample->ConvertToContiguousBuffer(&spBuffer);
			BYTE* pData = nullptr;
			DWORD currentLength = 0;
			spBuffer->Lock(&pData, nullptr, &currentLength);
			ATLTRACE(L"currentLength: %d\n", currentLength);
			spBuffer->Unlock();
#endif

			++cSamples;
		}

		pSample.Release();
	}

	if (FAILED(hr))
	{
		ATLTRACE(L"ProcessSamples FAILED, hr = 0x%x\n", hr);
	} else
	{
		ATLTRACE(L"Processed %d samples\n", cSamples);
	}
	pSample.Release();;
	return hr;
}

HRESULT GetDuration(IMFSourceReader* pReader, LONGLONG* phnsDuration)
{
	PROPVARIANT var;
	HRESULT hr = pReader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE,
		MF_PD_DURATION, &var);
	if (SUCCEEDED(hr))
	{
		hr = ::PropVariantToInt64(var, phnsDuration);
		PropVariantClear(&var);
	}
	return hr;
}

void GetVideoInfo(IMFSourceReader* reader, UINT32* width, UINT32* height, double* fps)
{
	BITMAPINFOHEADER bih = { sizeof(BITMAPINFOHEADER) };

	IMFMediaType* mediaType;
	reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &mediaType);

	MFGetAttributeSize(mediaType, MF_MT_FRAME_SIZE, width, height);
	bih.biWidth = *width;
	bih.biHeight = *height;

	UINT32 nume, denom;
	MFGetAttributeRatio(mediaType, MF_MT_FRAME_RATE, &nume, &denom);
	*fps = (double)nume / denom;

	GUID subType = {};
	mediaType->GetGUID(MF_MT_SUBTYPE, &subType);
	if (subType == MFVideoFormat_YUY2) {
		bih.biCompression = FCC('YUY2');
		bih.biBitCount = 16;
	}
	mediaType->Release();
}

void GetAudioInfo(IMFSourceReader* reader)
{
	WAVEFORMATEX wavFormat = {};
	wavFormat.wFormatTag = WAVE_FORMAT_PCM;

	CComPtr<IMFMediaType> mediaType;
	reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &mediaType);

	UINT32 numChannel = 0;
	mediaType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &numChannel);
	wavFormat.nChannels = static_cast<WORD>(numChannel);

	UINT32 samplesParSec = 0;
	mediaType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &samplesParSec);
	wavFormat.nSamplesPerSec = static_cast<DWORD>(samplesParSec);
	g_nSamplesPerSec = samplesParSec;

	UINT32 avgBytesPerSec = 0;
	mediaType->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &avgBytesPerSec);
	wavFormat.nAvgBytesPerSec = static_cast<DWORD>(avgBytesPerSec);

	UINT32 blockAlign = 0;
	mediaType->GetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, &blockAlign);
	wavFormat.nBlockAlign = static_cast<WORD>(blockAlign);

	UINT32 bitsPerSample = 0;
	mediaType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bitsPerSample);
	wavFormat.wBitsPerSample = static_cast<WORD>(bitsPerSample);
}

UINT32 GetTotalFrameCount(IMFSourceReader* reader)
{
	IMFMediaType* mediaType;
	reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &mediaType);

	UINT32 nume, denom;
	MFGetAttributeRatio(mediaType, MF_MT_FRAME_RATE, &nume, &denom);
	//*fps = (double)nume / denom;

	LONGLONG nsDuration;	// 100ns units
	PROPVARIANT var;
	HRESULT hr = reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var);
	hr = ::PropVariantToInt64(var, &nsDuration);
	PropVariantClear(&var);

	g_nume = nume;
	g_denom = denom;

	UINT32 totalFrames = ((nsDuration / 10000000.0) * nume) / denom;
	return totalFrames;
}

void SeekFrame(IMFSourceReader* pReader, int frame)
{
	LONGLONG nsPosition = ConvertTimeStampFromFrame(frame);

	PROPVARIANT var;
	HRESULT hr = InitPropVariantFromInt64(nsPosition, &var);
	if (SUCCEEDED(hr))
	{
		hr = pReader->SetCurrentPosition(GUID_NULL, var);
		PropVariantClear(&var);
	}
}

#define USE_DXVA

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
	//present_params.BackBufferFormat = D3DFMT_YUY2;//D3DFMT_UNKNOWN;
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

LRESULT CMainDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	// center the dialog on the screen
	CenterWindow();

	// set icons
	HICON hIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON));
	SetIcon(hIcon, TRUE);
	HICON hIconSmall = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
	SetIcon(hIconSmall, FALSE);

	// register object for message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->AddMessageFilter(this);
	pLoop->AddIdleHandler(this);

	UIAddChildWindowContainer(m_hWnd);

	try {
		HRESULT hr = ::MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
		if (FAILED(hr)) {
			throw hr;
		}

		CComPtr<IMFAttributes> spAttributes = NULL;
#ifdef USE_DXVA

#if 1
		// ==
		CComPtr<IDirect3DDeviceManager9> dev_manager;
		CComPtr<IDirect3DDevice9> device;
		dev_manager.Attach(CreateD3DDevManager(GetDesktopWindow(), &device));

		g_dev_manager = dev_manager;
		CComPtr<IDirect3DDeviceManager9> spDXGIDeviceManager = dev_manager;
#endif
		// ==
#if 0
		CComPtr<IDXGIFactory1> spDXGIFactory;
		hr = ::CreateDXGIFactory1(IID_PPV_ARGS(&spDXGIFactory));
		ATLASSERT(SUCCEEDED(hr));

		CComPtr<IDXGIAdapter1> spDXGIAdapter;    //デバイス取得用
		int adapterIndex = 0;    //列挙するデバイスのインデックス
		bool adapterFound = false;    //目的のデバイスを見つけたか

		// 目的のデバイスを探索
		while (spDXGIFactory->EnumAdapters1(adapterIndex, &spDXGIAdapter) != DXGI_ERROR_NOT_FOUND) {
			DXGI_ADAPTER_DESC1 desc;
			spDXGIAdapter->GetDesc1(&desc);  // デバイスの情報を取得

			// ハードウェアのみ選ぶ
			if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
				adapterFound = true;
				break;
			}
			++adapterIndex;
		}		


		CComPtr<IMFDXGIDeviceManager> spDXGIDeviceManager;
		UINT resetToken = 0;
		hr = ::MFCreateDXGIDeviceManager(&resetToken, &spDXGIDeviceManager);
		ATLASSERT(SUCCEEDED(hr));

		D3D_FEATURE_LEVEL d3dFeatureLevels[] = {
			D3D_FEATURE_LEVEL_11_1
		};
		CComPtr<ID3D11Device> spD3D11Device;
		D3D_FEATURE_LEVEL featureLevel;
		hr = ::D3D11CreateDevice(spDXGIAdapter.p, D3D_DRIVER_TYPE_UNKNOWN, NULL, D3D11_CREATE_DEVICE_VIDEO_SUPPORT, d3dFeatureLevels, 1, D3D11_SDK_VERSION, &spD3D11Device, &featureLevel, NULL);
		ATLASSERT(SUCCEEDED(hr));
		CComQIPtr<ID3D10Multithread> spMultithread = spD3D11Device;
		BOOL b = spMultithread->SetMultithreadProtected(TRUE);

		hr = spDXGIDeviceManager->ResetDevice(spD3D11Device, resetToken);
		ATLASSERT(SUCCEEDED(hr));
		HANDLE deviceHandle;
		//spDXGIDeviceManager->ResetDevice()
#endif
		hr = ::MFCreateAttributes(&spAttributes, 2);

		hr = spAttributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, spDXGIDeviceManager);
		ATLASSERT(SUCCEEDED(hr));

		hr = spAttributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, FALSE);
		ATLASSERT(SUCCEEDED(hr));
#if 0
		hr = spAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
		ATLASSERT(SUCCEEDED(hr));

		hr = spAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
		ATLASSERT(SUCCEEDED(hr));
#endif
		
#else
		hr = ::MFCreateAttributes(&spAttributes, 1);

		hr = spAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
#endif

		//LPCWSTR videoPath = LR"(G:\共有フォルダ\新しいフォルダー\2020-01-17_04-59-36_.mkv)";
		LPCWSTR videoPath = LR"(G:\まちカドまぞく\#1.mp4)";
		CComPtr<IMFSourceReader> spMFSourceReader;
		hr = ::MFCreateSourceReaderFromURL(videoPath, spAttributes, &spMFSourceReader);
		if (FAILED(hr)) {
			throw hr;
		}

		// 出力形式指定
		hr = ConfigureDecoder(spMFSourceReader, MF_SOURCE_READER_FIRST_VIDEO_STREAM);
		ATLASSERT(SUCCEEDED(hr));
		hr = ConfigureDecoder(spMFSourceReader, MF_SOURCE_READER_FIRST_AUDIO_STREAM);
		ATLASSERT(SUCCEEDED(hr));

#ifdef USE_DXVA
		// Send a message to the decoder to tell it to use DXVA2.
		CComPtr<IMFTransform> spVideoDecoder;
		hr = spMFSourceReader->GetServiceForStream(MF_SOURCE_READER_FIRST_VIDEO_STREAM, GUID_NULL, IID_PPV_ARGS(&spVideoDecoder));
		ATLASSERT(SUCCEEDED(hr));

		hr = spVideoDecoder->ProcessMessage(
			MFT_MESSAGE_SET_D3D_MANAGER,
			reinterpret_cast<ULONG_PTR>(spDXGIDeviceManager.p));
		ATLASSERT(SUCCEEDED(hr));

#endif

		GetAudioInfo(spMFSourceReader);
		UINT32 totalFrames = GetTotalFrameCount(spMFSourceReader);

		UINT32 width, height;
		double fps;
		GetVideoInfo(spMFSourceReader, &width, &height, &fps);

#ifdef USE_DXVA
		//bool bb = AdjustD3DDeviceBackBufferDimensions(width, height, device.p, GetDesktopWindow());
#endif
		LONGLONG llnsDuration = 0;
		hr = GetDuration(spMFSourceReader, &llnsDuration);

		//SeekFrame(spMFSourceReader, 950);
		//TestReadSample(spMFSourceReader);

#if 0
		DWORD streamIndex, flags;
		LONGLONG llTimeStamp;

		CComPtr<IMFSample> spMFSample;
		hr = spMFSourceReader->ReadSample(
			MF_SOURCE_READER_ANY_STREAM,    // Stream index.
			0,                              // Flags.
			&streamIndex,                   // Receives the actual stream index. 
			&flags,                         // Receives status flags.
			&llTimeStamp,                   // Receives the time stamp.
			&spMFSample                        // Receives the sample or NULL.
		);
		DWORD bufferCount = 0;
		spMFSample->GetBufferCount(&bufferCount);
#endif
#if 0
		{
			HANDLE file;
			BITMAPFILEHEADER fileHeader;
			BITMAPINFOHEADER fileInfo;
			DWORD write = 0;

			file = CreateFile(L"sample.bmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);  //Sets up the new bmp to be written to

			fileHeader.bfType = 19778;                                                                    //Sets our type to BM or bmp
			fileHeader.bfSize = sizeof(fileHeader.bfOffBits) + sizeof(RGBTRIPLE);                                                //Sets the size equal to the size of the header struct
			fileHeader.bfReserved1 = 0;                                                                    //sets the reserves to 0
			fileHeader.bfReserved2 = 0;
			fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);                    //Sets offbits equal to the size of file and info header

			fileInfo.biSize = sizeof(BITMAPINFOHEADER);
			fileInfo.biWidth = 1280;
			fileInfo.biHeight = 720;
			fileInfo.biPlanes = 1;
			fileInfo.biBitCount = 32;
			fileInfo.biCompression = BI_RGB;
			fileInfo.biSizeImage = fileInfo.biWidth * fileInfo.biHeight * (fileInfo.biBitCount / 8);
			fileInfo.biXPelsPerMeter = 0;
			fileInfo.biYPelsPerMeter = 0;
			fileInfo.biClrImportant = 0;
			fileInfo.biClrUsed = 0;
			fileInfo.biHeight = -fileInfo.biHeight;

			WriteFile(file, &fileHeader, sizeof(fileHeader), &write, NULL);
			WriteFile(file, &fileInfo, sizeof(fileInfo), &write, NULL);

			IMFMediaBuffer* mediaBuffer = NULL;
			BYTE* pData = NULL;

			spMFSample->ConvertToContiguousBuffer(&mediaBuffer);
			DWORD currentLength = 0;
			mediaBuffer->GetCurrentLength(&currentLength);
			hr = mediaBuffer->Lock(&pData, NULL, NULL);

			WriteFile(file, pData, fileInfo.biSizeImage, &write, NULL);

			CloseHandle(file);

			mediaBuffer->Unlock();
		}
#endif
		Utility::timer timer;
		hr = ProcessSamples(spMFSourceReader);
		ATLTRACE("timer: %s\n", timer.format().c_str());
		if (FAILED(hr)) {
			throw hr;
		}
	}
	catch (HRESULT hr)
	{
		ATLTRACE(L"catch: %ld", hr);
	}

	return TRUE;
}

LRESULT CMainDlg::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	// unregister message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->RemoveMessageFilter(this);
	pLoop->RemoveIdleHandler(this);

	{
		::MFShutdown();
	}

	return 0;
}

LRESULT CMainDlg::OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	CAboutDlg dlg;
	dlg.DoModal();
	return 0;
}

LRESULT CMainDlg::OnOK(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	// TODO: Add validation code 
	CloseDialog(wID);
	return 0;
}

LRESULT CMainDlg::OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	CloseDialog(wID);
	return 0;
}

void CMainDlg::CloseDialog(int nVal)
{
	DestroyWindow();
	::PostQuitMessage(nVal);
}












