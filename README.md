# ble-residency-monitor

M5Stack Basic 向けの BLE 在室検知プロトタイプ（案C）。一度だけボンディングしてスマホの **IRK (Identity Resolving Key)** を取得し、以後は接続せずパッシブスキャンだけで RPA をソフト解決して個人を識別する実験用ファームウェアです。

## ハードウェア

- M5Stack Basic（無印 ESP32）
- BLE 4.2（ESP32 内蔵）

## 開発環境

- [PlatformIO](https://platformio.org/)
- Arduino framework
- [M5Unified](https://github.com/m5stack/M5Unified)

## ビルド・書き込み

```bash
pio run
pio run -t upload
pio device monitor
```

シリアルモニタは 115200 bps。

## 操作

| ボタン | 機能 |
|--------|------|
| **A** | Enrollment モード（IRK 取得） |
| **B** | Monitoring モード（在室監視） |
| **C** | 登録一覧クリア |

### Enrollment（登録）

1. ボタン **A** を押す（画面が `Enrollment` になる）
2. iPhone で **LightBlue** または **nRF Connect** を開く（設定アプリの Bluetooth 一覧には表示されません）
3. `LabPresence-Enroll` に接続し、ペアリングを承認
4. シリアルに `[enroll] peer RPA verified at bond time` が出れば IRK 取得成功

### Monitoring（監視）

1. ボタン **B** を押す
2. 登録済み端末の RPA が解決されると `[scan] MATCH ... -> UserN` が出る
3. TFT に在室状態（IN/OUT）と RSSI が表示される

## アーキテクチャ

```
Enrollment  →  GATT サーバ + ボンディング  →  IRK を NVS に保存
Monitoring  →  パッシブスキャン  →  ah(IRK, prand) で RPA 解決  →  在室判定
```

- **BLE スタック**: Bluedroid（`esp_ble_get_bond_device_list()` で IRK 取得）
- **RPA 解決**: mbedTLS AES-128-ECB（コントローラの resolving list は使わない）
- **永続化**: Preferences（NVS）
- **在室判定**: 最終受信から 45 秒以内を IN、RSSI は EMA で平滑化

## プロジェクト構成

```
src/
  main.cpp           # モード切替・UI
  enrollment.cpp     # ボンディング・IRK 取得
  monitoring.cpp     # パッシブスキャン・在室判定
  rpa_resolve.cpp    # ah() / RPA ソフト解決
  enrollee.cpp       # 登録データ・NVS
  ble_lifecycle.cpp  # BLE init/deinit（モード切替）
include/
  *.h
```

## 既知の注意点

- **IRK のバイト順**: ESP32 Bluedroid から取得した IRK はバイト逆順（variant=1）で正規化して保存します。Enrollment 時に `peer RPA verified at bond time (variant=1)` が出ることを確認してください。
- **iPhone の制約**: ボンド時の IRK で解決できる RPA と、日常のバックグラウンド advertise で使われる RPA が異なる可能性があります。ロック画面・長時間放置での MATCH 継続を必ず実機確認してください。
- **モード切替**: `BLEDevice::deinit(true)` は使わず、BT メモリを解放しない `deinit(false)` で切り替えます。

## M3 検証（go/no-go）

| 端末 | 画面 ON | 画面 OFF / ロック |
|------|---------|-------------------|
| Android | 未検証 | 未検証 |
| iPhone | MATCH 確認済み (`4E:EB:62:4D:CA:3C`) | 要確認 |

## ライセンス

検証用プロトタイプ。IRK は端末追跡の鍵となるため、取り扱いと同意取得に注意してください。
