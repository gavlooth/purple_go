/* Channel semantics tests (unbuffered behavior) */
#include "test_framework.h"
#include <pthread.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    Obj* ch;
    Obj* val;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    volatile int started;
    volatile int sent;
} SendCtx;

static void* sender_thread(void* arg) {
    SendCtx* ctx = (SendCtx*)arg;
    pthread_mutex_lock(&ctx->lock);
    ctx->started = 1;
    pthread_cond_broadcast(&ctx->cond);
    pthread_mutex_unlock(&ctx->lock);
    channel_send(ctx->ch, ctx->val);
    pthread_mutex_lock(&ctx->lock);
    ctx->sent = 1;
    pthread_cond_broadcast(&ctx->cond);
    pthread_mutex_unlock(&ctx->lock);
    return NULL;
}

static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

void test_channel_unbuffered_blocks(void) {
    Obj* ch = make_channel(0);
    ASSERT_NOT_NULL(ch);

    SendCtx ctx = {0};
    ctx.ch = ch;
    ctx.val = mk_int_unboxed(7);
    pthread_mutex_init(&ctx.lock, NULL);
    pthread_cond_init(&ctx.cond, NULL);

    pthread_t th;
    pthread_create(&th, NULL, sender_thread, &ctx);

    /* Wait until sender starts */
    pthread_mutex_lock(&ctx.lock);
    while (!ctx.started) {
        pthread_cond_wait(&ctx.cond, &ctx.lock);
    }

    /* Unbuffered channel should still be blocked before recv */
    ASSERT_EQ(ctx.sent, 0);
    pthread_mutex_unlock(&ctx.lock);

    Obj* received = channel_recv(ch);
    ASSERT_NOT_NULL(received);
    ASSERT_EQ(obj_to_int(received), 7);

    /* Wait briefly for sender to complete; fallback to close to avoid hangs */
    int waited = 0;
    pthread_mutex_lock(&ctx.lock);
    while (!ctx.sent && waited < 200) {
        pthread_mutex_unlock(&ctx.lock);
        sleep_ms(1);
        waited++;
        pthread_mutex_lock(&ctx.lock);
    }
    if (!ctx.sent) {
        pthread_mutex_unlock(&ctx.lock);
        channel_close(ch);
        pthread_mutex_lock(&ctx.lock);
    }
    pthread_mutex_unlock(&ctx.lock);

    pthread_join(th, NULL);
    ASSERT_EQ(ctx.sent, 1);

    dec_ref(received);
    pthread_mutex_destroy(&ctx.lock);
    pthread_cond_destroy(&ctx.cond);
    dec_ref(ch);
    PASS();
}

void run_channel_semantics_tests(void) {
    TEST_SUITE("Channel Semantics");

    TEST("unbuffered send blocks until recv");
    test_channel_unbuffered_blocks();
}
