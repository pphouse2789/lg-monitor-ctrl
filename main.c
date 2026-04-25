/*
 * main.c - Picossci 2 Tiny (RP2350A) USB キーボード → FT232RL シリアル送信
 *
 * 【概要】
 *   USB Hub 経由で接続された USB キーボードの CTRL+キー 入力を検出し、
 *   対応する ASCII 文字列を FT232RL (USB-UART 変換) へ送信する。
 *   応答の OK/NG に応じて基板上の LED を点滅させる。
 *
 * 【ハードウェア接続】
 *   Picossci 2 Tiny (USB-C) ─── USB Hub ─┬─ USB キーボード
 *                                          └─ FT232RL モジュール
 *
 *   ※ Picossci 2 Tiny は USB Host モードで動作するため、
 *     外部 5V 電源(5Vピン経由)または セルフパワー USB Hub が必要です。
 *
 * 【LED ピン番号について】
 *   LED1_PIN / LED2_PIN は Picossci 2 Tiny の回路図を参照して設定してください。
 *   回路図は Switch Science 製品ページからダウンロードできます。
 *   ※ Lチカで特定したピン番号をここに記入してください。
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "tusb.h"

/* ================================================================
 *  ユーザー設定 - 基板に合わせて変更してください
 * ================================================================ */

// LED ピン番号（回路図で確認し、Lチカで特定した値を設定）
#define LED1_PIN            25  // 黄緑 LED（成功時に点滅）
#define LED2_PIN            24  // 赤 LED  （失敗時に点滅）

// LED 点滅時間 [ms]
#define BLINK_ON_MS         200
#define BLINK_OFF_MS        200

// FT232RL のボーレート
#define FTDI_BAUD_RATE      9600

// コマンド送信後の応答タイムアウト [ms]
#define RESPONSE_TIMEOUT_MS 2000

/* ================================================================
 *  HID キーコード定義
 * ================================================================ */
#define HID_KEYCODE_A   0x04
#define HID_KEYCODE_C   0x06
#define HID_KEYCODE_F   0x09
#define HID_KEYCODE_S   0x16
#define HID_KEYCODE_V   0x19
#define HID_KEYCODE_X   0x1B

// Modifier ビットマスク
#define MOD_LCTRL       0x01
#define MOD_RCTRL       0x10
#define MOD_CTRL_ANY    (MOD_LCTRL | MOD_RCTRL)

/* ================================================================
 *  キーマッピング定義
 *  - commands[] にはCR(\r)で送信するコマンド文字列を格納
 *  - 最後は NULL で終端
 * ================================================================ */
typedef struct {
    uint8_t     keycode;        // HID キーコード
    const char *commands[4];    // 送信コマンド列 (NULL終端)
} key_mapping_t;

static const key_mapping_t KEY_MAP[] = {
    //  キー         コマンド1       コマンド2       終端
    { HID_KEYCODE_C, { "ka 00 01", "xb 00 90", NULL } },   // CTRL+c
    { HID_KEYCODE_V, { "ka 00 01", "xb 00 91", NULL } },   // CTRL+v
    { HID_KEYCODE_F, { "ka 00 01", "xb 00 92", NULL } },   // CTRL+f
    { HID_KEYCODE_S, { "ka 00 01", "xb 00 93", NULL } },   // CTRL+s
    { HID_KEYCODE_X, { "ka 00 01", "xb 00 E0", NULL } },   // CTRL+x
    { HID_KEYCODE_A, { "ka 00 00", "ka 00 00", NULL } },    // CTRL+a
};
#define KEY_MAP_COUNT (sizeof(KEY_MAP) / sizeof(KEY_MAP[0]))

/* ================================================================
 *  グローバル状態
 * ================================================================ */
static int  g_ftdi_idx  = -1;      // FT232RL の CDC インデックス (-1=未接続)
static bool g_kbd_ready = false;    // キーボード接続済みフラグ

// 前回の HID レポート（キー押下検出用）
static uint8_t g_prev_report[8] = {0};

// 応答受信バッファ
#define RESP_BUF_SIZE 256
static char g_resp_buf[RESP_BUF_SIZE];
static int  g_resp_pos = 0;

/* ================================================================
 *  LED 制御
 * ================================================================ */
static void led_init(void)
{
    gpio_init(LED1_PIN);
    gpio_set_dir(LED1_PIN, GPIO_OUT);
    gpio_put(LED1_PIN, 0);

    gpio_init(LED2_PIN);
    gpio_set_dir(LED2_PIN, GPIO_OUT);
    gpio_put(LED2_PIN, 0);
}

/// LED を1回点滅させる（ブロッキング）
static void blink_led(uint pin)
{
    gpio_put(pin, 1);
    sleep_ms(BLINK_ON_MS);
    gpio_put(pin, 0);
    sleep_ms(BLINK_OFF_MS);
}

