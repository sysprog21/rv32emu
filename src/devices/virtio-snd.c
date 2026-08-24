/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <portaudio.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "virtio.h"

#define VSND_DEV_CNT_MAX 1
#define VSND_QUEUE_NUM_MAX 1024
#define VSND_QUEUE_COUNT 4
#define VSND_QUEUE (vsnd->queues[vsnd->queue_sel])

#define VSND_CNFA_FRAME_SZ 2 /* S16 = 2 bytes per sample */

enum {
    VSND_QUEUE_CTRL = 0,
    VSND_QUEUE_EVT = 1,
    VSND_QUEUE_TX = 2,
    VSND_QUEUE_RX = 3,
};

enum {
    VSND_FEATURES_0 = 0,
    VSND_FEATURES_1 = 1, /* VIRTIO_F_VERSION_1 */
};

enum {
    /* jack control request types */
    VIRTIO_SND_R_JACK_INFO = 1,

    /* PCM control request types */
    VIRTIO_SND_R_PCM_INFO = 0x0100,
    VIRTIO_SND_R_PCM_SET_PARAMS,
    VIRTIO_SND_R_PCM_PREPARE,
    VIRTIO_SND_R_PCM_RELEASE,
    VIRTIO_SND_R_PCM_START,
    VIRTIO_SND_R_PCM_STOP,

    /* channel map control request types */
    VIRTIO_SND_R_CHMAP_INFO = 0x0200,

    /* jack event types */
    VIRTIO_SND_EVT_JACK_CONNECTED = 0x1000,
    VIRTIO_SND_EVT_JACK_DISCONNECTED,

    /* PCM event types */
    VIRTIO_SND_EVT_PCM_PERIOD_ELAPSED = 0x1100,
    VIRTIO_SND_EVT_PCM_XRUN,

    /* common status codes */
    VIRTIO_SND_S_OK = 0x8000,
    VIRTIO_SND_S_BAD_MSG,
    VIRTIO_SND_S_NOT_SUPP,
    VIRTIO_SND_S_IO_ERR,
};

/* Unit: Hz */
#define SND_PCM_RATE \
    _(5512)          \
    _(8000)          \
    _(11025)         \
    _(16000)         \
    _(22050)         \
    _(32000)         \
    _(44100)         \
    _(48000)         \
    _(64000)         \
    _(88200)         \
    _(96000)         \
    _(176400)        \
    _(192000)        \
    _(384000)

enum {
#define _(rate) VIRTIO_SND_PCM_RATE_##rate,
    SND_PCM_RATE
#undef _
        VIRTIO_SND_PCM_RATE_COUNT,
};

static const int pcm_rate_tbl[] = {
#define _(rate) [VIRTIO_SND_PCM_RATE_##rate] = rate,
    SND_PCM_RATE
#undef _
};

enum {
    VIRTIO_SND_PCM_F_SHMEM_HOST = 0,
    VIRTIO_SND_PCM_F_SHMEM_GUEST,
    VIRTIO_SND_PCM_F_MSG_POLLING,
    VIRTIO_SND_PCM_F_EVT_SHMEM_PERIODS,
    VIRTIO_SND_PCM_F_EVT_XRUNS,
};

enum {
#define _(samp_fmt) VIRTIO_SND_PCM_FMT_##samp_fmt
    _(IMA_ADPCM) = 0,
    _(MU_LAW),
    _(A_LAW),
    _(S8),
    _(U8),
    _(S16),
    _(U16),
    _(S18_3),
    _(U18_3),
    _(S20_3),
    _(U20_3),
    _(S24_3),
    _(U24_3),
    _(S20),
    _(U20),
    _(S24),
    _(U24),
    _(S32),
    _(U32),
    _(FLOAT),
    _(FLOAT64),
    _(DSD_U8),
    _(DSD_U16),
    _(DSD_U32),
    _(IEC958_SUBFRAME),
#undef _
};

enum {
#define _(chmap_pos) VIRTIO_SND_CHMAP_##chmap_pos
    _(NONE) = 0,
    _(NA),
    _(MONO),
    _(FL),
    _(FR),
    _(RL),
    _(RR),
    _(FC),
    _(LFE),
    _(SL),
    _(SR),
    _(RC),
    _(FLC),
    _(FRC),
    _(RLC),
    _(RRC),
    _(FLW),
    _(FRW),
    _(FLH),
    _(FCH),
    _(FRH),
    _(TC),
    _(TFL),
    _(TFR),
    _(TFC),
    _(TRL),
    _(TRR),
    _(TRC),
    _(TFLC),
    _(TFRC),
    _(TSL),
    _(TSR),
    _(LLFE),
    _(RLFE),
    _(BC),
    _(BLC),
    _(BRC),
#undef _
};

enum {
    VIRTIO_SND_D_OUTPUT = 0,
    VIRTIO_SND_D_INPUT,
};

typedef struct {
    uint32_t jacks;
    uint32_t streams;
    uint32_t chmaps;
    uint32_t controls;
} virtio_snd_config_t;

typedef struct {
    uint32_t code;
} virtio_snd_hdr_t;

typedef struct {
    uint32_t hda_fn_nid;
} virtio_snd_info_t;

typedef struct {
    virtio_snd_hdr_t hdr;
    uint32_t start_id;
    uint32_t count;
    uint32_t size;
} virtio_snd_query_info_t;

typedef struct {
    virtio_snd_info_t hdr;
    uint32_t features;
    uint32_t hda_reg_defconf;
    uint32_t hda_reg_caps;
    uint8_t connected;
    uint8_t padding[7];
} virtio_snd_jack_info_t;

typedef struct {
    virtio_snd_info_t hdr;
    uint32_t features;
    uint64_t formats;
    uint64_t rates;
    uint8_t direction;
    uint8_t channels_min;
    uint8_t channels_max;
    uint8_t padding[5];
} virtio_snd_pcm_info_t;

typedef struct {
    virtio_snd_hdr_t hdr;
    uint32_t stream_id;
} virtio_snd_pcm_hdr_t;

typedef struct {
    virtio_snd_pcm_hdr_t hdr;
    uint32_t buffer_bytes;
    uint32_t period_bytes;
    uint32_t features;
    uint8_t channels;
    uint8_t format;
    uint8_t rate;
    uint8_t padding;
} virtio_snd_pcm_set_params_t;

typedef struct {
    uint32_t stream_id;
} virtio_snd_pcm_xfer_t;

typedef struct {
    uint32_t status;
    uint32_t latency_bytes;
} virtio_snd_pcm_status_t;

#define VIRTIO_SND_CHMAP_MAX_SIZE 18

typedef struct {
    virtio_snd_info_t hdr;
    uint8_t direction;
    uint8_t channels;
    uint8_t positions[VIRTIO_SND_CHMAP_MAX_SIZE];
} virtio_snd_chmap_info_t;

typedef struct {
    pthread_cond_t completed;
    pthread_cond_t all_completed;
    size_t pending_pcm_bytes;
    uint32_t inflight_count;
    bool releasing;
    pthread_mutex_t lock;
} virtio_snd_queue_lock_t;

typedef struct {
    uint32_t stream_id;
} vsnd_stream_sel_t;

typedef struct vsnd_buf_queue_node {
    uint8_t *addr;
    uint32_t len;
    uint32_t pos;

    /* Keep the virtio request alive until the consumer has consumed all PCM. */
    uint32_t desc_idx;
    uint64_t status_addr;
    uint32_t stream_id;
    uint32_t completion_status;

    struct vsnd_buf_queue_node *next;
    struct vsnd_buf_queue_node *prev;
} vsnd_buf_queue_node_t;

typedef struct {
    virtio_snd_jack_info_t j;
    virtio_snd_pcm_info_t p;
    virtio_snd_chmap_info_t c;
    virtio_snd_pcm_set_params_t pp;
    PaStream *pa_stream;

    virtio_snd_queue_lock_t lock;
    vsnd_buf_queue_node_t *buf_head;
    vsnd_buf_queue_node_t *buf_tail;
    vsnd_buf_queue_node_t *completed_head;
    vsnd_buf_queue_node_t *completed_tail;

    virtio_snd_state_t *vsnd;
    pthread_t completion_thread;
    bool completion_thread_stop;
    bool completion_thread_started;

    vsnd_stream_sel_t v;
} virtio_snd_prop_t;

