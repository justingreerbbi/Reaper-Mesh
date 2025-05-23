#pragma once
// ─── LoRa-specific compile-time constants ────────────────────────────────────
#define LORA_BANDWIDTH        500.0     // kHz
#define LORA_SPREADING_FACTOR 10
#define LORA_CODING_RATE      5         // 4/5
#define LORA_PREAMBLE_LENGTH  20
#define LORA_SYNC_WORD        0xF3
#define LORA_CRC              true
// AES / fragmentation
#define AES_BLOCK_LEN         16
#define FRAG_HEADER_SIZE      6
#define MAX_FRAGMENT_SIZE     96        // 6 header + 90 payload (multiple of 16)
#define FRAG_DATA_LEN         (MAX_FRAGMENT_SIZE - FRAG_HEADER_SIZE)
// fragment types
#define TYPE_TEXT_FRAGMENT    0x03
#define TYPE_ACK_FRAGMENT     0x04
#define TYPE_ACK_CONFIRM      0x08
#define BROADCAST_MEMORY_TIME 30000UL   // ms
