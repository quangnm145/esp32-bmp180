#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SIGNAL_FILTER_WINDOW 5

/* Loc 3 tang cho mot kenh do:
 *  1. Loai gia tri ngoai dai vat ly [min_valid, max_valid].
 *  2. Median truot 5 mau: bo gai nhieu don le, giu nguyen xu huong tang/giam deu.
 *  3. EMA (alpha): lam muot, tre co dinh nen khong meo do doc. */
typedef struct {
    float window[SIGNAL_FILTER_WINDOW];
    uint8_t count;
    uint8_t index;
    float alpha;
    float min_valid;
    float max_valid;
    float output;
    bool ready;
} signal_filter_t;

void signal_filter_init(signal_filter_t *filter, float alpha, float min_valid, float max_valid);
void signal_filter_reset(signal_filter_t *filter);

/* Tra ve false neu raw bi loai (ngoai dai hoac khong phai so). */
bool signal_filter_update(signal_filter_t *filter, float raw);

/* Tra ve false khi chua co mau hop le nao. */
bool signal_filter_get(const signal_filter_t *filter, float *out);