typedef struct {
    virtio_snd_config_t config;
    virtio_snd_prop_t props[VSND_DEV_CNT_MAX];

    pthread_mutex_t tx_mutex;
    pthread_cond_t tx_cond;
    int tx_ev_notify;
    bool tx_thread_stop;
    bool tx_thread_started;
    pthread_t tx_thread;

    pthread_mutex_t tx_process_mutex;
    pthread_mutex_t tx_used_mutex;

    bool pa_initialized;
    virtio_snd_state_t *vsnd;
} virtio_snd_priv_t;

typedef int (*vsnd_virtq_cb)(virtio_snd_state_t *vsnd,
                             virtio_snd_queue_t *queue,
                             uint32_t desc_idx,
                             uint32_t *plen);

static int virtio_snd_stream_cb(const void *input,
                                void *output,
                                unsigned long frame_cnt,
                                const PaStreamCallbackTimeInfo *time_info,
                                PaStreamCallbackFlags status_flags,
                                void *user_data);

static void virtio_queue_notify_handler(virtio_snd_state_t *vsnd, int index);
static void virtio_snd_process_tx_queue(virtio_snd_state_t *vsnd);
static void vsnd_cancel_pending_and_wait(virtio_snd_prop_t *props);
static void *virtio_snd_completion_thread(void *arg);

static inline virtio_snd_priv_t *vsnd_priv(virtio_snd_state_t *vsnd)
{
    return (virtio_snd_priv_t *) vsnd->priv;
}

