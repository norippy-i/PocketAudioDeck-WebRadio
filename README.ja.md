# Pocket Audio Deck Web Radio

[English](README.md)

M5StickS3とPocket Audio Deck専用基板向けの、PlatformIO/Arduinoファームウェアです。
公開HTTP MP3 Webラジオを基板上のPCM5102A DACから再生でき、microSD内のMP3ファイル再生にも対応します。

この公開版には、地域制限サービスの認証、非公開エンドポイント、地域放送局データ、および関連する再生処理は含まれていません。

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
- microSD内MP3のメタデータ、進捗、残り時間表示
- 1曲リピート、全曲リピート、シャッフル再生
- 放送局、動作モード、音量、EQ設定の保存
- QRコードとキャプティブポータルによるWi-Fi設定
- SDカードの挿入・取り外し自動検出

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
2. KEY1を長押ししてWi-Fi設定用QRコードを表示します。
3. QRコードを読み取り、`PocketAudioDeck-Setup`へ接続します。
4. キャプティブポータルで接続先Wi-FiのSSIDとパスワードを入力します。
5. 設定を変更せず終了する場合は、KEY1をもう一度長押しします。

入力した認証情報はESP32のNVSへ保存されます。

## 操作

- SW1: 次の放送局またはMP3曲
- SW2: 前の放送局またはMP3曲
- 音量コントロール左/右: 音量ダウン/アップ
- 音量コントロール押し込み: ミュート切り替え
- MP3モードでKEY1短押し: 再生/一時停止
- MP3モードでKEY1長押し: 進捗バー/スペクトラム表示切り替え
- ラジオモードでKEY1長押し: Wi-Fi設定画面の開始/終了
- MP3モードでKEY2短押し: リピートモード切り替え
- KEY2長押し: ラジオ/MP3モード切り替え
- KEY1 + KEY2: EQプリセット切り替え

## ビルド

```sh
./scripts/pio-local.sh run
```

M5StickS3のシリアルポートを指定して書き込みます。

```sh
./scripts/pio-local.sh run -t upload --upload-port /dev/cu.usbmodemXXXX
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

このリポジトリのファームウェアソースはMIT Licenseで公開しています。
ラジオコンテンツ、放送局名、商標、サードパーティライブラリは各権利者に帰属し、このライセンスの対象外です。
