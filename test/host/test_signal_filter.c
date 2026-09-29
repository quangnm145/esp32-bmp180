/* Host unit tests for lib/signal_filter. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "signal_filter.h"
#include "test_common.h"

#define ALPHA 0.3f /* same as FILTER_ALPHA in sensor_service.c */

/* Deterministic PRNG so the test is reproducible. */
static uint32_t s_rng = 12345u;
static float noise(float amplitude)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    float u = (float)(s_rng >> 8) / (float)(1u << 24); /* [0,1) */
    return (2.0f * u - 1.0f) * amplitude;
}

static void test_rejects_invalid(void)
{
    signal_filter_t f;
    float out = 0;
    signal_filter_init(&f, ALPHA, -40.0f, 85.0f);

    CHECK(!signal_filter_get(&f, &out), "get before any sample must fail");
    CHECK(!signal_filter_update(&f, NAN), "NaN accepted");
    CHECK(!signal_filter_update(&f, INFINITY), "+Inf accepted");
    CHECK(!signal_filter_update(&f, -INFINITY), "-Inf accepted");
    CHECK(!signal_filter_update(&f, -40.01f), "below min accepted");
    CHECK(!signal_filter_update(&f, 85.01f), "above max accepted");
    CHECK(!signal_filter_get(&f, &out), "rejected samples must not make filter ready");

    CHECK(signal_filter_update(&f, -40.0f), "min boundary rejected");
    CHECK(!signal_filter_get(&f, &out), "output must wait for 3 samples");
    signal_filter_update(&f, -40.0f);
    CHECK(!signal_filter_get(&f, &out), "output must wait for 3 samples");
    signal_filter_update(&f, -40.0f);
    CHECK(signal_filter_get(&f, &out) && out == -40.0f, "first output should equal the median, got %f", out);
    signal_filter_reset(&f);
    CHECK(!signal_filter_get(&f, &out), "reset must clear ready");
    CHECK(signal_filter_update(&f, 85.0f), "max boundary rejected");

    /* Rejected values must not disturb the state. */
    signal_filter_reset(&f);
    for (int i = 0; i < 10; i++) {
        signal_filter_update(&f, 25.0f);
    }
    signal_filter_update(&f, NAN);
    signal_filter_update(&f, 200.0f);
    signal_filter_get(&f, &out);
    CHECK(fabsf(out - 25.0f) < 1e-5f, "state disturbed by rejected sample: %f", out);
}

static void test_single_spike_removed(void)
{
    signal_filter_t f;
    float out = 0, max_dev = 0;
    signal_filter_init(&f, ALPHA, -40.0f, 85.0f);
    for (int i = 0; i < 40; i++) {
        float raw = 25.0f + (i == 20 ? 10.0f : 0.0f);
        CHECK(signal_filter_update(&f, raw), "valid sample rejected");
        if (!signal_filter_get(&f, &out)) {
            continue;
        }
        float dev = fabsf(out - 25.0f);
        if (dev > max_dev) {
            max_dev = dev;
        }
    }
    CHECK(max_dev < 0.01f, "single spike leaked through median: max dev %f", max_dev);

    /* Two adjacent spikes are still removed by a 5-sample median. */
    signal_filter_reset(&f);
    max_dev = 0;
    for (int i = 0; i < 40; i++) {
        float raw = 25.0f + ((i == 20 || i == 21) ? 10.0f : 0.0f);
        signal_filter_update(&f, raw);
        signal_filter_get(&f, &out);
        if (fabsf(out - 25.0f) > max_dev) {
            max_dev = fabsf(out - 25.0f);
        }
    }
    CHECK(max_dev < 0.01f, "double spike leaked through median: max dev %f", max_dev);

    /* A spike as the very first sample must not reach the output. */
    signal_filter_reset(&f);
    signal_filter_update(&f, 33.0f);
    signal_filter_update(&f, 25.0f);
    signal_filter_update(&f, 25.0f);
    CHECK(signal_filter_get(&f, &out) && fabsf(out - 25.0f) < 1e-5f,
          "start-up spike leaked: output %f", out);
}

/* Triangle 20 -> 35 -> 20 over 120 samples, like the firmware fake-data mode. */
#define PERIOD 120
#define TICKS (3 * PERIOD)
static float triangle(int tick)
{
    int p = tick % PERIOD;
    int half = PERIOD / 2;
    float frac = p < half ? (float)p / half : (float)(PERIOD - p) / half;
    return 20.0f + 15.0f * frac;
}