static inline uint32_t vsnd_min(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static bool vsnd_stream_accepts_tx(uint32_t code)
{
    return code == VIRTIO_SND_R_PCM_PREPARE || code == VIRTIO_SND_R_PCM_START;
}

static void vsnd_queue_push(virtio_snd_prop_t *props,
                            vsnd_buf_queue_node_t *node)
{
    node->next = NULL;
    node->prev = props->buf_tail;

    if (props->buf_tail)
        props->buf_tail->next = node;
    else
        props->buf_head = node;

    props->buf_tail = node;
}

static void vsnd_queue_remove(virtio_snd_prop_t *props,
                              vsnd_buf_queue_node_t *node)
{
    if (node->prev)
        node->prev->next = node->next;
    else
        props->buf_head = node->next;

    if (node->next)
        node->next->prev = node->prev;
    else
        props->buf_tail = node->prev;
}

static void vsnd_free_node(vsnd_buf_queue_node_t *node)
{
    if (!node)
        return;
    free(node->addr);
    free(node);
}

static void vsnd_completed_push_locked(virtio_snd_prop_t *props,
                                       vsnd_buf_queue_node_t *node)
{
    node->next = NULL;
    node->prev = props->completed_tail;
    if (props->completed_tail)
        props->completed_tail->next = node;
    else
        props->completed_head = node;
    props->completed_tail = node;
}

static vsnd_buf_queue_node_t *vsnd_completed_pop_locked(
    virtio_snd_prop_t *props)
{
    vsnd_buf_queue_node_t *node = props->completed_head;
    if (!node)
        return NULL;
    props->completed_head = node->next;
    if (props->completed_head)
        props->completed_head->prev = NULL;
    else
        props->completed_tail = NULL;
    node->next = NULL;
    node->prev = NULL;
    return node;
}

static void vsnd_move_pending_to_completed_locked(virtio_snd_prop_t *props)
{
    while (props->buf_head) {
        vsnd_buf_queue_node_t *node = props->buf_head;
        vsnd_queue_remove(props, node);
        node->completion_status = VIRTIO_SND_S_OK;
        vsnd_completed_push_locked(props, node);
    }
    props->lock.pending_pcm_bytes = 0;
    pthread_cond_signal(&props->lock.completed);
}

static void vsnd_clear_all_nodes_locked(virtio_snd_prop_t *props)
{
    vsnd_buf_queue_node_t *node = props->buf_head;
    while (node) {
        vsnd_buf_queue_node_t *next = node->next;
        vsnd_free_node(node);
        node = next;
    }
    node = props->completed_head;
    while (node) {
        vsnd_buf_queue_node_t *next = node->next;
        vsnd_free_node(node);
        node = next;
    }
    props->buf_head = NULL;
    props->buf_tail = NULL;
    props->completed_head = NULL;
    props->completed_tail = NULL;
    props->lock.pending_pcm_bytes = 0;
    props->lock.inflight_count = 0;
}

static void virtio_snd_set_fail(virtio_snd_state_t *vsnd)
{
    uint32_t old_status = __atomic_fetch_or(
        &vsnd->status, VIRTIO_STATUS_DEVICE_NEEDS_RESET, __ATOMIC_ACQ_REL);

    if (old_status & VIRTIO_STATUS_DRIVER_OK)
        __atomic_fetch_or(&vsnd->interrupt_status, VIRTIO_INT_CONF_CHANGE,
                          __ATOMIC_RELEASE);

    rv_log_error("virtio-snd: DEVICE_NEEDS_RESET");
}

static bool vsnd_guest_range_ok(uint64_t addr, uint64_t len)
{
#if MEM_SIZE < 0x100000000ULL
    if (addr >= MEM_SIZE)
        return false;
    if (len > MEM_SIZE)
        return false;
    if (addr + len < addr)
        return false;
    if (addr + len > MEM_SIZE)
        return false;
#endif
    return true;
}

static bool vsnd_word_range_ok(uint32_t word_addr, uint32_t words)
{
#if MEM_SIZE < 0x100000000ULL
    uint64_t byte_addr = (uint64_t) word_addr << 2;
    uint64_t byte_len = (uint64_t) words << 2;
    return vsnd_guest_range_ok(byte_addr, byte_len);
#else
    (void) word_addr;
    (void) words;
    return true;
#endif
}

static bool vsnd_check_word_range(virtio_snd_state_t *vsnd,
                                  uint32_t word_addr,
                                  uint32_t words)
{
    if (!vsnd_word_range_ok(word_addr, words)) {
        virtio_snd_set_fail(vsnd);
        return false;
    }

    return true;
}

static inline uint32_t vsnd_preprocess(virtio_snd_state_t *vsnd, uint32_t addr)
{
#if MEM_SIZE < 0x100000000ULL
    if ((addr >= MEM_SIZE) || (addr & 0b11)) {
#else
    if (addr & 0b11) {
#endif
        virtio_snd_set_fail(vsnd);
        return 0;
    }

    return addr >> 2;
}

static void vsnd_reset_stream(virtio_snd_prop_t *props)
{
    pthread_mutex_lock(&props->lock.lock);
    props->lock.releasing = true;
    pthread_mutex_unlock(&props->lock.lock);

    if (props->pa_stream && Pa_IsStreamActive(props->pa_stream) == 1)
        Pa_AbortStream(props->pa_stream);

    vsnd_cancel_pending_and_wait(props);

    if (props->pa_stream) {
        Pa_CloseStream(props->pa_stream);
        props->pa_stream = NULL;
    }

    pthread_mutex_lock(&props->lock.lock);
    memset(&props->pp, 0, sizeof(props->pp));
    props->pp.hdr.hdr.code = VIRTIO_SND_R_PCM_SET_PARAMS;
    props->lock.releasing = false;
    pthread_mutex_unlock(&props->lock.lock);
}

static void virtio_snd_update_status(virtio_snd_state_t *vsnd, uint32_t status)
{
    if (status) {
        __atomic_fetch_or(&vsnd->status, status, __ATOMIC_RELEASE);
        return;
    }

    uint32_t *ram = vsnd->ram;
    void *priv = vsnd->priv;

    if (priv) {
        virtio_snd_priv_t *p = (virtio_snd_priv_t *) priv;
        for (uint32_t i = 0; i < VSND_DEV_CNT_MAX; i++)
            vsnd_reset_stream(&p->props[i]);

        pthread_mutex_lock(&p->tx_process_mutex);
        pthread_mutex_lock(&p->tx_mutex);
        p->tx_ev_notify = 0;
        pthread_mutex_unlock(&p->tx_mutex);
    }

    memset(vsnd, 0, sizeof(*vsnd));
    vsnd->ram = ram;
    vsnd->priv = priv;

    if (priv) {
        virtio_snd_priv_t *p = (virtio_snd_priv_t *) priv;
        pthread_mutex_unlock(&p->tx_process_mutex);
    }
}

static void *vsnd_guest_ptr(virtio_snd_state_t *vsnd,
                            uint64_t addr,
                            uint32_t len)
{
    if (!vsnd_guest_range_ok(addr, len)) {
        virtio_snd_set_fail(vsnd);
        return NULL;
    }

    return (void *) ((uintptr_t) vsnd->ram + (uintptr_t) addr);
}

static bool virtio_snd_complete_tx_request(virtio_snd_state_t *vsnd,
                                           uint32_t desc_idx,
                                           uint64_t status_addr,
                                           uint32_t completion_status,
                                           uint32_t latency_bytes)
{
    virtio_snd_priv_t *p = vsnd_priv(vsnd);
    virtio_snd_queue_t *queue = &vsnd->queues[VSND_QUEUE_TX];
    uint32_t *ram = vsnd->ram;

    pthread_mutex_lock(&p->tx_used_mutex);
    if (!queue->ready || !queue->queue_num ||
        queue->queue_num > VSND_QUEUE_NUM_MAX) {
        pthread_mutex_unlock(&p->tx_used_mutex);
        return false;
    }

    virtio_snd_pcm_status_t *status =
        vsnd_guest_ptr(vsnd, status_addr, sizeof(*status));
    if (!status) {
        pthread_mutex_unlock(&p->tx_used_mutex);
        return false;
    }

    if (!vsnd_check_word_range(vsnd, queue->queue_used, 1) ||
        !vsnd_check_word_range(vsnd, queue->queue_avail, 1)) {
        pthread_mutex_unlock(&p->tx_used_mutex);
        return false;
    }

    uint16_t used_idx = ram[queue->queue_used] >> 16;
    uint32_t used_addr =
        queue->queue_used + 1 + (used_idx % queue->queue_num) * 2;
    if (!vsnd_check_word_range(vsnd, used_addr, 2)) {
        pthread_mutex_unlock(&p->tx_used_mutex);
        return false;
    }

    status->status = completion_status;
    status->latency_bytes = latency_bytes;
    __atomic_thread_fence(__ATOMIC_RELEASE);

    ram[used_addr] = desc_idx;
    ram[used_addr + 1] = sizeof(*status);
    used_idx++;
    ram[queue->queue_used] &= MASK(16);
    ram[queue->queue_used] |= ((uint32_t) used_idx) << 16;

    __atomic_thread_fence(__ATOMIC_RELEASE);
    if (!(ram[queue->queue_avail] & 1))
        __atomic_fetch_or(&vsnd->interrupt_status, VIRTIO_INT_USED_RING,
                          __ATOMIC_RELEASE);

    pthread_mutex_unlock(&p->tx_used_mutex);
    return true;
}

static void *virtio_snd_completion_thread(void *arg)
{
    virtio_snd_prop_t *props = (virtio_snd_prop_t *) arg;

    for (;;) {
        pthread_mutex_lock(&props->lock.lock);
        while (!props->completed_head && !props->completion_thread_stop)
            pthread_cond_wait(&props->lock.completed, &props->lock.lock);

        if (props->completion_thread_stop && !props->completed_head) {
            pthread_mutex_unlock(&props->lock.lock);
            break;
        }

        vsnd_buf_queue_node_t *node = vsnd_completed_pop_locked(props);
        size_t pending = props->lock.pending_pcm_bytes;
        pthread_mutex_unlock(&props->lock.lock);

        uint32_t latency =
            pending > UINT32_MAX ? UINT32_MAX : (uint32_t) pending;
        if (props->vsnd)
            virtio_snd_complete_tx_request(props->vsnd, node->desc_idx,
                                           node->status_addr,
                                           node->completion_status, latency);
        vsnd_free_node(node);

        pthread_mutex_lock(&props->lock.lock);
        if (props->lock.inflight_count > 0)
            props->lock.inflight_count--;
        if (props->lock.inflight_count == 0)
            pthread_cond_broadcast(&props->lock.all_completed);
        pthread_mutex_unlock(&props->lock.lock);
    }
    return NULL;
}

static void vsnd_cancel_pending_and_wait(virtio_snd_prop_t *props)
{
    pthread_mutex_lock(&props->lock.lock);
    props->lock.releasing = true;
    pthread_mutex_unlock(&props->lock.lock);

    if (props->vsnd)
        virtio_snd_process_tx_queue(props->vsnd);

    pthread_mutex_lock(&props->lock.lock);
    vsnd_move_pending_to_completed_locked(props);
    if (!props->completion_thread_started) {
        vsnd_clear_all_nodes_locked(props);
        pthread_mutex_unlock(&props->lock.lock);
        return;
    }

    while (props->lock.inflight_count > 0)
        pthread_cond_wait(&props->lock.all_completed, &props->lock.lock);
    pthread_mutex_unlock(&props->lock.lock);
}


static bool vsnd_collect_desc_chain(virtio_snd_state_t *vsnd,
                                    virtio_snd_queue_t *queue,
                                    uint32_t desc_idx,
                                    struct virtq_desc *chain,
                                    uint32_t *chain_cnt)
{
    if (!queue->queue_num || queue->queue_num > VSND_QUEUE_NUM_MAX)
        return false;

    uint32_t cnt = 0;

    for (;;) {
        if (desc_idx >= queue->queue_num)
            return false;

        if (cnt >= queue->queue_num)
            return false;

        uint32_t desc_word = queue->queue_desc + desc_idx * 4;
        if (!vsnd_check_word_range(vsnd, desc_word, 4))
            return false;

        struct virtq_desc *desc = (struct virtq_desc *) &vsnd->ram[desc_word];

        if (!vsnd_guest_range_ok(desc->addr, desc->len))
            return false;

        chain[cnt] = *desc;
        cnt++;

        if (!(desc->flags & VIRTIO_DESC_F_NEXT))
            break;

        desc_idx = desc->next;
    }

    *chain_cnt = cnt;
    return true;
}

static void virtio_snd_read_jack_info_handler(
    virtio_snd_state_t *vsnd,
    virtio_snd_jack_info_t *info,
    const virtio_snd_query_info_t *query,
    uint32_t *payload_len)
{
    virtio_snd_priv_t *p = vsnd_priv(vsnd);
    uint32_t cnt = query->count;
    uint32_t written = 0;

    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t id = query->start_id + i;
        if (id >= VSND_DEV_CNT_MAX)
            break;

        info[i].hdr.hda_fn_nid = 0;
        info[i].features = 0;
        info[i].hda_reg_defconf = 0;
        info[i].hda_reg_caps = 0;
        info[i].connected = 1;
        memset(info[i].padding, 0, sizeof(info[i].padding));

        p->props[id].j = info[i];
        written++;
    }

    *payload_len = written * sizeof(*info);
}

static void virtio_snd_read_pcm_info_handler(
    virtio_snd_state_t *vsnd,
    virtio_snd_pcm_info_t *info,
    const virtio_snd_query_info_t *query,
    uint32_t *payload_len)
{
    virtio_snd_priv_t *p = vsnd_priv(vsnd);
    uint32_t cnt = query->count;
    uint32_t written = 0;

    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t id = query->start_id + i;
        if (id >= VSND_DEV_CNT_MAX)
            break;

        info[i].hdr.hda_fn_nid = 0;
        info[i].features = 0;
        info[i].formats = 1ULL << VIRTIO_SND_PCM_FMT_S16;
        info[i].rates = 0;

#define _(rate) info[i].rates |= 1ULL << VIRTIO_SND_PCM_RATE_##rate;
        SND_PCM_RATE
#undef _

        info[i].direction = VIRTIO_SND_D_OUTPUT;
        info[i].channels_min = 1;
        info[i].channels_max = 1;
        memset(info[i].padding, 0, sizeof(info[i].padding));

        p->props[id].p = info[i];
        written++;
    }

    *payload_len = written * sizeof(*info);
}

