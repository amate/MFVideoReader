# MFVideoReader
 

## 始めに

これは "L-SMASH Works File Reader"、"DirectShow File Reader"に代わる、第三のファイル入力プラグインとして製作した
AviUtlの入力プラグインです  

ファイルの入力、分離、デコードに、Media Foundation を使っており、DXVA2(GPU)を利用した高速なデコードが可能です (シークはそんなに速くない...)  

VideoReaderとありますが、音声ファイルも入力できます (Media Foundationが対応していれば)

対応するファイル形式は  
- コンテナ  
.3g2 .3gp .3gp2 .3gpp .aac . ac3 .asf .avi .flac .m2t .m2ts .m4a .m4v .mkv .mov .mp3 .mp4 .mpeg .mpg .wav .wma .wmv

- 動画コーディック  
MPEG2 / MPEG4 / H264 / VP9 / WMV3 / VC-1

- 音声コーディック  
AAC / ALAC / AC3 / FLAC / MP3 / Opus / WMA

OSに元からインストールされている主なコーディックを書きましたが、大体再生できるんじゃないかな

#### ※注意事項
 - 10bit(high bit)カラー、または インターレースの動画は再生できません！
 - 可変フレームレートの動画には対応していません！
 - 3ch以上の音声は、2chとして扱います！
 - Media Foundationでは、動画のフレーム単位、音声のサンプリング単位でのシークができません  
 そのためにシークを行うと、内部でフレームのタイムスタンプから計算してフレーム番号を算出していますが、実際の再生位置とのずれが生じる可能性があります (動画なら1フレーム、音声なら32サンプルほどずれる可能性がある)  
 AvlUtlに渡す総フレーム数や、総サンプリング数もずれることがあり、後ろ数フレーム欠けることがあります  
 シーケンシャルに読み込んでいく場合は、ずれが生じないようにしています (エンコード時にはずれないはず)



## 動作環境
- Windows 10 home 64bit バージョン 1909
- AviUtl 1.00 or 1.10
- 拡張編集 0.92

※ 拡張編集 0.93rc1 はシーン周りに不具合があるので推奨しません  
上記の環境で動作を確認しています  
Media Foundationの都合上、恐らくWindows 10でしか動きません

## 導入方法

"MFVideoReaderPlugin.aui"と"MFVideoReader.exe"を   
aviutl.exeがあるフォルダ、もしくは "aviutl.exeがあるフォルダ\plugins\"にコピーしてください

その後、aviutlのメインメニューから、[ファイル]->[環境設定]->[入力プラグイン優先度の設定]を選択し、  
"MFVideoReaderPlugin"を"L-SMASH Works File Reader"や"DirectShow File Reader"より上にドラッグして移動してあげれば完了です

## 導入の確認
拡張編集のタイムラインに適当な動画ファイル(mp4やmkvなら多分大丈夫)を放り込んで、タイムラインの再生位置をグリグリ移動させたときに、タスクマネージャーのGPUタブの"Video Decode"のグラフが適度に上下していれば導入できているはずです

## 設定
[ファイル]->[環境設定]->[入力プラグインの設定]->[MFVideoReaderPluginの設定]で
プラグインの設定ができます

- DXVA2(GPU利用)を有効にする  

GPUを利用したデコードを有効にする設定です  
これにチェックを付けていても、動画のコーディックによっては DXVA2を利用できないかもしれません

- ハンドルキャッシュを有効

編集で同じ動画ファイルのカットを多用するなどの場合、  
拡張編集プラグインが同じ動画に対して毎回ファイルオープンのリクエストを送ってくるので、  
いちいちファイルをオープンせずに、一度開いたファイルのハンドルを使いまわすようにする設定です

- プロセス間通信を有効にする

外部プロセスを立ち上げて、その中でMFVideoReaderPluginを実行するようにします  
外部プロセスとの通信はコストがかかるので、重いと感じたらチェック外してください

- 再生テスト  

動画情報の取得と、デコードテストを行えます  
デコードテストでは動画の全フレームのデコードを試みます (音声はデコードしません)  
上記の"DXVA2(GPU利用)を有効にする"のチェックボックスと連動しているので、ベンチマークとしても利用できます

## プラグインを入れて不安定になった場合
- 拡張編集の環境設定から、動画ファイルのハンドル数を8から16程度に増やしてみてください
- MFVideoReaderPluginの設定から[プロセス間通信を有効にする]のチェックを外してみてください

## アンインストールの方法
MFVideoReaderPlugin.aui と  
MFVideoReader.exe  と  
MFVideoReaderConfig.ini を削除してください

## 免責
作者(原著者＆改変者)は、このソフトによって生じた如何なる損害にも、
修正や更新も、責任を負わないこととします。
使用にあたっては、自己責任でお願いします。
 
何かあれば下記のURLにあるメールフォームか、githubのIssueにお願いします。　　
https://ws.formzu.net/fgen/S37403840/
 
## 著作権表示
Copyright (C) 2020 amate

私が書いた部分のソースコードは、MIT License とします。

## ビルドについて
Visual Studio 2019 が必要です  
ビルドには boost(1.72~)とWTL(10_9163) が必要なのでそれぞれ用意してください。

Boost::Logを使用しているので、事前にライブラリのビルドが必要になります

Boostライブラリのビルド方法  
https://boostjp.github.io/howtobuild.html  

//コマンドライン  
>b2.exe install -j 16 --prefix=lib toolset=msvc-14.2  runtime-link=static --with-log --with-filesystem

- boost  
http://www.boost.org/

- WTL  
http://sourceforge.net/projects/wtl/


## 更新履歴
- v1.0  
・完成
