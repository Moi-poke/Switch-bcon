// PoC暫定コピー (origin: pico-wakecon src/btstack_config.h)。
// Task 4 (BT移植) で正式版に置換する。内容は wakecon と同一。
#ifndef _PICO_BTSTACK_BTSTACK_CONFIG_H
#define _PICO_BTSTACK_BTSTACK_CONFIG_H

// ==========================================================================
// BTstack の設定
//
// 土台は pico-examples の bluetooth/config/btstack_config_common.h である。
// Raspberry Pi 公式が実際に使っている設定であり、推測で書かず、
// 動いている実物を土台にする。不足があるとビルドが通らない
// （HCI_ACL_CHUNK_SIZE_ALIGNMENT が無いと cyw43 の転送層が弾く）。
//
// Classic だけを使う構成だが、#ifdef ENABLE_BLE のブロックは残す。
// 将来 BLE をリンクしたとき、この設定が自動で効くようにするためである。
// ==========================================================================

// --- 使う機能 -------------------------------------------------------------
#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP

#ifdef ENABLE_BLE
// Switch 2 の BLE ペアリング用。いまは定義されない。
#define ENABLE_GATT_CLIENT_PAIRING
#define ENABLE_L2CAP_LE_CREDIT_BASED_FLOW_CONTROL_MODE
#define ENABLE_LE_CENTRAL
#define ENABLE_LE_DATA_LENGTH_EXTENSION
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_PRIVACY_ADDRESS_RESOLUTION
#define ENABLE_LE_SECURE_CONNECTIONS
#define ENABLE_LE_ENHANCED_CONNECTION_COMPLETE_EVENT
#endif

#ifdef ENABLE_CLASSIC
#define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE
#define ENABLE_GOEP_L2CAP
#endif

#if defined (ENABLE_CLASSIC) && defined (ENABLE_BLE)
#define ENABLE_CROSS_TRANSPORT_KEY_DERIVATION
#endif

// --- 大きさの設定 ---------------------------------------------------------
// HCI_ACL_CHUNK_SIZE_ALIGNMENT が要点である。cyw43 の転送層は
// 4 の倍数でないと #error で止まる。
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE (1691 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4

#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 2
#define MAX_NR_GATT_CLIENTS 1
#define MAX_NR_HCI_CONNECTIONS 2
#define MAX_NR_L2CAP_CHANNELS 4
#define MAX_NR_L2CAP_SERVICES 3
#define MAX_NR_RFCOMM_CHANNELS 1
#define MAX_NR_RFCOMM_MULTIPLEXERS 1
#define MAX_NR_RFCOMM_SERVICES 1
#define MAX_NR_SERVICE_RECORD_ITEMS 4
#define MAX_NR_SM_LOOKUP_ENTRIES 3
#define MAX_NR_WHITELIST_ENTRIES 16
#define MAX_NR_LE_DEVICE_DB_ENTRIES 16

// cyw43 の共有バスが溢れないよう、使う buffer の数を抑える。
// 公式の設定をそのまま採る。減らす理由が無い。
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define MAX_NR_CONTROLLER_SCO_PACKETS 3

// 同じ理由で、Controller → Host の流量制御を入れる。
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN 1024
#define HCI_HOST_ACL_PACKET_NUM 3
#define HCI_HOST_SCO_PACKET_LEN 120
#define HCI_HOST_SCO_PACKET_NUM 3

// --- 保存先 ---------------------------------------------------------------
// リンクキーとボンド情報をフラッシュへ置く（TLV）。
// 将来 Switch 2 の LTK 保存にも使う。
#define NVM_NUM_DEVICE_DB_ENTRIES 16
#define NVM_NUM_LINK_KEYS 16

// malloc を渡さないので、ATT DB は固定長にする。
// 将来 GATT サーバを作るときに要る。
#define MAX_ATT_DB_SIZE 512

// --- 動かし方 -------------------------------------------------------------
#define HAVE_EMBEDDED_TIME_MS

// btstack_assert を Pico SDK の assert へ回す。
#define HAVE_ASSERT

// HCI reset の応答が遅い場合の待ち時間。公式の値をそのまま採る。
#define HCI_RESET_RESEND_TIMEOUT_MS 1000

// ENABLE_SCO_OVER_HCI は入れない。公式は音声（ヘッドセット）向けに
//   入れているが、コントローラのエミュレートには要らない。
//   入れると SCO 用の buffer が確保され、RAM を無駄に使う。

#define ENABLE_SOFTWARE_AES128
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS

// HAVE_BTSTACK_STDIN は定義しない。公式の例は標準入力から操作する
//   ので定義しているが、本プロジェクトは UART を自分で読むので要らない。
//   定義すると stdio を要求され、stdio を USB だけにしている本構成では
//   食い違う。

#endif // _PICO_BTSTACK_BTSTACK_CONFIG_H