static void virtio_snd_read_chmap_info_handler(
    virtio_snd_state_t *vsnd,
    virtio_snd_chmap_info_t *info,
    const virtio_snd_query_info_t *query,
    uint32_t *payload_len)
{
    virtio_snd_priv_t *p = vsnd_priv(vsnd);
    uint32_t cnt = query->count;
    uint32_t written = 0;

    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t id = query->start_id + i;
        if (id >= VSND_DEV_CNT_MAX)
            break;

        memset(&info[i], 0, sizeof(info[i]));
        info[i].hdr.hda_fn_nid = 0;
        info[i].direction = VIRTIO_SND_D_OUTPUT;
        info[i].channels = 1;
        info[i].positions[0] = VIRTIO_SND_CHMAP_MONO;

        p->props[id].c = info[i];
        written++;
    }

    *payload_len = written * sizeof(*info);
}

static uint32_t virtio_snd_pcm_set_params(
    virtio_snd_state_t *vsnd,
    const virtio_snd_pcm_set_params_t *request,
    uint32_t *payload_len)
{
    virtio_snd_priv_t *p = vsnd_priv(vsnd);
    uint32_t id = request->hdr.stream_id;

    if (id >= VSND_DEV_CNT_MAX)
        return VIRTIO_SND_S_BAD_MSG;

    if (request->channels != 1)
        return VIRTIO_SND_S_NOT_SUPP;

    if (request->format != VIRTIO_SND_PCM_FMT_S16)
        return VIRTIO_SND_S_NOT_SUPP;

    if (request->rate >= VIRTIO_SND_PCM_RATE_COUNT ||
        pcm_rate_tbl[request->rate] == 0)
        return VIRTIO_SND_S_NOT_SUPP;

    virtio_snd_prop_t *props = &p->props[id];

    pthread_mutex_lock(&props->lock.lock);
    uint32_t code = props->pp.hdr.hdr.code;
    if (code != VIRTIO_SND_R_PCM_RELEASE &&
        code != VIRTIO_SND_R_PCM_SET_PARAMS &&
        code != VIRTIO_SND_R_PCM_PREPARE) {
        pthread_mutex_unlock(&props->lock.lock);
        return VIRTIO_SND_S_BAD_MSG;
    }

    props->pp = *request;
    props->pp.hdr.hdr.code = VIRTIO_SND_R_PCM_SET_PARAMS;
    pthread_mutex_unlock(&props->lock.lock);

    *payload_len = 0;
    return VIRTIO_SND_S_OK;
}

static uint32_t virtio_snd_pcm_prepare(virtio_snd_state_t *vsnd,
                                       const virtio_snd_pcm_hdr_t *request,
                                       uint32_t *payload_len)
{
    virtio_snd_priv_t *p = vsnd_priv(vsnd);
    uint32_t stream_id = request->stream_id;
    if (stream_id >= VSND_DEV_CNT_MAX)
        return VIRTIO_SND_S_BAD_MSG;

    virtio_snd_prop_t *props = &p->props[stream_id];
    pthread_mutex_lock(&props->lock.lock);
    uint32_t code = props->pp.hdr.hdr.code;
    pthread_mutex_unlock(&props->lock.lock);

    if (code != VIRTIO_SND_R_PCM_RELEASE &&
        code != VIRTIO_SND_R_PCM_SET_PARAMS && code != VIRTIO_SND_R_PCM_PREPARE)
        return VIRTIO_SND_S_BAD_MSG;
    if (props->pp.channels != 1 || props->pp.format != VIRTIO_SND_PCM_FMT_S16 ||
        props->pp.rate >= VIRTIO_SND_PCM_RATE_COUNT ||
        pcm_rate_tbl[props->pp.rate] == 0)
        return VIRTIO_SND_S_BAD_MSG;

    pthread_mutex_lock(&props->lock.lock);
    props->lock.releasing = true;
    pthread_mutex_unlock(&props->lock.lock);

    if (props->pa_stream && Pa_IsStreamActive(props->pa_stream) == 1)
        Pa_AbortStream(props->pa_stream);

    vsnd_cancel_pending_and_wait(props);

    if (props->pa_stream) {
        Pa_CloseStream(props->pa_stream);
        props->pa_stream = NULL;
    }
    props->v.stream_id = stream_id;

    uint32_t channels = props->pp.channels;
    uint32_t rate = (uint32_t) pcm_rate_tbl[props->pp.rate];
    uint32_t period_frames = rate / 10;

    if (!period_frames)
        period_frames = 1;

    PaStreamParameters params = {
        .device = Pa_GetDefaultOutputDevice(),
        .channelCount = (int) channels,
        .sampleFormat = paInt16,
        .suggestedLatency = 0.1,
        .hostApiSpecificStreamInfo = NULL,
    };

    if (params.device == paNoDevice)
        goto prepare_failed;

    PaError err =
        Pa_OpenStream(&props->pa_stream, NULL, &params, rate, period_frames,
                      paClipOff, virtio_snd_stream_cb, &props->v);
    if (err != paNoError) {
        rv_log_error("PortAudio Pa_OpenStream failed: %s",
                     Pa_GetErrorText(err));
        props->pa_stream = NULL;
        goto prepare_failed;
    }

    pthread_mutex_lock(&props->lock.lock);
    props->lock.releasing = false;
    props->pp.hdr.hdr.code = VIRTIO_SND_R_PCM_PREPARE;
    pthread_mutex_unlock(&props->lock.lock);
    *payload_len = 0;
    return VIRTIO_SND_S_OK;

prepare_failed:
    pthread_mutex_lock(&props->lock.lock);
    props->lock.releasing = false;
    props->pp.hdr.hdr.code = VIRTIO_SND_R_PCM_SET_PARAMS;
    pthread_mutex_unlock(&props->lock.lock);
    return VIRTIO_SND_S_IO_ERR;
}

static uint32_t virtio_snd_pcm_start(virtio_snd_state_t *vsnd,
                                     const virtio_snd_pcm_hdr_t *request,
                                     uint32_t *payload_len)
{
    virtio_snd_priv_t *p = vsnd_priv(vsnd);
    uint32_t stream_id = request->stream_id;
    if (stream_id >= VSND_DEV_CNT_MAX)
        return VIRTIO_SND_S_BAD_MSG;

    virtio_snd_prop_t *props = &p->props[stream_id];

    pthread_mutex_lock(&props->lock.lock);
    uint32_t code = props->pp.hdr.hdr.code;
    if (code == VIRTIO_SND_R_PCM_PREPARE || code == VIRTIO_SND_R_PCM_STOP) {
        props->lock.releasing = false;
        props->pp.hdr.hdr.code = VIRTIO_SND_R_PCM_START;
    }
    pthread_mutex_unlock(&props->lock.lock);

    if (code != VIRTIO_SND_R_PCM_PREPARE && code != VIRTIO_SND_R_PCM_STOP)
        return VIRTIO_SND_S_BAD_MSG;
    if (!props->pa_stream)
        return VIRTIO_SND_S_IO_ERR;

    int active = Pa_IsStreamActive(props->pa_stream);
    if (active < 0) {
        rv_log_error("virtio-snd: Pa_IsStreamActive failed: %s",
                     Pa_GetErrorText(active));
        pthread_mutex_lock(&props->lock.lock);
        props->pp.hdr.hdr.code = code;
        pthread_mutex_unlock(&props->lock.lock);
        return VIRTIO_SND_S_IO_ERR;
    }

    if (active == 0) {
        PaError err = Pa_StartStream(props->pa_stream);
        if (err != paNoError) {
            rv_log_error("virtio-snd: Pa_StartStream failed: %s",
                         Pa_GetErrorText(err));
            pthread_mutex_lock(&props->lock.lock);
            props->pp.hdr.hdr.code = code;
            pthread_mutex_unlock(&props->lock.lock);
            return VIRTIO_SND_S_IO_ERR;
        }
    }

    *payload_len = 0;
    return VIRTIO_SND_S_OK;
}

