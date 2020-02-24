// MainDlg.h : interface of the CMainDlg class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include <thread>
#include <atomic>

#include <atlframe.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <atlmisc.h>
#include <atlddx.h>
#include <atlcrack.h>

#include "resource.h"

#include "aboutdlg.h"

#include "..\Share\Common.h"


class CMainDlg : 
	public CDialogImpl<CMainDlg>, 
	public CUpdateUI<CMainDlg>,
	public CMessageFilter, 
	public CIdleHandler,
	public CWinDataExchange<CMainDlg>
{
public:
	enum { IDD = IDD_MAINDLG };

	virtual BOOL PreTranslateMessage(MSG* pMsg)
	{
		return CWindow::IsDialogMessage(pMsg);
	}

	virtual BOOL OnIdle()
	{
		UIUpdateChildWindows();
		return FALSE;
	}

	BEGIN_UPDATE_UI_MAP(CMainDlg)
	END_UPDATE_UI_MAP()

	BEGIN_DDX_MAP(CMainDlg)
		DDX_CHECK(IDC_CHECK_USE_DXVA2, m_config.bUseDXVA2)
		DDX_CHECK(IDC_CHECK_USE_HANDLECACHE, m_config.bEnableHandleCache)
		DDX_CHECK(IDC_CHECK_USE_IPC, m_config.bEnableIPC)
		DDX_COMBO_INDEX(IDC_COMBO_LOGLEVEL, m_config.logLevel)

		DDX_TEXT(IDC_EDIT_VIDEOPATH, m_videoPath)
		DDX_CONTROL_HANDLE(IDC_EDIT_VIDEOINFO, m_editVideoInfo)
		DDX_CONTROL_HANDLE(IDC_RICHEDIT2_LOG, m_editLog)
		DDX_CONTROL_HANDLE(IDC_PROGRESS_TEST, m_progressTest)
		DDX_CONTROL_HANDLE(IDC_EDIT_AVARAGEFPS, m_editAvarageFPS)
	END_DDX_MAP()

	BEGIN_MSG_MAP_EX(CMainDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		COMMAND_ID_HANDLER(ID_APP_ABOUT, OnAppAbout)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)

		MSG_WM_DROPFILES(OnDropFiles)
		COMMAND_ID_HANDLER_EX(IDC_BUTTON_FILESELECT, OnFileSelect)
		COMMAND_ID_HANDLER_EX(IDC_BUTTON_CHECKMEDIAINFO, OnChechMediaInfo)
		COMMAND_ID_HANDLER_EX(IDC_BUTTON_DECODETEST, OnDecodeTest)
		END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnOK(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void CloseDialog(int nVal);

	void OnDropFiles(HDROP hDropInfo);
	void OnFileSelect(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnChechMediaInfo(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnDecodeTest(UINT uNotifyCode, int nID, CWindow wndCtl);

private:
	void	PutLog(LPCWSTR format, ...);

	Config	m_config;

	CEdit	m_editVideoInfo;
	CRichEditCtrl	m_editLog;
	CProgressBarCtrl	m_progressTest;
	CEdit	m_editAvarageFPS;

	// 再生テスト
	CString  m_videoPath;

	// デコードテスト
	std::thread	m_decodeThread;
	std::atomic_bool m_decodeCancel;

};
