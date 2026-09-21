# Pocket Audio Deck Web Radio

[English](README.md) | [写真付き操作マニュアル](https://rinoproducts.official.jp/support/pocket-audio-deck/)

M5StickS3とPocket Audio Deck専用基板向けの、PlatformIO/Arduinoファームウェアです。
公開HTTP MP3 Webラジオを基板上のPCM5102A DACから再生でき、microSD内のMP3ファイル再生にも対応します。

この公開版には、地域制限サービスの認証、非公開エンドポイント、地域放送局データ、および関連する再生処理は含まれていません。

## はじめに：PlatformIOで書き込む

**VS Codeでこのフォルダを開き、PlatformIOの「Upload」を実行すると、ビルドからUSB書き込みまで行えます。**
ボード設定やライブラリ指定は用意済みです。通常はソースの編集も、Wi-Fi情報の埋め込みも不要です。

**ダウンロード → VS Codeで開く → USB接続 → Upload → 本体でWi-Fi設定**

### 1. 用意するもの

- M5StickS3（Pocket Audio Deckには付属しません）とPocket Audio Deck基板
- **データ通信対応**のUSBケーブル（充電専用ケーブルは不可）
- インターネットに接続したPC
- [Visual Studio Code](https://code.visualstudio.com/)と、拡張機能の[PlatformIO IDE](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)
- [Git](https://git-scm.com/downloads)（ZIPで入手する場合も、ビルド時の依存ライブラリ取得に必要）

VS Codeの拡張機能画面で「PlatformIO IDE」をインストールし、初期セットアップの完了を待ってください。
PlatformIO CoreやArduino IDEを別途インストールする必要はありません。
詳細は[PlatformIO公式の導入ガイド](https://docs.platformio.org/en/stable/integration/ide/vscode.html#installation)を参照してください。

### 2. プロジェクトを開く

1. このGitHubページの **Code → Download ZIP** でダウンロードし、ZIPを展開します。Gitを使う方はcloneでも構いません。
2. VS Codeの **ファイル → フォルダーを開く** で、展開した中の **`platformio.ini`が入っているフォルダ**を開きます。`src`だけを開かないでください。
3. PlatformIOの読み込みが終わるまで待ちます。左側のPlatformIOアイコンから **PROJECT TASKS → m5sticks3 → General** を開きます。

「New Project」で新しいプロジェクトを作ったり、ボードを選び直したりする必要はありません。
`platformio.ini`の`esp32-s3-devkitc1-n8r8`は、このプロジェクトでM5StickS3向けに使用している設定です。そのまま使ってください。

### 3. USBで接続して書き込む

1. M5StickS3をUSBケーブルでPCに接続します。別のシリアルモニターを開いている場合は閉じます。
2. **PROJECT TASKS → m5sticks3 → General → Upload** をクリックします。
3. ターミナルに **`SUCCESS`** が出るまで、USBを抜かずに待ちます。初回は開発ツールとライブラリのダウンロードがあるため、時間がかかります。
4. 本体が再起動し、Pocket Audio Deckのウェルカム画面が表示されたら、初回設定へ進みます。

| 操作 | 行うこと |
| --- | --- |
| Build | プログラムをビルドしてエラーを確認。**本体には書き込みません**。 |
| Upload | 必要に応じてビルドし、USB経由で本体へ書き込みます。通常はこちらだけでOKです。 |
| Monitor | 本体のシリアルログを表示します（115200 baud）。問題の切り分け用です。 |

書き込むと、M5StickS3に入っていた別のファームウェアは置き換わります。
**Buildの成功と、Uploadの成功は別です。** Uploadのログで`SUCCESS`を確認してください。

### 4. はじめての再生

**Webラジオ：** ラジオモードで青いKEY1を約2秒長押しし、表示されたQRコードからWi-Fi設定へ進みます。
詳しくは[Wi-Fi設定](#wi-fi設定)を参照してください。未設定時の`WiFi not configured`は、書き込み失敗ではありません。

**MP3：** MP3ファイルを入れたmicroSDカードを挿し、KEY2を長押ししてMP3モードに切り替えます。MP3再生にWi-Fiは不要です。

音声は**Pocket Audio Deck基板のイヤホンジャック**から出ます。M5StickS3の内蔵スピーカーは使用しません。
最初は音量を下げて確認してください。ボタン位置や詳しい操作は[写真付き操作マニュアル](https://rinoproducts.official.jp/support/pocket-audio-deck/)にまとめています。

### 書き込めないとき

| 症状 | 確認すること |
| --- | --- |
| PROJECT TASKSが出ない | `platformio.ini`があるフォルダを開いているか、PlatformIOの初期セットアップが終わっているか確認します。 |
| USBポートが見つからない | データ通信対応ケーブルに交換し、PCへ直接接続します。他のUSBシリアル機器を外して再確認します。 |
| `Connecting...`のまま進まない | 下記の「手動でダウンロードモードにする」を試してください。 |
| ポートが使用中になる | PlatformIOのMonitorや、ほかのアプリのシリアルモニターを閉じてからUploadします。 |
| ダウンロードが失敗する / Gitが見つからない | ネット接続とGitのインストールを確認します。Gitを追加した場合はVS Codeを再起動します。 |
| `xtensa-esp32s3-elf-g++: command not found`や`get_metavar`のエラー | 書き込み前のPC側の開発環境エラーです。`platformio.ini`を独自に変更せず、最初のエラーを含むログとOS情報を添えて[Issues](https://github.com/norippy-i/PocketAudioDeck-WebRadio/issues)で相談してください。 |
| Uploadは成功したが起動しない | 電源／リセットボタンを短押しして再起動します。改善しなければMonitorで起動ログを確認してください。 |

**手動でダウンロードモードにする：** USB接続中に、本体側面の**電源／リセットボタン**を内部の緑色LEDが点滅するまで長押しします。
青いKEY1や、選曲ボタンではありません。[M5Stack公式のボタン位置・手順](https://docs.m5stack.com/en/core/StickS3#download-mode)も確認してください。
その後、**PROJECT TASKS → m5sticks3-manual → General → Upload** を実行します。
ダウンロードモードへの移行でUSBポート名が変わった場合は、ポートを選び直してください。

## ハードウェア

- M5StickS3
- Pocket Audio Deck外付けPCM5102A I2S DAC
- microSDカードスロット
- 前後選曲スイッチ
- 左右操作と押し込みに対応した音量コントロール
- ヘッドホン出力

## ハードウェア資料

- [回路図（PDF）](hardware/schematic/M5_PocketAudioDeck.pdf)
- [筐体上側モデル（STEP）](hardware/3d/PocketAudioDeck_Upper.step)
- [筐体下側モデル（STEP）](hardware/3d/PocketAudioDeck_Base.step)

## 組み立て

筐体は、イヤホンジャック側およびM5StickS3のUSBケーブルコネクタ側の両方を、M2 x 6 mmタッピングネジで締結します。

## 機能

- SomaFMのNon-SSL 128kbps MP3ストリーム4局
- ICY配信曲名表示と長い文字列の横スクロール
- PCMデータを使ったFFTスペクトラム表示
- microSD内MP3のID3タイトル/アーティスト優先表示（タグがない場合はファイル名を使用）
- 1曲リピート、全曲リピート、シャッフル再生
- 進捗バー/スペクトラム表示と長いメタデータの自動スクロール
- 曲送り時は設定音量を維持し、ミュート解除時のみフェードイン
- 放送局、動作モード、音量、EQ、MP3表示、リピートモード、最後に選んだ曲の保存
- ラジオ/MP3両モードの画面OFFとワンタッチ復帰
- QRコードとキャプティブポータルによるWi-Fi設定
- SDカードの挿入・取り外し自動検出と隠しシステムフォルダの除外

## 収録放送局

- Groove Salad
- Drone Zone
- Indie Pop Rocks!
- Space Station Soma

URLはSomaFM公式のNon-SSL MP3プレイリストに掲載されているものを使用しています。
SomaFMはリスナー支援型サービスであり、利用可能性と利用条件は同サービスに従います。
配信URLは変更される可能性があるため、[SomaFM公式Listenページ](https://somafm.com/listen/)も確認してください。

## Wi-Fi設定

このリポジトリにはWi-FiのSSIDとパスワードを保存していません。

1. ラジオモードで起動します。
2. KEY1を約2秒長押ししてWi-Fi設定用QRコードを表示します。
3. QRコードを読み取り、`PocketAudioDeck-Setup`へ接続します。
4. キャプティブポータルで接続先Wi-FiのSSIDとパスワードを入力します。
5. 設定を変更せず終了する場合は、KEY1をもう一度約2秒長押しします。

入力した認証情報はESP32のNVSへ保存されます。

## 操作

- SW1: 次の放送局またはMP3曲
- SW2: 前の放送局またはMP3曲
- 音量コントロール左/右: 音量ダウン/アップ
- 音量コントロール押し込み: ミュート切り替え
- MP3モードでKEY1短押し: 再生/一時停止
- WebラジオモードでKEY1短押し: 画面OFF
- MP3モードでKEY1を1秒長押し: 進捗バー/スペクトラム表示を切り替えて保存
- MP3モードでKEY1を4秒長押し: 表示モードを元に戻して画面OFF
- WebラジオモードでKEY1を2秒長押し: Wi-Fi設定画面の開始/終了
- 画面OFF中にKEY1を押す: 他の操作をせず画面を復帰
- MP3モードでKEY2短押し: リピートモード切り替え
- KEY2長押し: ラジオ/MP3モード切り替え
- KEY1 + KEY2: EQプリセット切り替え

## コマンドで書き込む場合

通常は上記のVS Code操作だけで書き込めます。コマンドを使う場合は、VS Code内の**PlatformIO Core CLI**で、`platformio.ini`があるフォルダから実行します。

```sh
pio run -e m5sticks3
pio run -e m5sticks3 -t upload
```

ポートの自動検出がうまくいかない場合は、`pio device list`で確認して明示します。下記のポート名は例です。

```sh
pio device list
# macOS
pio run -e m5sticks3 -t upload --upload-port /dev/cu.usbmodemXXXX
# Windows
pio run -e m5sticks3 -t upload --upload-port COM5
```

手動でダウンロードモードに入れた場合は、`-e m5sticks3-manual`へ変更します。
ログの確認には`pio device monitor -b 115200`を使用します。

macOS/LinuxでPlatformIOがPATHにある場合は、既存の補助スクリプトも使用できます。

```sh
./scripts/pio-local.sh run -e m5sticks3
./scripts/pio-local.sh run -e m5sticks3 -t upload
```

8MB FlashとPSRAMを搭載した構成を対象としています。
プリビルドスクリプトは、PCM可視化、出力ミュート、SDカード挿抜対応のため、ESP32-audioI2Sへ小さなフックを適用します。

## 放送局の追加

`src/main.cpp`の`kStations`へ項目を追加します。

```cpp
{"Station Name", "http://example.com/live-128-mp3"},
```

直接接続できるHTTP MP3ストリームURLを推奨します。
プレイリストページ、HTTPS専用ストリーム、AAC、HLS、認証が必要なサービスには追加実装が必要な場合があります。

## ライセンス

このリポジトリのファームウェアソースはGNU General Public License v3.0で公開しています。
本プロジェクトは、同じくGPL-3.0で公開されている
[ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S)を利用し、
ビルド時にローカルパッチを適用します。詳細は[LICENSE](LICENSE)を参照してください。
ラジオコンテンツ、放送局名、商標、その他のサードパーティライブラリは
各権利者に帰属し、このライセンスの対象外です。