static uint32_t virtio_snd_pcm_stop(virtio_snd_state_t *vsnd,
                                    const virtio_snd_pcm_hdr_t *request,
                                    uint32_t *payload_len)
{
    virtio_snd_priv_t *p = vsnd_priv(vsnd);
    uint32_t stream_id = request->stream_id;
    if (stream_id >= VSND_DEV_CNT_MAX)
        return VIRTIO_SND_S_BAD_MSG;
    virtio_snd_prop_t *props = &p->props[stream_id];

    pthread_mutex_lock(&props->lock.lock);
    uint32_t code = props->pp.hdr.hdr.code;
    if (code == VIRTIO_SND_R_PCM_START)
        props->pp.hdr.hdr.code = VIRTIO_SND_R_PCM_STOP;
    pthread_mutex_unlock(&props->lock.lock);
    if (code != VIRTIO_SND_R_PCM_START)
        return VIRTIO_SND_S_BAD_MSG;
    if (!props->pa_stream)
        return VIRTIO_SND_S_IO_ERR;

    PaError err = paNoError;
    int active = Pa_IsStreamActive(props->pa_stream);
    if (active < 0)
        err = (PaError) active;
    else if (active == 1)
        err = Pa_AbortStream(props->pa_stream);
    if (err != paNoError) {
        rv_log_error("PortAudio Pa_AbortStream failed: %s",
                     Pa_GetErrorText(err));
        return VIRTIO_SND_S_IO_ERR;
    }
    *payload_len = 0;
    return VIRTIO_SND_S_OK;
}
static uint32_t virtio_snd_pcm_release(virtio_snd_state_t *vsnd,
                                       const virtio_snd_pcm_hdr_t *request,
                                       uint32_t *payload_len)
{
    virtio_snd_priv_t *p = vsnd_priv(vsnd);
    uint32_t stream_id = request->stream_id;

    if (stream_id >= VSND_DEV_CNT_MAX)
        return VIRTIO_SND_S_BAD_MSG;

    virtio_snd_prop_t *props = &p->props[stream_id];
    pthread_mutex_lock(&props->lock.lock);
    uint32_t code = props->pp.hdr.hdr.code;
    if (code == VIRTIO_SND_R_PCM_PREPARE || code == VIRTIO_SND_R_PCM_STOP ||
        code == VIRTIO_SND_R_PCM_START) {
        props->pp.hdr.hdr.code = VIRTIO_SND_R_PCM_RELEASE;
        props->lock.releasing = true;
    }
    pthread_mutex_unlock(&props->lock.lock);
    if (code != VIRTIO_SND_R_PCM_PREPARE && code != VIRTIO_SND_R_PCM_STOP &&
        code != VIRTIO_SND_R_PCM_START)
        return VIRTIO_SND_S_BAD_MSG;

    if (props->pa_stream) {
        int active = Pa_IsStreamActive(props->pa_stream);
        if (active == 1) {
            PaError err = Pa_AbortStream(props->pa_stream);
            if (err != paNoError)
                rv_log_error("PortAudio Pa_AbortStream failed: %s",
                             Pa_GetErrorText(err));
        } else if (active < 0) {
            rv_log_error("PortAudio Pa_IsStreamActive failed: %s",
                         Pa_GetErrorText(active));
        }
    }

    vsnd_cancel_pending_and_wait(props);

    if (props->pa_stream) {
        PaError err = Pa_CloseStream(props->pa_stream);
        if (err != paNoError)
            rv_log_error("PortAudio Pa_CloseStream failed: %s",
                         Pa_GetErrorText(err));
        props->pa_stream = NULL;
    }
    *payload_len = 0;
    return VIRTIO_SND_S_OK;
}

static uint32_t vsnd_queue_read_locked(virtio_snd_prop_t *props,
                                       void *dst,
                                       uint32_t bytes)
{
    uint8_t *out = (uint8_t *) dst;
    uint32_t copied = 0;
    while (props->buf_head && copied < bytes) {
        vsnd_buf_queue_node_t *node = props->buf_head;
        uint32_t left = bytes - copied;
        uint32_t available = node->len - node->pos;
        uint32_t n = vsnd_min(left, available);
        memcpy(out + copied, node->addr + node->pos, n);
        copied += n;
        node->pos += n;
        if (props->lock.pending_pcm_bytes >= n)
            props->lock.pending_pcm_bytes -= n;
        else
            props->lock.pending_pcm_bytes = 0;
        if (node->pos >= node->len) {
            vsnd_queue_remove(props, node);
            node->completion_status = VIRTIO_SND_S_OK;
            vsnd_completed_push_locked(props, node);
            pthread_cond_signal(&props->lock.completed);
        }
    }
    return copied;
}

static int virtio_snd_stream_cb(const void *input,
                                void *output,
                                unsigned long frame_cnt,
                                const PaStreamCallbackTimeInfo *time_info,
                                PaStreamCallbackFlags status_flags,
                                void *user_data)
{
    (void) input;
    (void) time_info;
    (void) status_flags;
    vsnd_stream_sel_t *sel = (vsnd_stream_sel_t *) user_data;
    virtio_snd_prop_t *props =
        (virtio_snd_prop_t *) ((uintptr_t) sel -
                               offsetof(virtio_snd_prop_t, v));

    uint32_t channels = props->pp.channels ? props->pp.channels : 1U;
    uint32_t out_bytes = (uint32_t) frame_cnt * channels * VSND_CNFA_FRAME_SZ;
    uint8_t *out = (uint8_t *) output;

    /*
     * Do not block the PortAudio callback on the PCM queue mutex. If another
     * thread briefly owns the lock, output silence for this callback instead.
     */
    if (pthread_mutex_trylock(&props->lock.lock) != 0) {
        memset(output, 0, out_bytes);
        return paContinue;
    }

    if (props->lock.releasing || props->lock.pending_pcm_bytes == 0) {
        pthread_mutex_unlock(&props->lock.lock);
        memset(output, 0, out_bytes);
        return paContinue;
    }

    uint32_t written = vsnd_queue_read_locked(props, out, out_bytes);
    if (written < out_bytes)
        memset(out + written, 0, out_bytes - written);
    pthread_mutex_unlock(&props->lock.lock);
    return paContinue;
}

