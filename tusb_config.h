/*
 * tusb_config.h - TinyUSB 設定ファイル
 *
 * Picossci 2 Tiny (RP2350A) で USB Host モードを使用し、
 * USB Hub 経由で HID キーボードと FT232RL (CDC/FTDI) を同時接続する
 */

#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// 共通設定
//--------------------------------------------------------------------
#define CFG_TUSB_RHPORT0_MODE       OPT_MODE_HOST
#define CFG_TUSB_OS                 OPT_OS_PICO
#define CFG_TUSB_MEM_ALIGN          __attribute__((aligned(4)))

//--------------------------------------------------------------------
// USB Host 設定
//--------------------------------------------------------------------
#define CFG_TUH_ENABLED             1
#define CFG_TUH_MAX_SPEED           OPT_MODE_FULL_SPEED

// 接続可能デバイス数: Hub(1) + Keyboard(1) + FT232RL(1) + 予備
#define CFG_TUH_DEVICE_MAX          5

// 列挙時のバッファサイズ
#define CFG_TUH_ENUMERATION_BUFSIZE 256

//--------------------------------------------------------------------
// Hub サポート（USB Hub 経由接続に必須）
//--------------------------------------------------------------------
#define CFG_TUH_HUB                 1

//--------------------------------------------------------------------
// HID サポート（USB キーボード用）
//--------------------------------------------------------------------
#define CFG_TUH_HID                 4
#define CFG_TUH_HID_EPIN_BUFSIZE    64

//--------------------------------------------------------------------
// CDC サポート（FT232RL 用）
// TinyUSB の CDC Host ドライバは FTDI チップをネイティブサポート
//--------------------------------------------------------------------
#define CFG_TUH_CDC                 2
#define CFG_TUH_CDC_FTDI            1
// CP210x や CH34x も使う場合は以下を有効化
// #define CFG_TUH_CDC_CP210X       1
// #define CFG_TUH_CDC_CH34X        1

// CDC 受信 FIFO サイズ
#define CFG_TUH_CDC_RX_BUFSIZE      256
#define CFG_TUH_CDC_TX_BUFSIZE      256

//--------------------------------------------------------------------
// 受信エンドポイントサイズ
//--------------------------------------------------------------------
#define CFG_TUH_CDC_LINE_CODING_ON_ENUM  { 115200, 0, 0, 8 }

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H */
