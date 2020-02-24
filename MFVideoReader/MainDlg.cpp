
#include "pch.h"
#include "MainDlg.h"

#include <chrono>
#include <boost\algorithm\string\replace.hpp>

#include "DropHandler.h"
#include "timer.h"
#include "..\Share\Common.h"
#include "..\Share\CodeConvert.h"
#include "..\MFVideoReaderPlugin\MFVideoDecoder.h"
#include "..\MFMediaPropDump\MFFriendlyErrors.h"

// for Logger
extern int	g_logLevel;
std::string	LogFileName()
{
	return (GetExeDirectory() / L"MFVideoReader.log").string();
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

	std::wstring title = L"MFVideoReader - version " + CodeConvert::UTF16fromShiftJIS(PLUGIN_VERSION);
	SetWindowText(title.c_str());

	DragAcceptFiles(TRUE);

	m_config.LoadConfig();
	g_logLevel = m_config.get_severity_level();
	CComboBox cmbLogLevel = GetDlgItem(IDC_COMBO_LOGLEVEL);
	LPCWSTR logLevelText[] = {
		L"none", L"info", L"warning", L"error"
	};
	for (LPCWSTR logLevel : logLevelText) {
		cmbLogLevel.AddString(logLevel);
	}

	DoDataExchange(DDX_LOAD);

	CLogFont lf;
	lf.SetMenuFont();
	m_editLog.SetFont(lf.CreateFontIndirectW());

	m_editLog.SetBackgroundColor();

	MFInitialize();

	return TRUE;
}

LRESULT CMainDlg::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	// unregister message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->RemoveMessageFilter(this);
	pLoop->RemoveIdleHandler(this);

	MFFinalize();

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
	DoDataExchange(DDX_SAVE);
	if (!m_config.SaveConfig()) {
		MessageBox(L"設定の保存に失敗しました。", L"エラー", MB_ICONERROR);
	}
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

void CMainDlg::OnDropFiles(HDROP hDropInfo)
{
	std::list<fs::path> dropFiles;
	DropHandler drop(hDropInfo);
	UINT nCount = drop.GetCount();
	for (UINT i = 0; i < nCount; ++i) {
		fs::path dropPath = drop[i];
		if (fs::is_directory(dropPath)) {
			continue;
		}
		dropFiles.emplace_back(dropPath);
	}
	if (dropFiles.size() > 0u) {
		fs::path& videoPath = dropFiles.front();
		m_videoPath = videoPath.c_str();
		DoDataExchange(DDX_LOAD, IDC_EDIT_VIDEOPATH);
	}
}

void CMainDlg::OnFileSelect(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	CShellFileOpenDialog fileOpenDlg;
	if (fileOpenDlg.DoModal() == IDOK) {
		fileOpenDlg.GetFilePath(m_videoPath.GetBuffer(MAX_PATH), MAX_PATH);
		m_videoPath.ReleaseBuffer();
		DoDataExchange(DDX_LOAD, IDC_EDIT_VIDEOPATH);
	}
}

void CMainDlg::OnChechMediaInfo(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	m_editVideoInfo.SetWindowText(L"");
	m_editLog.SetWindowText(L"");

	if (!fs::exists(m_videoPath.GetString())) {
		return;
	}

	DoDataExchange(DDX_SAVE, IDC_CHECK_USE_DXVA2);

	try {
		MFVideoDecoder decoder;
		if (!decoder.Initialize(m_config.bUseDXVA2)) {
			PutLog(L"デコーダーの初期化に失敗しました。");
			return;
		}
		if (!decoder.OpenMediaFile(m_videoPath.GetString())) {
			PutLog(L"ファイルのオープンに失敗しました。");
			return;
		}
		auto mediaInfo = decoder.GetMediaInfo().GetMediaInfoText();
		boost::algorithm::replace_all(mediaInfo, L"\n", L"\r\n");
		m_editVideoInfo.SetWindowText(mediaInfo.c_str());

		decoder.Finalize();
	} catch (HRESULT hr) {
		if (hr != S_OK) {
			LPCWSTR errorMsg = nullptr;
			if (FAILED(MFFriendlyConvertHRESULT(hr, &errorMsg))) {
				errorMsg = L"";
			}
			PutLog(L"例外が発生しました hr: %d [%s]", hr, errorMsg);
		}
	}
}