static int virtio_snd_tx_desc_handler(virtio_snd_state_t *vsnd,
                                      virtio_snd_queue_t *queue,
                                      uint32_t desc_idx,
                                      uint32_t *plen)
{
    struct virtq_desc chain[VSND_QUEUE_NUM_MAX];
    uint32_t cnt = 0;
    *plen = 0;
    if (!vsnd_collect_desc_chain(vsnd, queue, desc_idx, chain, &cnt))
        return -1;
    if (cnt < 3)
        return -1;

    struct virtq_desc *xfer_desc = &chain[0];
    struct virtq_desc *status_desc = &chain[cnt - 1];
    if (xfer_desc->flags & VIRTIO_DESC_F_WRITE)
        return -1;
    if (!(status_desc->flags & VIRTIO_DESC_F_WRITE))
        return -1;
    if (xfer_desc->len < sizeof(virtio_snd_pcm_xfer_t) ||
        status_desc->len < sizeof(virtio_snd_pcm_status_t))
        return -1;

    virtio_snd_pcm_xfer_t *xfer =
        vsnd_guest_ptr(vsnd, xfer_desc->addr, sizeof(*xfer));
    virtio_snd_pcm_status_t *status =
        vsnd_guest_ptr(vsnd, status_desc->addr, sizeof(*status));
    if (!xfer || !status)
        return -1;

    uint64_t total64 = 0;
    for (uint32_t i = 1; i + 1 < cnt; i++) {
        if (chain[i].flags & VIRTIO_DESC_F_WRITE)
            return -1;
        total64 += chain[i].len;
        if (total64 > UINT32_MAX ||
            !vsnd_guest_range_ok(chain[i].addr, chain[i].len))
            return -1;
    }

    uint32_t stream_id = xfer->stream_id;
    if (stream_id >= VSND_DEV_CNT_MAX) {
        virtio_snd_complete_tx_request(vsnd, desc_idx, status_desc->addr,
                                       VIRTIO_SND_S_IO_ERR, 0);
        return 0;
    }

    virtio_snd_priv_t *p = vsnd_priv(vsnd);
    virtio_snd_prop_t *props = &p->props[stream_id];
    pthread_mutex_lock(&props->lock.lock);
    bool accepting = !props->lock.releasing &&
                     vsnd_stream_accepts_tx(props->pp.hdr.hdr.code);
    pthread_mutex_unlock(&props->lock.lock);
    if (!accepting || total64 == 0) {
        virtio_snd_complete_tx_request(vsnd, desc_idx, status_desc->addr,
                                       VIRTIO_SND_S_OK, 0);
        return 0;
    }

    vsnd_buf_queue_node_t *node = calloc(1, sizeof(*node));
    if (!node) {
        rv_log_error("virtio-snd: cannot allocate TX request node");
        virtio_snd_complete_tx_request(vsnd, desc_idx, status_desc->addr,
                                       VIRTIO_SND_S_IO_ERR, 0);
        return 0;
    }
    node->addr = malloc((size_t) total64);
    if (!node->addr) {
        rv_log_error("virtio-snd: cannot allocate %" PRIu64
                     " bytes for TX request",
                     total64);
        free(node);
        virtio_snd_complete_tx_request(vsnd, desc_idx, status_desc->addr,
                                       VIRTIO_SND_S_IO_ERR, 0);
        return 0;
    }

    uint32_t offset = 0;
    for (uint32_t i = 1; i + 1 < cnt; i++) {
        void *payload = vsnd_guest_ptr(vsnd, chain[i].addr, chain[i].len);
        if (!payload) {
            vsnd_free_node(node);
            return -1;
        }
        memcpy(node->addr + offset, payload, chain[i].len);
        offset += chain[i].len;
    }

    node->len = (uint32_t) total64;
    node->desc_idx = desc_idx;
    node->status_addr = status_desc->addr;
    node->stream_id = stream_id;
    node->completion_status = VIRTIO_SND_S_OK;

    pthread_mutex_lock(&props->lock.lock);
    props->lock.inflight_count++;
    accepting = !props->lock.releasing &&
                vsnd_stream_accepts_tx(props->pp.hdr.hdr.code);
    if (accepting) {
        vsnd_queue_push(props, node);
        props->lock.pending_pcm_bytes += node->len;
    } else {
        vsnd_completed_push_locked(props, node);
        pthread_cond_signal(&props->lock.completed);
    }
    pthread_mutex_unlock(&props->lock.lock);

    return 0;
}



static uint32_t virtio_snd_ctrl_process(virtio_snd_state_t *vsnd,
                                        const void *query,
                                        void *info,
                                        uint32_t type,
                                        uint32_t info_len,
                                        uint32_t *payload_len)
{
    *payload_len = 0;

    switch (type) {
    case VIRTIO_SND_R_JACK_INFO: {
        const virtio_snd_query_info_t *q = query;
        uint64_t need = (uint64_t) q->count * sizeof(virtio_snd_jack_info_t);
        if (info_len < need)
            return VIRTIO_SND_S_BAD_MSG;
        virtio_snd_read_jack_info_handler(vsnd, info, q, payload_len);
        return VIRTIO_SND_S_OK;
    }
    case VIRTIO_SND_R_PCM_INFO: {
        const virtio_snd_query_info_t *q = query;
        uint64_t need = (uint64_t) q->count * sizeof(virtio_snd_pcm_info_t);
        if (info_len < need)
            return VIRTIO_SND_S_BAD_MSG;
        virtio_snd_read_pcm_info_handler(vsnd, info, q, payload_len);
        return VIRTIO_SND_S_OK;
    }
    case VIRTIO_SND_R_CHMAP_INFO: {
        const virtio_snd_query_info_t *q = query;
        uint64_t need = (uint64_t) q->count * sizeof(virtio_snd_chmap_info_t);
        if (info_len < need)
            return VIRTIO_SND_S_BAD_MSG;
        virtio_snd_read_chmap_info_handler(vsnd, info, q, payload_len);
        return VIRTIO_SND_S_OK;
    }
    case VIRTIO_SND_R_PCM_SET_PARAMS:
        return virtio_snd_pcm_set_params(vsnd, query, payload_len);
    case VIRTIO_SND_R_PCM_PREPARE:
        return virtio_snd_pcm_prepare(vsnd, query, payload_len);
    case VIRTIO_SND_R_PCM_RELEASE:
        return virtio_snd_pcm_release(vsnd, query, payload_len);
    case VIRTIO_SND_R_PCM_START:
        return virtio_snd_pcm_start(vsnd, query, payload_len);
    case VIRTIO_SND_R_PCM_STOP:
        return virtio_snd_pcm_stop(vsnd, query, payload_len);
    default:
        return VIRTIO_SND_S_NOT_SUPP;
    }
}

static int virtio_snd_ctrl_desc_handler(virtio_snd_state_t *vsnd,
                                        virtio_snd_queue_t *queue,
                                        uint32_t desc_idx,
                                        uint32_t *plen)
{
    struct virtq_desc chain[VSND_QUEUE_NUM_MAX];
    uint32_t cnt = 0;

    if (!vsnd_collect_desc_chain(vsnd, queue, desc_idx, chain, &cnt))
        return -1;

    if (cnt < 2)
        return -1;

    struct virtq_desc *request_desc = &chain[0];
    struct virtq_desc *response_desc = &chain[1];

    if (request_desc->flags & VIRTIO_DESC_F_WRITE)
        return -1;

    if (!(response_desc->flags & VIRTIO_DESC_F_WRITE))
        return -1;

    if (request_desc->len < sizeof(virtio_snd_hdr_t))
        return -1;

    if (response_desc->len < sizeof(virtio_snd_hdr_t))
        return -1;

    virtio_snd_hdr_t *request_hdr =
        vsnd_guest_ptr(vsnd, request_desc->addr, sizeof(*request_hdr));
    virtio_snd_hdr_t *response =
        vsnd_guest_ptr(vsnd, response_desc->addr, sizeof(*response));

    if (!request_hdr || !response)
        return -1;

    uint32_t type = request_hdr->code;
    void *query = vsnd_guest_ptr(vsnd, request_desc->addr, request_desc->len);
    if (!query)
        return -1;

    void *info = NULL;
    uint32_t info_len = 0;

    if (cnt >= 3) {
        struct virtq_desc *info_desc = &chain[2];

        if (!(info_desc->flags & VIRTIO_DESC_F_WRITE))
            return -1;

        info = vsnd_guest_ptr(vsnd, info_desc->addr, info_desc->len);
        if (!info)
            return -1;

        info_len = info_desc->len;
    }

    switch (type) {
    case VIRTIO_SND_R_JACK_INFO:
    case VIRTIO_SND_R_PCM_INFO:
    case VIRTIO_SND_R_CHMAP_INFO:
        if (request_desc->len < sizeof(virtio_snd_query_info_t) || !info)
            return -1;
        break;
    case VIRTIO_SND_R_PCM_SET_PARAMS:
        if (request_desc->len < sizeof(virtio_snd_pcm_set_params_t))
            return -1;
        break;
    case VIRTIO_SND_R_PCM_PREPARE:
    case VIRTIO_SND_R_PCM_RELEASE:
    case VIRTIO_SND_R_PCM_START:
    case VIRTIO_SND_R_PCM_STOP:
        if (request_desc->len < sizeof(virtio_snd_pcm_hdr_t))
            return -1;
        break;
    default:
        break;
    }

    uint32_t payload_len = 0;
    uint32_t status = virtio_snd_ctrl_process(vsnd, query, info, type, info_len,
                                              &payload_len);

    response->code = status;

    *plen = sizeof(*response) + payload_len;

    if (status != VIRTIO_SND_S_OK)
        rv_log_error("virtio-snd: CTRL type=0x%x status=0x%x", type, status);

    return 0;
}

