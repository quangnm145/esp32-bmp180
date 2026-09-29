/* Host unit tests for lib/data_pool (FreeRTOS/esp_err stubbed in stubs/). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "data_pool.h"
#include "test_common.h"

/* data_pool keeps static state; include the source so each test binary gets a fresh copy
 * and so we can reset it between test cases. */
#include "data_pool.c"

static void pool_reset(void)
{
    memset(s_items, 0, sizeof(s_items));
    s_head = 0;
    s_count = 0;
    s_next_seq = 1;
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.capacity = DATA_POOL_CAPACITY;
    data_pool_init();
}

static bool push_value(float v)
{
    sensor_sample_t s = {.valid = SAMPLE_VALID_BMP180, .bmp_temperature_c = v};
    return data_pool_push(&s);
}

static void test_fifo(void)
{
    pool_reset();
    CHECK(data_pool_init() == ESP_OK, "init failed");
    sensor_sample_t out[DATA_POOL_CAPACITY];
    CHECK(data_pool_peek(out, DATA_POOL_CAPACITY) == 0, "empty pool peek not 0");

    sensor_sample_t s = {.bmp_temperature_c = 1.0f};
    CHECK(!data_pool_push(&s), "unexpected drop");
    CHECK(s.seq == 1, "push must write assigned seq back, got %u", s.seq);
    for (int i = 2; i <= 10; i++) {
        push_value((float)i);
    }
    size_t n = data_pool_peek(out, 4);
    CHECK(n == 4, "peek max 4 returned %zu", n);
    for (size_t i = 0; i < n; i++) {
        CHECK(out[i].seq == i + 1 && out[i].bmp_temperature_c == (float)(i + 1),
              "FIFO order broken at %zu: seq %u val %f", i, out[i].seq, out[i].bmp_temperature_c);
    }
    n = data_pool_peek(out, DATA_POOL_CAPACITY);
    CHECK(n == 10, "peek all returned %zu", n);
    /* peek must not remove */
    CHECK(data_pool_peek(out, DATA_POOL_CAPACITY) == 10, "peek removed items");

    data_pool_stats_t st;
    data_pool_get_stats(&st);
    CHECK(st.count == 10 && st.pushed == 10 && st.dropped == 0 && st.sent == 0 &&
          st.capacity == DATA_POOL_CAPACITY, "stats wrong: count %zu pushed %u dropped %u sent %u",
          st.count, st.pushed, st.dropped, st.sent);
}

static void test_overflow_drops_oldest(void)
{
    pool_reset();
    int drops = 0;
    for (int i = 1; i <= DATA_POOL_CAPACITY + 5; i++) {
        drops += push_value((float)i) ? 1 : 0;
    }
    CHECK(drops == 5, "push reported %d drops", drops);
    data_pool_stats_t st;
    data_pool_get_stats(&st);
    CHECK(st.dropped == 5, "stats.dropped = %u", st.dropped);
    CHECK(st.count == DATA_POOL_CAPACITY, "count = %zu", st.count);
    CHECK(st.pushed == DATA_POOL_CAPACITY + 5, "pushed = %u", st.pushed);

    sensor_sample_t out[DATA_POOL_CAPACITY + 4];
    size_t n = data_pool_peek(out, DATA_POOL_CAPACITY + 4);
    CHECK(n == DATA_POOL_CAPACITY, "peek returned %zu", n);
    for (size_t i = 0; i < n; i++) {
        CHECK(out[i].seq == 6 + i && out[i].bmp_temperature_c == (float)(6 + i),
              "after overflow idx %zu seq %u (expected %zu)", i, out[i].seq, 6 + i);
    }
}

static void test_release(void)
{
    pool_reset();
    for (int i = 1; i <= 10; i++) {
        push_value((float)i);
    }
    data_pool_release(0);
    sensor_sample_t out[DATA_POOL_CAPACITY];
    CHECK(data_pool_peek(out, DATA_POOL_CAPACITY) == 10, "release(0) removed items");

    data_pool_release(4);
    size_t n = data_pool_peek(out, DATA_POOL_CAPACITY);
    CHECK(n == 6 && out[0].seq == 5 && out[5].seq == 10, "release(4): n %zu first %u", n, out[0].seq);

    data_pool_release(4); /* idempotent */
    CHECK(data_pool_peek(out, DATA_POOL_CAPACITY) == 6, "repeated release removed more");

    data_pool_release(100); /* seq beyond newest -> everything */
    CHECK(data_pool_peek(out, DATA_POOL_CAPACITY) == 0, "release(100) left items");

    data_pool_stats_t st;
    data_pool_get_stats(&st);
    CHECK(st.sent == 10 && st.count == 0, "sent %u count %zu", st.sent, st.count);

    /* Next push after full release continues seq. */
    sensor_sample_t s = {0};
    data_pool_push(&s);
    CHECK(s.seq == 11, "seq after release = %u", s.seq);
}

