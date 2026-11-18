// kb_queue.h -- 8-byte HID keyboard report の順序保持キュー (Pico非依存)。
// 受信境界で press→neutral が連続しても落とさず、USB送出まで順序を保つ。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define KB_QUEUE_CAP 16u

typedef struct {
    uint8_t report[KB_QUEUE_CAP][8];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    uint32_t dropped;
} kb_queue_t;

void kb_queue_init(kb_queue_t *q);
/* 中身だけ捨てる。dropped は診断用に保持する。 */
void kb_queue_clear(kb_queue_t *q);
/* 8Bを積む。満杯時は dropped を増やし、drop_oldest=true なら最古を
 * 捨てて最新を残す。false なら最新を捨てて false を返す。 */
bool kb_queue_push(kb_queue_t *q, const uint8_t report[8], bool drop_oldest);
/* 先頭8Bを取り出す。空なら false。 */
bool kb_queue_pop(kb_queue_t *q, uint8_t out[8]);
uint8_t kb_queue_count(const kb_queue_t *q);
uint32_t kb_queue_dropped(const kb_queue_t *q);