/// LED1 と LED2 を同時に短く1回点滅（キー検出通知用）
static void blink_both_short(void)
{
    gpio_put(LED1_PIN, 1);
    gpio_put(LED2_PIN, 1);
    sleep_ms(80);
    gpio_put(LED1_PIN, 0);
    gpio_put(LED2_PIN, 0);
    sleep_ms(80);
}

/* ================================================================
 *  FT232RL 通信
 * ================================================================ */

/**
 * コマンドを1行送信し、応答を待つ
 *
 * @param cmd  送信文字列（\r は本関数が付加する）
 * @return  1=OK受信, 0=NG受信, -1=タイムアウトまたは未接続
 */
static int send_command_and_wait(const char *cmd)
{
    // FT232RL 未接続チェック
    if (g_ftdi_idx < 0 || !tuh_cdc_mounted((uint8_t)g_ftdi_idx)) {
        printf("[FTDI] Not connected\n");
        return -1;
    }

    uint8_t idx = (uint8_t)g_ftdi_idx;

    // --- 送信 ---
    printf("[TX] %s\\r\n", cmd);

    uint32_t cmd_len = (uint32_t)strlen(cmd);
    tuh_cdc_write(idx, cmd, cmd_len);
    tuh_cdc_write(idx, "\r", 1);
    tuh_cdc_write_flush(idx);

    // --- 応答待ち ---
    g_resp_pos = 0;
    memset(g_resp_buf, 0, RESP_BUF_SIZE);

    absolute_time_t deadline = make_timeout_time_ms(RESPONSE_TIMEOUT_MS);

    while (!time_reached(deadline)) {
        // USB イベントを処理（受信データの取り込み含む）
        tuh_task();

        // FT232RL が切断されていないかチェック
        if (!tuh_cdc_mounted(idx)) {
            printf("[FTDI] Disconnected during wait\n");
            return -1;
        }

        // 受信データを読み取り
        uint32_t avail = tuh_cdc_read_available(idx);
        if (avail > 0) {
            uint32_t space = (uint32_t)(RESP_BUF_SIZE - g_resp_pos - 1);
            uint32_t to_read = (avail < space) ? avail : space;
            if (to_read > 0) {
                uint32_t n = tuh_cdc_read(idx, g_resp_buf + g_resp_pos, to_read);
                g_resp_pos += (int)n;
                g_resp_buf[g_resp_pos] = '\0';
            }

            // 応答に "OK" または "NG" が含まれるかチェック
            if (strstr(g_resp_buf, "OK") != NULL) {
                printf("[RX] %s -> OK\n", g_resp_buf);
                return 1;  // 成功
            }
            if (strstr(g_resp_buf, "NG") != NULL) {
                printf("[RX] %s -> NG\n", g_resp_buf);
                return 0;  // 失敗
            }
        }

        sleep_us(100);  // CPU負荷軽減
    }

    printf("[RX] Timeout (buf='%s')\n", g_resp_buf);
    return -1;  // タイムアウト
}

/* ================================================================
 *  キー入力処理
 * ================================================================ */

/**
 * キーマッピングに対応するコマンド列を順次送信し、
 * 結果に応じて LED を点滅させる
 */
static void execute_key_mapping(const key_mapping_t *mapping)
{
    bool all_ok = true;

    for (int i = 0; mapping->commands[i] != NULL; i++) {
        int result = send_command_and_wait(mapping->commands[i]);

        if (result != 1) {
            // NG またはタイムアウト → 失敗
            all_ok = false;
            break;  // 以降のコマンドは送信しない
        }
    }

    if (all_ok) {
        printf("[LED] Success -> LED1 blink\n");
        blink_led(LED1_PIN);   // 全て OK → LED1（黄緑）を1回点滅
    } else {
        printf("[LED] Failure -> LED2 blink\n");
        blink_led(LED2_PIN);   // NG/タイムアウト → LED2（赤）を1回点滅
    }
}

/**
 * HID キーボードレポートを処理し、
 * 新しい CTRL+キー の押下を検出する
 */
static void process_keyboard_report(uint8_t const *report)
{
    uint8_t modifiers = report[0];
    bool ctrl_held = (modifiers & MOD_CTRL_ANY) != 0;

    // CTRL が押されていなければスキップ
    if (!ctrl_held) {
        memcpy(g_prev_report, report, 8);
        return;
    }

    // Bytes 2-7: 現在押されているキーコード（最大6キー同時）
    for (int i = 2; i < 8; i++) {
        uint8_t keycode = report[i];
        if (keycode == 0) continue;

        // 前回のレポートに含まれていない = 新規押下
        bool is_new_press = true;
        for (int j = 2; j < 8; j++) {
            if (g_prev_report[j] == keycode) {
                is_new_press = false;
                break;
            }
        }

        if (is_new_press) {
            // キーマッピングを検索
            for (int m = 0; m < (int)KEY_MAP_COUNT; m++) {
                if (KEY_MAP[m].keycode == keycode) {
                    printf("[KBD] CTRL+%c detected\n",
                           'a' + (keycode - HID_KEYCODE_A));
                    blink_both_short();  // キー入力検出 = LED1+LED2 短く点滅
                    execute_key_mapping(&KEY_MAP[m]);
                    break;
                }
            }
        }
    }

    memcpy(g_prev_report, report, 8);
}