static void test_release_after_drop_during_send(void)
{
    pool_reset();
    for (int i = 0; i < DATA_POOL_CAPACITY; i++) {
        push_value((float)i);
    }
    sensor_sample_t batch[DATA_POOL_CAPACITY];
    size_t n = data_pool_peek(batch, DATA_POOL_CAPACITY); /* "sending" these */
    CHECK(n == DATA_POOL_CAPACITY, "peek %zu", n);
    uint32_t last_sent = batch[n - 1].seq;
    CHECK(last_sent == 32, "last peeked seq %u", last_sent);

    /* While the HTTP request is in flight, 3 new samples arrive -> 3 oldest dropped. */
    for (int i = 0; i < 3; i++) {
        CHECK(push_value(100.0f + i), "expected drop on push %d", i);
    }
    data_pool_release(last_sent);

    sensor_sample_t out[DATA_POOL_CAPACITY];
    n = data_pool_peek(out, DATA_POOL_CAPACITY);
    CHECK(n == 3, "expected 3 left, got %zu", n);
    for (size_t i = 0; i < n; i++) {
        CHECK(out[i].seq == 33 + i && out[i].bmp_temperature_c == 100.0f + i,
              "left idx %zu seq %u", i, out[i].seq);
    }
    data_pool_stats_t st;
    data_pool_get_stats(&st);
    printf("  info: after drop-during-send: count %zu dropped %u sent %u (29 released + 3 dropped = 32 peeked)\n",
           st.count, st.dropped, st.sent);
    CHECK(st.dropped == 3 && st.sent == 29, "dropped %u sent %u", st.dropped, st.sent);
}

static void test_seq_wraparound(void)
{
    /* Start near UINT32_MAX so seqs wrap during the test. */
    pool_reset();
    s_next_seq = UINT32_MAX - 2; /* seqs: MAX-2, MAX-1, MAX, 0, 1, 2 */
    uint32_t seqs[6];
    for (int i = 0; i < 6; i++) {
        sensor_sample_t s = {0};
        data_pool_push(&s);
        seqs[i] = s.seq;
    }
    CHECK(seqs[3] == 0 && seqs[5] == 2, "wrap seqs %u %u", seqs[3], seqs[5]);
    printf("  info: firmware seq wraps to 0 after UINT32_MAX (seq 0 is produced)\n");

    /* Release up to UINT32_MAX: must remove first 3 only, not the wrapped 0,1,2. */
    data_pool_release(UINT32_MAX);
    sensor_sample_t out[8];
    size_t n = data_pool_peek(out, 8);
    CHECK(n == 3 && out[0].seq == 0, "release(MAX) across wrap: n %zu first %u", n, n ? out[0].seq : 0);

    /* Release 1 (post-wrap) removes 0 and 1. */
    data_pool_release(1);
    n = data_pool_peek(out, 8);
    CHECK(n == 1 && out[0].seq == 2, "release(1) after wrap: n %zu first %u", n, n ? out[0].seq : 0);

    /* A stale release number (just before wrap) must not remove newer items. */
    data_pool_release(UINT32_MAX - 10);
    CHECK(data_pool_peek(out, 8) == 1, "stale release removed newer item");

    /* Comparison sanity on the same expression used by data_pool_release. */
    CHECK((int32_t)((uint32_t)5 - 3u) > 0, "5 > 3");
    CHECK((int32_t)((uint32_t)2 - (UINT32_MAX - 1)) > 0, "2 is newer than MAX-1");
    CHECK((int32_t)((UINT32_MAX - 1) - (uint32_t)2) < 0, "MAX-1 older than 2");
}

int main(void)
{
    test_fifo();
    test_overflow_drops_oldest();
    test_release();
    test_release_after_drop_during_send();
    test_seq_wraparound();
    return TEST_SUMMARY("test_data_pool");
}