void CMainDlg::OnDecodeTest(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	if (!fs::exists(m_videoPath.GetString())) {
		return;
	}

	auto funcControlStateChange = [this](bool bEnable) {
		GetDlgItem(IDC_EDIT_VIDEOPATH).EnableWindow(bEnable);
		GetDlgItem(IDC_BUTTON_FILESELECT).EnableWindow(bEnable);
		GetDlgItem(IDC_BUTTON_CHECKMEDIAINFO).EnableWindow(bEnable);
	};

	if (m_decodeThread.joinable()) {
		// テスト中
		m_decodeCancel.store(true);
		GetDlgItem(IDC_BUTTON_DECODETEST).SetWindowTextW(L"キャンセル中...");

	} else {
		m_decodeCancel.store(false);
		GetDlgItem(IDC_BUTTON_DECODETEST).SetWindowTextW(L"キャンセル");

		m_editVideoInfo.SetWindowText(L"");
		m_editLog.SetWindowText(L"");

		funcControlStateChange(false);

		DoDataExchange(DDX_SAVE, IDC_CHECK_USE_DXVA2);

		PutLog(L"デコードテストを開始します...");

		m_decodeThread = std::thread([this, funcControlStateChange]() {
			MFVideoDecoder decoder;
			PutLog(L"decoder.Initialize - UseDXVA2 : %s", m_config.bUseDXVA2 ? L"Yes" : L"No");
			if (decoder.Initialize(m_config.bUseDXVA2)) {
				try {
					if (!decoder.OpenMediaFile(m_videoPath.GetString())) {
						PutLog(L"ファイルのオープンに失敗しました。");
						throw S_OK;
					}
					const auto& mediaInfo = decoder.GetMediaInfo();
					auto mediaInfoText = mediaInfo.GetMediaInfoText();
					boost::algorithm::replace_all(mediaInfoText, L"\n", L"\r\n");
					m_editVideoInfo.SetWindowText(mediaInfoText.c_str());

					if (mediaInfo.videoStreamIndex == -1) {
						PutLog(L"ファイルに映像が存在しません。");
						throw S_OK;
					}

					std::unique_ptr<BYTE[]> buffer(new BYTE[mediaInfo.outImageBufferSize]);

					m_progressTest.SetRange32(0, mediaInfo.totalFrameCount);
					auto startTime = std::chrono::steady_clock::now();
					enum { kFPSCountInterval = 1000, kProgressSetPosInterval = 100 };

					auto fucnCalcAvarageFPS = [startTime](int frame) -> int {
						auto processingDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - startTime).count();
						double avarageFPS = static_cast<double>(frame) / processingDuration * 1000000000;
						return static_cast<int>(avarageFPS);
					};

					int i = 0;
					for ( ; i < mediaInfo.totalFrameCount && !m_decodeCancel; ++i) {
						int readSize = decoder.ReadFrame(i, buffer.get());
						if (readSize == 0) {
							PutLog(L"フレーム取得に失敗 frame: %d", i);
						}
						if ((i % kFPSCountInterval) == 0) {
							wchar_t avarageFPSText[32] = L"";
							swprintf_s(avarageFPSText, L"%d", fucnCalcAvarageFPS(i));
							m_editAvarageFPS.SetWindowText(avarageFPSText);
						}
						if ((i % kProgressSetPosInterval) == 0) {
							m_progressTest.SetPos(i);
						}
					}
					m_progressTest.SetPos(0);

					PutLog(L"平均fps: %d", fucnCalcAvarageFPS(i));

					auto processingTime = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - startTime).count();
					double processingSec = static_cast<double>(processingTime) / 1000000000;
					PutLog(L"処理時間 : %.2f 秒", processingSec);

				} catch (HRESULT hr) {
					if (hr != S_OK) {
						LPCWSTR errorMsg = nullptr;
						if (FAILED(MFFriendlyConvertHRESULT(hr, &errorMsg))) {
							errorMsg = L"";
						}
						PutLog(L"例外が発生しました hr: %d [%s]", hr, errorMsg);
					}
				}
				decoder.Finalize();

			} else {
				PutLog(L"デコーダーの初期化に失敗しました。");
			}
			PutLog(L"テスト終了");
			GetDlgItem(IDC_BUTTON_DECODETEST).SetWindowTextW(L"デコードテスト");

			funcControlStateChange(true);

			m_decodeThread.detach();
		});
	}
}

void CMainDlg::PutLog(LPCWSTR format, ...)
{
	CString str;
	va_list args;
	va_start(args, format);
	str.FormatV(format, args);
	va_end(args);
	str += L"\r\n";
	m_editLog.AppendText(str);
}












