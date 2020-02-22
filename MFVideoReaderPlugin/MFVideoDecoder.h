#pragma once

#include <string>

#include <d3d9.h>
#include <d3d11.h>
#include <dxva2api.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <atlcomcli.h>

#include <boost\circular_buffer.hpp>

bool	MFInitialize();
void	MFFinalize();

struct MediaInfo
{
	DWORD videoStreamIndex;	// -1 で映像は存在しない
	DWORD audioStreamIndex;	// -1 で音声は存在しない

	UINT32 nume, denom;
	LONGLONG nsDuration;	// 100ns units
	int	totalFrameCount;		
	BITMAPINFOHEADER	imageFormat = {};
	int outImageBufferSize;

	int totalAudioSampleCount;	
	WAVEFORMATEX		audioFormat = {};

	std::wstring GetMediaInfoText() const;
};

class SampleCache
{
public:
	enum { 
		kMaxVideoBufferSize = 4,	// あまり大きな値を設定するとReadSampleで停止する
		kMaxAudioBufferSize = 20, 

		kFrameWaringGapCount = 1,
		kAudioSampleWaringGapCount = 1000,
	};

	void	ResetVideo();
	void	ResetAudio(WORD nBlockAlign);

	void	AddFrameSample(int frame, IMFSample* pSample);
	void	AddAudioSample(int startSample, IMFSample* pSample);

	int		LastFrameNumber() const;
	int		LastAudioSampleNumber() const;

	IMFSample*	SearchFrameSample(int frame);
	bool		SearchAudioSampleAndCopyBuffer(int startSample, int copySampleLength, BYTE* buffer);


private:
	struct VideoCache
	{
		int	frame;
		CComPtr<IMFSample> spSample;
	};
	boost::circular_buffer<VideoCache>	m_videoCircularBuffer;

	struct AudioCache
	{
		int startSampleNum;
		CComPtr<IMFSample> spSample;
		int audioSampleCount;

		bool	CopyBuffer(int& startSample, int& copySampleLength, BYTE*& buffer, WORD	nBlockAlign);
	};
	WORD	m_nBlockAlign;
	boost::circular_buffer<AudioCache>	m_audioCircularBuffer;
};

class MFVideoDecoder
{
	enum { 
		// 現在のフレームからどれくらいの範囲ならシーケンシャル読み込みさせるかの閾値
		kThresholdFrameCount = 30,

		// 現在のサンプル数からどれくらいの範囲ならシーケンシャル読み込みさせるかの閾値
		kThresholdSampleCount = 30000,
	};

public:
	bool	Initialize(bool bUseDXVA2);
	void	Finalize();

	bool	OpenMediaFile(const std::wstring& filePath);
	MediaInfo& GetMediaInfo() { return m_mediaInfo;  }

	int		ReadFrame(const int frame, BYTE* buf);
	int		ReadAudio(int start, int length, void* buf);

private:
	bool	CheckMediaInfo(IMFSourceReader* pReader);
	void	ChangeColorConvertSettingAndCreateBuffer();
	void	TestFirstReadSample();

	void	SeekVideo(LONGLONG destTimePosition);
	void	SeekAudio(LONGLONG destTimePosition);

	CComPtr<IMFSample>	ReadSample(DWORD streamIndex, HRESULT& hr);
	void	ConvertColor(IMFSample* pSample);

	bool	m_bUseDXVA2 = false;

	// for DXVA2
	CComPtr<IDirect3DDeviceManager9>	m_spDevManager;
	CComPtr<IDirect3DDevice9>	m_spDevice;
	//CComPtr<IMFDXGIDeviceManager>	m_spDevManager;
	//CComPtr<ID3D11Device>	m_spDevice;
	CComPtr<IMFTransform>	m_spVPMFTransform;
	CComPtr<IMFSample>		m_spMFOutBufferSample;

	CComPtr<IMFSourceReader>	m_spVideoSourceReader;
	CComPtr<IMFSourceReader>	m_spAudioSourceReader;
	LONGLONG	m_firstGapTimeStamp = 0;
	LONGLONG	m_currentVideoTimeStamp = 0;
	LONGLONG	m_currentAudioTimeStamp = 0;

	MediaInfo	m_mediaInfo;

	//LONGLONG	m_currentTimeStamp = 0;

	int	m_currentFrame;
	int m_currentSample;

	SampleCache	m_sampleCache;
};
