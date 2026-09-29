#include "signal_filter.h"

#include <math.h>
#include <string.h>

void signal_filter_init(signal_filter_t *filter, float alpha, float min_valid, float max_valid)
{
    memset(filter, 0, sizeof(*filter));
    filter->alpha = alpha;
    filter->min_valid = min_valid;
    filter->max_valid = max_valid;
}

void signal_filter_reset(signal_filter_t *filter)
{
    signal_filter_init(filter, filter->alpha, filter->min_valid, filter->max_valid);
}

static float window_median(const signal_filter_t *filter)
{
    float sorted[SIGNAL_FILTER_WINDOW];
    uint8_t n = filter->count;
    memcpy(sorted, filter->window, n * sizeof(float));
    for (uint8_t i = 1; i < n; i++) {
        float value = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > value) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = value;
    }
    /* So mau chan (luc khoi dong) lay trung binh hai phan tu giua. */
    return (n % 2) ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0f;
}

bool signal_filter_update(signal_filter_t *filter, float raw)
{
    if (!isfinite(raw) || raw < filter->min_valid || raw > filter->max_valid) {
        return false;
    }

    filter->window[filter->index] = raw;
    filter->index = (filter->index + 1) % SIGNAL_FILTER_WINDOW;
    if (filter->count < SIGNAL_FILTER_WINDOW) {
        filter->count++;
    }

    /* Can it nhat 3 mau de median loai duoc mot gai nhieu ngay tu dau. */
    if (filter->count < 3) {
        return true;
    }
    float median = window_median(filter);
    if (!filter->ready) {
        filter->output = median;
        filter->ready = true;
    } else {
        filter->output += filter->alpha * (median - filter->output);
    }
    return true;
}

bool signal_filter_get(const signal_filter_t *filter, float *out)
{
    if (!filter->ready) {
        return false;
    }
    *out = filter->output;
    return true;
}
