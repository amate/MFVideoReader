/**
*	@brief MFVideoDecoderをAviUtlのファイルリーダープラグイン渡す関数として包み込む
*/

#pragma once

#include "..\MFVideoReaderPlugin\input.h"

INPUT_HANDLE MFVideoDecoder_func_open(LPSTR file, bool bUseDXVA2);
BOOL MFVideoDecoder_func_close(INPUT_HANDLE ih);
BOOL MFVideoDecoder_func_info_get(INPUT_HANDLE ih, INPUT_INFO* iip);
int MFVideoDecoder_func_read_video(INPUT_HANDLE ih, int frame, void* buf);
int MFVideoDecoder_func_read_audio(INPUT_HANDLE ih, int start, int length, void* buf);