static vsnd_virtq_cb virtio_snd_queue_op(int index)
{
    switch (index) {
    case VSND_QUEUE_CTRL:
        return virtio_snd_ctrl_desc_handler;
    case VSND_QUEUE_TX:
        return virtio_snd_tx_desc_handler;
    default:
        return NULL;
    }
}

static void virtio_queue_notify_handler(virtio_snd_state_t *vsnd, int index)
{
    uint32_t *ram = vsnd->ram;
    virtio_snd_queue_t *queue = &vsnd->queues[index];
    bool async_tx = index == VSND_QUEUE_TX;
    uint32_t status = __atomic_load_n(&vsnd->status, __ATOMIC_ACQUIRE);
    if (status & VIRTIO_STATUS_DEVICE_NEEDS_RESET)
        return;
    if (!((status & VIRTIO_STATUS_DRIVER_OK) && queue->ready)) {
        if (async_tx)
            return;
        virtio_snd_set_fail(vsnd);
        return;
    }
    if (!queue->queue_num || queue->queue_num > VSND_QUEUE_NUM_MAX) {
        virtio_snd_set_fail(vsnd);
        return;
    }
    if (!vsnd_check_word_range(vsnd, queue->queue_avail, 1) ||
        !vsnd_check_word_range(vsnd, queue->queue_used, 1))
        return;

    uint16_t new_avail = ram[queue->queue_avail] >> 16;
    if (new_avail - queue->last_avail > (uint16_t) queue->queue_num) {
        virtio_snd_set_fail(vsnd);
        return;
    }
    if (queue->last_avail == new_avail)
        return;

    uint16_t new_used = async_tx ? 0 : ram[queue->queue_used] >> 16;
    while (queue->last_avail != new_avail) {
        uint16_t queue_idx = queue->last_avail % queue->queue_num;
        uint32_t avail_word = queue->queue_avail + 1 + queue_idx / 2;
        if (!vsnd_check_word_range(vsnd, avail_word, 1))
            return;
        uint16_t buffer_idx = ram[avail_word] >> (16 * (queue_idx % 2));
        if (buffer_idx >= queue->queue_num) {
            virtio_snd_set_fail(vsnd);
            return;
        }

        uint32_t len = 0;
        vsnd_virtq_cb handler = virtio_snd_queue_op(index);
        if (!handler || handler(vsnd, queue, buffer_idx, &len) != 0) {
            virtio_snd_set_fail(vsnd);
            return;
        }
        if (async_tx) {
            queue->last_avail++;
            continue;
        }

        uint32_t used_addr =
            queue->queue_used + 1 + (new_used % queue->queue_num) * 2;
        if (!vsnd_check_word_range(vsnd, used_addr, 2))
            return;

        ram[used_addr] = buffer_idx;
        ram[used_addr + 1] = len;
        queue->last_avail++;
        new_used++;
    }

    if (async_tx)
        return;
    ram[queue->queue_used] &= MASK(16);
    ram[queue->queue_used] |= ((uint32_t) new_used) << 16;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    if (!(ram[queue->queue_avail] & 1))
        __atomic_fetch_or(&vsnd->interrupt_status, VIRTIO_INT_USED_RING,
                          __ATOMIC_RELEASE);
}

static void virtio_snd_process_tx_queue(virtio_snd_state_t *vsnd)
{
    virtio_snd_priv_t *p = vsnd_priv(vsnd);
    pthread_mutex_lock(&p->tx_process_mutex);
    virtio_queue_notify_handler(vsnd, VSND_QUEUE_TX);
    pthread_mutex_unlock(&p->tx_process_mutex);
}


static void *virtio_snd_tx_thread(void *arg)
{
    virtio_snd_state_t *vsnd = (virtio_snd_state_t *) arg;
    virtio_snd_priv_t *p = vsnd_priv(vsnd);

    for (;;) {
        pthread_mutex_lock(&p->tx_mutex);

        while (p->tx_ev_notify <= 0 && !p->tx_thread_stop)
            pthread_cond_wait(&p->tx_cond, &p->tx_mutex);

        if (p->tx_thread_stop) {
            pthread_mutex_unlock(&p->tx_mutex);
            break;
        }

        p->tx_ev_notify--;
        pthread_mutex_unlock(&p->tx_mutex);
        virtio_snd_process_tx_queue(vsnd);
    }

    return NULL;
}

static uint32_t virtio_snd_config_read(virtio_snd_state_t *vsnd,
                                       uint32_t word_offset)
{
    virtio_snd_priv_t *p = vsnd_priv(vsnd);
    uint32_t *cfg = (uint32_t *) &p->config;

    if (word_offset >= sizeof(p->config) / sizeof(uint32_t)) {
        virtio_snd_set_fail(vsnd);
        return 0;
    }

    return cfg[word_offset];
}

static void virtio_snd_config_write(virtio_snd_state_t *vsnd,
                                    uint32_t word_offset,
                                    uint32_t value)
{
    virtio_snd_priv_t *p = vsnd_priv(vsnd);

    (void) value;

    if (word_offset >= sizeof(p->config) / sizeof(uint32_t)) {
        virtio_snd_set_fail(vsnd);
        return;
    }

    /* The VirtIO sound device-specific configuration is read-only. */
}

uint32_t virtio_snd_read(virtio_snd_state_t *vsnd, uint32_t addr)
{
    addr = addr >> 2;

#define _(reg) VIRTIO_##reg
    switch (addr) {
    case _(MagicValue):
        return VIRTIO_MAGIC_NUMBER;
    case _(Version):
        return VIRTIO_VERSION;
    case _(DeviceID):
        return VIRTIO_SND_DEV_ID;
    case _(VendorID):
        return VIRTIO_VENDOR_ID;
    case _(DeviceFeatures):
        return vsnd->device_features_sel == 0
                   ? VSND_FEATURES_0 | vsnd->device_features
                   : (vsnd->device_features_sel == 1 ? VSND_FEATURES_1 : 0);
    case _(QueueNumMax):
        return VSND_QUEUE_NUM_MAX;
    case _(QueueReady):
        return (uint32_t) VSND_QUEUE.ready;
    case _(InterruptStatus):
        return __atomic_load_n(&vsnd->interrupt_status, __ATOMIC_ACQUIRE);
    case _(Status):
        return __atomic_load_n(&vsnd->status, __ATOMIC_ACQUIRE);
    case _(ConfigGeneration):
        return VIRTIO_CONFIG_GENERATE;
    default:
        if (addr >= _(Config))
            return virtio_snd_config_read(vsnd, addr - _(Config));
        return 0;
    }
#undef _
}