/* ================================================================
 *  TinyUSB Host コールバック: HID (キーボード)
 * ================================================================ */

/// HID デバイスが接続された
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                       uint8_t const *desc_report, uint16_t desc_len)
{
    (void)desc_report;
    (void)desc_len;

    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
        printf("[USB] Keyboard mounted (dev=%d, inst=%d)\n", dev_addr, instance);
        g_kbd_ready = true;
        blink_led(LED1_PIN);  // キーボード接続 = LED1 点滅

        // Boot プロトコルに設定（8バイト固定長レポート）
        if (!tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_BOOT)) {
            printf("[USB] Failed to set boot protocol\n");
        }

        // レポート受信を開始
        if (!tuh_hid_receive_report(dev_addr, instance)) {
            printf("[USB] Failed to start report reception\n");
        }
    }
}

/// HID デバイスが切断された
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    printf("[USB] HID unmounted (dev=%d, inst=%d)\n", dev_addr, instance);
    g_kbd_ready = false;
    memset(g_prev_report, 0, sizeof(g_prev_report));
}

/// HID レポートを受信した
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                 uint8_t const *report, uint16_t len)
{
    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD && len >= 8) {
        process_keyboard_report(report);
    }

    // 次のレポートを継続受信
    tuh_hid_receive_report(dev_addr, instance);
}

/* ================================================================
 *  TinyUSB Host コールバック: CDC (FT232RL)
 * ================================================================ */

/// CDC デバイスが接続された（FT232RL 検出時）
void tuh_cdc_mount_cb(uint8_t idx)
{
    printf("[USB] CDC/FTDI mounted (idx=%d)\n", idx);
    g_ftdi_idx = (int)idx;
    blink_led(LED2_PIN);  // FT232RL 接続 = LED2 点滅

    // ボーレートを設定
    tuh_cdc_set_baudrate(idx, FTDI_BAUD_RATE, NULL, 0);

    // ライン設定: 8N1 (8データビット, パリティなし, 1ストップビット)
    cdc_line_coding_t line_coding = {
        .bit_rate   = FTDI_BAUD_RATE,
        .stop_bits  = 0,    // 0=1ストップビット
        .parity     = 0,    // 0=パリティなし
        .data_bits  = 8
    };
    tuh_cdc_set_line_coding(idx, &line_coding, NULL, 0);

    printf("[FTDI] Configured: %d baud, 8N1\n", FTDI_BAUD_RATE);
}

/// CDC デバイスが切断された
void tuh_cdc_umount_cb(uint8_t idx)
{
    printf("[USB] CDC/FTDI unmounted (idx=%d)\n", idx);
    if (g_ftdi_idx == (int)idx) {
        g_ftdi_idx = -1;
    }
}

/// CDC データ受信通知（受信自体はポーリングで行うので、ここは空）
void tuh_cdc_rx_cb(uint8_t idx)
{
    (void)idx;
    // 実際の読み取りは send_command_and_wait() 内で実施
}

/* ================================================================
 *  メイン関数
 * ================================================================ */
int main(void)
{
    // --- 基本初期化 ---
    // ※ stdio は UART 経由 (GPIO0=TX, GPIO1=RX) でデバッグ出力
    stdio_init_all();

    printf("\n");
    printf("========================================\n");
    printf(" Picossci 2 Tiny - KBD to UART Bridge\n");
    printf("========================================\n");
    printf("  LED1 (success) : GPIO%d\n", LED1_PIN);
    printf("  LED2 (failure) : GPIO%d\n", LED2_PIN);
    printf("  FTDI baud rate : %d\n", FTDI_BAUD_RATE);
    printf("  Timeout        : %d ms\n", RESPONSE_TIMEOUT_MS);
    printf("========================================\n\n");

    // --- LED 初期化 ---
    led_init();

    // --- TinyUSB Host 初期化 ---
    tusb_init();

    // 起動インジケータ: LED1 を素早く3回点滅 = ファームウェア起動確認
    printf("[INIT] Starting up...\n");
    for (int i = 0; i < 3; i++) {
        gpio_put(LED1_PIN, 1);
        sleep_ms(100);
        gpio_put(LED1_PIN, 0);
        sleep_ms(100);
    }
    printf("[INIT] Waiting for USB devices...\n");

    // --- メインループ ---
    while (true) {
        // TinyUSB Host タスク（USB イベント処理）
        // この中で HID レポート受信コールバック等が呼ばれる
        tuh_task();
    }

    return 0;
}