static void test_triangle(void)
{
    signal_filter_t f, f_clean;
    signal_filter_init(&f, ALPHA, -40.0f, 85.0f);
    signal_filter_init(&f_clean, ALPHA, -40.0f, 85.0f);
    float filtered[TICKS];
    float max_err = 0, max_spike_effect = 0, max_above_true = 0;
    int max_err_tick = -1;

    FILE *csv = fopen("out/filter_triangle.csv", "w");
    CHECK(csv != NULL, "cannot open out/filter_triangle.csv");
    if (csv) {
        fprintf(csv, "tick,true,raw,filtered\n");
    }

    for (int t = 0; t < TICKS; t++) {
        float truth = triangle(t);
        float n = noise(0.1f);
        float spike = (t % 25 == 24) ? 8.0f : 0.0f;
        float raw = truth + n + spike;
        float out, out_clean;

        CHECK(signal_filter_update(&f, raw), "valid sample rejected at %d", t);
        signal_filter_update(&f_clean, truth + n);
        signal_filter_get(&f, &out);
        signal_filter_get(&f_clean, &out_clean);
        filtered[t] = out;
        if (csv) {
            fprintf(csv, "%d,%.3f,%.3f,%.3f\n", t, truth, raw, out);
        }

        if (t >= 10) { /* after warm-up */
            float err = fabsf(out - truth);
            if (err > max_err) {
                max_err = err;
                max_err_tick = t;
            }
            float effect = fabsf(out - out_clean);
            if (effect > max_spike_effect) {
                max_spike_effect = effect;
            }
            if (out - truth > max_above_true) {
                max_above_true = out - truth;
            }
        }
    }
    if (csv) {
        fclose(csv);
    }

    printf("  info: triangle max |err| after warm-up = %.3f C (tick %d), max above true = %.3f C, "
           "max spike effect = %.4f C\n", max_err, max_err_tick, max_above_true, max_spike_effect);
    CHECK(max_err < 1.5f, "tracking error too large: %f at tick %d", max_err, max_err_tick);
    /* Spike isolation: compare against the same signal without spikes. On a ramp an outlier
     * shifts the 5-sample median by one rank (~one ramp step = 0.25 C), then EMA scales it by
     * alpha, so a small residual is expected; the +8 C spike itself must not pass. */
    CHECK(max_spike_effect < 0.3f, "spike visible in output: %f", max_spike_effect);
    /* Output never exceeds true value by more than the expected lag (~1.1 C on falling slope). */
    CHECK(max_above_true < 1.5f, "output too far above true: %f", max_above_true);

    /* Constant lag in the middle of each segment: error ~ slope * lag, stable. */
    float slope = 15.0f / (PERIOD / 2);
    float expect_lag = slope * (2.0f + (1.0f - ALPHA) / ALPHA); /* median 2 + EMA (1-a)/a samples */
    for (int cycle = 1; cycle < 3; cycle++) {
        int rise_mid = cycle * PERIOD + PERIOD / 4;
        int fall_mid = cycle * PERIOD + 3 * PERIOD / 4;
        float lag_r = triangle(rise_mid) - filtered[rise_mid];
        float lag_f = filtered[fall_mid] - triangle(fall_mid);
        printf("  info: cycle %d lag rising %.3f C, falling %.3f C (expected ~%.3f)\n",
               cycle, lag_r, lag_f, expect_lag);
        CHECK(fabsf(lag_r - expect_lag) < 0.3f, "rising lag %f not ~%f", lag_r, expect_lag);
        CHECK(fabsf(lag_f - expect_lag) < 0.3f, "falling lag %f not ~%f", lag_f, expect_lag);
    }

    /* Monotonic-ish: skip 8 samples after each corner (median + EMA settle). */
    int violations = 0;
    float worst = 0;
    for (int t = 1; t < TICKS; t++) {
        int p = t % PERIOD;
        int since_corner = p < PERIOD / 2 ? p : p - PERIOD / 2;
        if (since_corner < 8) {
            continue;
        }
        float d = filtered[t] - filtered[t - 1];
        float wrong = p < PERIOD / 2 ? -d : d; /* movement against the ramp */
        if (wrong > 0.05f) {
            violations++;
        }
        if (wrong > worst) {
            worst = wrong;
        }
    }
    printf("  info: monotonic violations (>0.05 C against ramp) = %d, worst = %.4f C\n", violations, worst);
    CHECK(violations == 0, "%d monotonic violations, worst %f", violations, worst);
}

int main(void)
{
    test_rejects_invalid();
    test_single_spike_removed();
    test_triangle();
    return TEST_SUMMARY("test_signal_filter");
}
