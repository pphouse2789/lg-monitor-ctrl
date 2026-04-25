# LG 43UN700 RS232C コントローラー

LG電子の43UN700シリーズモニターに対して、USB キーボードから RS232C 経由で操作するファームウェアです。
6キーキーボードの各キーに電源ON/入力切替・電源OFFを割り当て、ワンキーで操作できます。

## 使用ハードウェア

| 機器 | 型番・購入先 |
|------|-------------|
| マイコンボード | [Picossci 2 Tiny (RP2350A)](https://www.switch-science.com/products/9797) |
| キーボード | [6キーキーボード](https://www.amazon.co.jp/dp/B0FN3HRL25) |
| USB-RS232C 変換 | [USB-RS232C 変換ケーブル](https://www.amazon.co.jp/dp/B075VKZ84S) |
| USB Hub | サンワサプライ 400-HUBCP38BK（Type-C 給電対応） |

### USB-RS232C 変換ケーブルの注意事項

ケーブルをカットして TX と RX を入れ替える必要があります。

## ハードウェア構成

```
              AC アダプタ
                   │
    ┌──────────────┴─────────────────────┐
    │       400-HUBCP38BK                │
    │    (USB Hub + Type-C 給電)         │
    ├──────────┬──────────┬──────────────┤
    │          │          │
 6キー    USB-RS232C   Type-C (給電 + USB Host)
キーボード  変換ケーブル      │
                │           │
                │    ┌──────┴───────────────┐
                │    │    Picossci 2 Tiny   │
                │    │      (RP2350A)       │
                │    │ LED1(黄緑, GP25)     │
                │    │ LED2(赤,   GP24)     │
                │    └─────────────────────┘
                │
                │ RS232C
                ▼
         LG 43UN700 シリーズ
```

### 電源について

Picossci 2 Tiny を USB Host モードで使うには USB-C コネクタから電源を取れないため、
USB Hub (400-HUBCP38BK) の Type-C ポートから給電します。

### デバッグ出力 (UART)

GPIO0 (TX) / GPIO1 (RX) に USB-UART 変換器を接続すると、
PC のターミナルソフトでデバッグログを確認できます（115200 baud）。

## キー操作と動作

| キー | 動作 | 送信コマンド |
|------|------|-------------|
| Copy  | 電源ON + HDMI1 入力切替 | `ka 00 01\r` → OK確認 → `ka 00 90\r` |
| Paste | 電源ON + HDMI2 入力切替 | `ka 00 01\r` → OK確認 → `ka 00 91\r` |
| Search | 電源ON + HDMI3 入力切替 | `ka 00 01\r` → OK確認 → `ka 00 92\r` |
| Save  | 電源ON + HDMI4 入力切替 | `ka 00 01\r` → OK確認 → `ka 00 93\r` |
| Cut   | 電源ON + Type-C 入力切替 | `ka 00 01\r` → OK確認 → `ka 00 E0\r` |
| All   | 電源OFF | `ka 00 00\r` → OK確認 → `ka 00 00\r` |

- 各コマンドは CR (`\r`) で終端して送信
- 1つ目のコマンド送信後、モニターからの `OK` 応答を確認してから2つ目を送信
- `OK` が確認できない場合は2つ目を送信せず LED2 を点滅

## LED 動作

| タイミング | LED の動作 |
|-----------|-----------|
| 起動時 | LED1（黄緑）が素早く3回点滅 |
| USB キーボード接続 | LED1（黄緑）が1回点滅 |
| USB-RS232C 変換器接続 | LED2（赤）が1回点滅 |
| キー入力検出 | LED1 + LED2 が同時に短く1回点滅 |
| 全コマンド OK | LED1（黄緑）が1回点滅 |
| NG またはタイムアウト | LED2（赤）が1回点滅 |

## ビルド方法

### 前提条件

- [Pico SDK 2.0 以上](https://github.com/raspberrypi/pico-sdk)
- CMake 3.13 以上
- ARM GCC ツールチェーン (arm-none-eabi-gcc)

### 手順

```bash
# 1. 環境変数を設定
export PICO_SDK_PATH=/path/to/pico-sdk

# 2. ビルドディレクトリを作成
cd picossci2_kbd_uart
mkdir build && cd build

# 3. CMake 構成（PICO_BOARD を明示的に指定）
cmake -DPICO_BOARD=pico2 ..

# 4. ビルド
make -j$(nproc)
```

ビルド成功後、`picossci2_kbd_uart.uf2` が生成されます。

### 書き込み

1. Picossci 2 Tiny の **BOOTSEL ボタンを押しながら** USB ケーブルで PC に接続
2. `RP2350` USB ドライブが現れる
3. `picossci2_kbd_uart.uf2` をコピー
4. 自動で再起動し、ファームウェアが起動

## トラブルシューティング

| 症状 | 対処法 |
|------|--------|
| USB デバイスが認識されない | Hub の Type-C 給電を確認 |
| キーボードが反応しない | デバッグ UART で mount ログを確認 |
| RS232C 変換器が認識されない | `tusb_config.h` の `CFG_TUH_CDC_FTDI` を確認 |
| 応答がタイムアウトする | TX/RX の入れ替えを確認、ボーレート設定を確認 |
| LED が光らない | LED ピン番号 (LED1=GP25, LED2=GP24) を確認 |