void virtio_snd_write(virtio_snd_state_t *vsnd, uint32_t addr, uint32_t value)
{
    addr = addr >> 2;

#define _(reg) VIRTIO_##reg
    switch (addr) {
    case _(DeviceFeaturesSel):
        vsnd->device_features_sel = value;
        break;
    case _(DriverFeatures):
        if (vsnd->driver_features_sel == 0)
            vsnd->driver_features = value;
        break;
    case _(DriverFeaturesSel):
        vsnd->driver_features_sel = value;
        break;
    case _(QueueSel):
        if (value < ARRAY_SIZE(vsnd->queues))
            vsnd->queue_sel = value;
        else
            virtio_snd_set_fail(vsnd);
        break;
    case _(QueueNum):
        if (value > 0 && value <= VSND_QUEUE_NUM_MAX)
            VSND_QUEUE.queue_num = value;
        else
            virtio_snd_set_fail(vsnd);
        break;
    case _(QueueReady):
        VSND_QUEUE.ready = value & 1;
        if (value & 1)
            VSND_QUEUE.last_avail = vsnd->ram[VSND_QUEUE.queue_avail] >> 16;
        break;
    case _(QueueDescLow):
        VSND_QUEUE.queue_desc = vsnd_preprocess(vsnd, value);
        break;
    case _(QueueDescHigh):
        if (value)
            virtio_snd_set_fail(vsnd);
        break;
    case _(QueueDriverLow):
        VSND_QUEUE.queue_avail = vsnd_preprocess(vsnd, value);
        break;
    case _(QueueDriverHigh):
        if (value)
            virtio_snd_set_fail(vsnd);
        break;
    case _(QueueDeviceLow):
        VSND_QUEUE.queue_used = vsnd_preprocess(vsnd, value);
        break;
    case _(QueueDeviceHigh):
        if (value)
            virtio_snd_set_fail(vsnd);
        break;
    case _(QueueNotify):
        if (value >= ARRAY_SIZE(vsnd->queues)) {
            virtio_snd_set_fail(vsnd);
            break;
        }

        switch (value) {
        case VSND_QUEUE_CTRL:
            virtio_queue_notify_handler(vsnd, value);
            break;

        case VSND_QUEUE_TX: {
            virtio_snd_priv_t *p = vsnd_priv(vsnd);
            pthread_mutex_lock(&p->tx_mutex);
            p->tx_ev_notify++;
            pthread_cond_signal(&p->tx_cond);
            pthread_mutex_unlock(&p->tx_mutex);
            break;
        }

        case VSND_QUEUE_EVT:
            break;

        case VSND_QUEUE_RX:
            break;

        default:
            break;
        }
        break;
    case _(InterruptACK):
        __atomic_fetch_and(&vsnd->interrupt_status, ~value, __ATOMIC_ACQ_REL);
        break;
    case _(Status):
        virtio_snd_update_status(vsnd, value);
        break;
    default:
        if (addr >= _(Config))
            virtio_snd_config_write(vsnd, addr - _(Config), value);
        break;
    }
#undef _
}

static bool vsnd_init_prop(virtio_snd_prop_t *props)
{
    memset(props, 0, sizeof(*props));
    if (pthread_mutex_init(&props->lock.lock, NULL))
        return false;
    if (pthread_cond_init(&props->lock.completed, NULL)) {
        pthread_mutex_destroy(&props->lock.lock);
        return false;
    }
    if (pthread_cond_init(&props->lock.all_completed, NULL)) {
        pthread_cond_destroy(&props->lock.completed);
        pthread_mutex_destroy(&props->lock.lock);
        return false;
    }

    props->pp.hdr.hdr.code = VIRTIO_SND_R_PCM_SET_PARAMS;
    return true;
}

static void vsnd_destroy_prop(virtio_snd_prop_t *props)
{
    if (props->completion_thread_started)
        vsnd_reset_stream(props);

    pthread_mutex_lock(&props->lock.lock);
    props->completion_thread_stop = true;
    pthread_cond_signal(&props->lock.completed);
    pthread_mutex_unlock(&props->lock.lock);
    if (props->completion_thread_started) {
        pthread_join(props->completion_thread, NULL);
        props->completion_thread_started = false;
    }

    pthread_mutex_lock(&props->lock.lock);
    vsnd_clear_all_nodes_locked(props);
    pthread_mutex_unlock(&props->lock.lock);
    pthread_cond_destroy(&props->lock.completed);
    pthread_cond_destroy(&props->lock.all_completed);
    pthread_mutex_destroy(&props->lock.lock);
}

bool virtio_snd_init(virtio_snd_state_t *vsnd)
{
    if (!vsnd || !vsnd->priv)
        return false;

    virtio_snd_priv_t *p = vsnd_priv(vsnd);
    p->vsnd = vsnd;

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        rv_log_error("PortAudio Pa_Initialize failed: %s",
                     Pa_GetErrorText(err));
        return false;
    }

    p->pa_initialized = true;

    uint32_t started = 0;
    for (uint32_t i = 0; i < VSND_DEV_CNT_MAX; i++) {
        virtio_snd_prop_t *props = &p->props[i];
        props->vsnd = vsnd;
        if (pthread_create(&props->completion_thread, NULL,
                           virtio_snd_completion_thread, props) != 0) {
            rv_log_error("cannot create virtio-snd completion thread");
            goto fail;
        }
        props->completion_thread_started = true;
        started++;
    }

    if (pthread_create(&p->tx_thread, NULL, virtio_snd_tx_thread, vsnd) != 0) {
        rv_log_error("cannot create virtio-snd TX thread");
        goto fail;
    }
    p->tx_thread_started = true;
    return true;

fail:
    for (uint32_t i = 0; i < started; i++) {
        virtio_snd_prop_t *props = &p->props[i];
        pthread_mutex_lock(&props->lock.lock);
        props->completion_thread_stop = true;
        pthread_cond_signal(&props->lock.completed);
        pthread_mutex_unlock(&props->lock.lock);
        pthread_join(props->completion_thread, NULL);
        props->completion_thread_started = false;
    }
    Pa_Terminate();
    p->pa_initialized = false;
    return false;
}

virtio_snd_state_t *vsnd_new(void)
{
    virtio_snd_state_t *vsnd = calloc(1, sizeof(*vsnd));
    assert(vsnd);

    virtio_snd_priv_t *p = calloc(1, sizeof(*p));
    if (!p) {
        free(vsnd);
        return NULL;
    }

    p->config.jacks = 1;
    p->config.streams = 1;
    p->config.chmaps = 1;
    p->config.controls = 0;

    if (pthread_mutex_init(&p->tx_mutex, NULL)) {
        free(p);
        free(vsnd);
        return NULL;
    }

    if (pthread_cond_init(&p->tx_cond, NULL)) {
        pthread_mutex_destroy(&p->tx_mutex);
        free(p);
        free(vsnd);
        return NULL;
    }

    if (pthread_mutex_init(&p->tx_process_mutex, NULL)) {
        pthread_cond_destroy(&p->tx_cond);
        pthread_mutex_destroy(&p->tx_mutex);
        free(p);
        free(vsnd);
        return NULL;
    }

    if (pthread_mutex_init(&p->tx_used_mutex, NULL)) {
        pthread_mutex_destroy(&p->tx_process_mutex);
        pthread_cond_destroy(&p->tx_cond);
        pthread_mutex_destroy(&p->tx_mutex);
        free(p);
        free(vsnd);
        return NULL;
    }

    for (uint32_t i = 0; i < VSND_DEV_CNT_MAX; i++) {
        if (!vsnd_init_prop(&p->props[i])) {
            for (uint32_t j = 0; j < i; j++)
                vsnd_destroy_prop(&p->props[j]);
            pthread_mutex_destroy(&p->tx_used_mutex);
            pthread_mutex_destroy(&p->tx_process_mutex);
            pthread_cond_destroy(&p->tx_cond);
            pthread_mutex_destroy(&p->tx_mutex);
            free(p);
            free(vsnd);
            return NULL;
        }
    }

    vsnd->priv = p;
    return vsnd;
}

void vsnd_delete(virtio_snd_state_t *vsnd)
{
    if (!vsnd)
        return;

    virtio_snd_priv_t *p = vsnd_priv(vsnd);

    if (p) {
        if (p->tx_thread_started) {
            pthread_mutex_lock(&p->tx_mutex);
            p->tx_thread_stop = true;
            pthread_cond_signal(&p->tx_cond);
            pthread_mutex_unlock(&p->tx_mutex);
            pthread_join(p->tx_thread, NULL);
            p->tx_thread_started = false;
        }

        for (uint32_t i = 0; i < VSND_DEV_CNT_MAX; i++)
            vsnd_destroy_prop(&p->props[i]);

        if (p->pa_initialized) {
            Pa_Terminate();
            p->pa_initialized = false;
        }

        pthread_mutex_destroy(&p->tx_used_mutex);
        pthread_mutex_destroy(&p->tx_process_mutex);
        pthread_cond_destroy(&p->tx_cond);
        pthread_mutex_destroy(&p->tx_mutex);
        free(p);
    }

    free(vsnd);
}
