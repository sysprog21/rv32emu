/*
 * Richards is an operating system task scheduler simulation benchmark,
 * designed for comparing system implementation languages. The benchmark
 * was originally implemented in BCPL by Martin Richards.
 * See https://www.cl.cam.ac.uk/~mr10/Bench.html for details.
 *
 * C version of the systems programming language benchmark
 * Author:  M. J. Jordan  Cambridge Computer Laboratory.
 *
 * Modified by: M. Richards, Nov 1996
 * to be ANSI C and runnable on 64 bit machines + other minor changes
 * Modified by: M. Richards, 20 Oct 1998
 * made minor corrections to improve ANSI compliance (suggested by David Levine)
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#define COUNT 10000
#define QPKT_COUNT_FINAL 23246
#define HOLD_COUNT_FINAL 9297

#define MAXINT 32767

#define BUFSIZE 3

enum {
    I_IDLE = 1,
    I_WORK,
    I_HANDLERA,
    I_HANDLERB,
    I_DEVA,
    I_DEVB,
};

#define PKT_BIT 1
#define WAIT_BIT 2
#define HOLD_BIT 4
#define NOTHOLD_BIT 0xFFFB

enum {
    S_RUN = 0,
    S_RUNPKT,
    S_WAIT,
    S_WAITPKT,
    S_HOLD,
    S_HOLDPKT,
    S_HOLDWAIT,
    S_HOLDWAITPKT,
};

#define K_DEV 1000
#define K_WORK 1001

struct packet {
    struct packet *p_link;
    int p_id;
    int p_kind;
    int p_a1;
    char p_a2[BUFSIZE + 1];
};

struct task {
    struct task *t_link;
    int t_id;
    int t_pri;
    struct packet *t_wkq;
    int t_state;
    struct task *(*t_fn)(struct packet *);
    uintptr_t t_v1, t_v2;
};

static const char alphabet[28] = "0ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static struct task *tasktab[11] = {
    (struct task *) 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static struct task *task_list = NULL;
static struct task *tcb;
static long taskid;
static uintptr_t v1, v2;
static int qpkt_count = 0, hold_count = 0;

void append(struct packet *pkt, struct packet *ptr);

void create_task(int id,
                 int pri,
                 struct packet *wkq,
                 int state,
                 struct task *(*fn)(struct packet *),
                 uintptr_t v1,
                 uintptr_t v2)
{
    struct task *t = malloc(sizeof(struct task));

    tasktab[id] = t;
    t->t_link = task_list;
    t->t_id = id;
    t->t_pri = pri;
    t->t_wkq = wkq;
    t->t_state = state;
    t->t_fn = fn;
    t->t_v1 = v1;
    t->t_v2 = v2;
    task_list = t;
}

struct packet *pkt(struct packet *link, int id, int kind)
{
    struct packet *p = malloc(sizeof(struct packet));

    for (int i = 0; i <= BUFSIZE; i++)
        p->p_a2[i] = 0;

    p->p_link = link;
    p->p_id = id;
    p->p_kind = kind;
    p->p_a1 = 0;

    return p;
}

void schedule()
{
    while (tcb) {
        struct packet *_pkt = NULL;

        switch (tcb->t_state) {
        case S_WAITPKT:
            _pkt = tcb->t_wkq;
            tcb->t_wkq = _pkt->p_link;
            tcb->t_state = !tcb->t_wkq ? S_RUN : S_RUNPKT;

        case S_RUN:
        case S_RUNPKT: {
            taskid = tcb->t_id;
            v1 = tcb->t_v1;
            v2 = tcb->t_v2;
            struct task *newtcb = (*(tcb->t_fn))(_pkt);
            tcb->t_v1 = v1;
            tcb->t_v2 = v2;
            tcb = newtcb;
            break;
        }

        case S_WAIT:
        case S_HOLD:
        case S_HOLDPKT:
        case S_HOLDWAIT:
        case S_HOLDWAITPKT:
            tcb = tcb->t_link;
            break;

        default:
            return;
        }
    }
}

struct task *wait_task(void)
{
    tcb->t_state |= WAIT_BIT;
    return tcb;
}

struct task *hold_self(void)
{
    ++hold_count;
    tcb->t_state |= HOLD_BIT;
    return tcb->t_link;
}

struct task *find_tcb(int id)
{
    struct task *t = NULL;

    if (1 <= id && id <= (long) tasktab[0])
        t = tasktab[id];
    if (!t)
        printf("\nBad task id %d\n", id);
    return t;
}

struct task *release(int id)
{
    struct task *t = find_tcb(id);
    if (!t)
        return NULL;

    t->t_state &= NOTHOLD_BIT;
    if (t->t_pri > tcb->t_pri)
        return t;

    return tcb;
}

struct task *qpkt(struct packet *pkt)
{
    struct task *t = find_tcb(pkt->p_id);
    if (!t)
        return NULL;

    qpkt_count++;

    pkt->p_link = NULL;
    pkt->p_id = taskid;

    if (!t->t_wkq) {
        t->t_wkq = pkt;
        t->t_state |= PKT_BIT;
        if (t->t_pri > tcb->t_pri)
            return t;
    } else {
        append(pkt, (struct packet *) &(t->t_wkq));
    }

    return tcb;
}

struct task *idlefn(struct packet *pkt)
{
    if (--v2 == 0)
        return hold_self();

    if ((v1 & 1) == 0) {
        v1 = (v1 >> 1) & MAXINT;
        return release(I_DEVA);
    }

    v1 = ((v1 >> 1) & MAXINT) ^ 0XD008;
    return release(I_DEVB);
}

struct task *workfn(struct packet *pkt)
{
    if (!pkt)
        return wait_task();

    v1 = I_HANDLERA + I_HANDLERB - v1;
    pkt->p_id = v1;

    pkt->p_a1 = 0;
    for (int i = 0; i <= BUFSIZE; i++) {
        v2++;
        if (v2 > 26)
            v2 = 1;
        (pkt->p_a2)[i] = alphabet[v2];
    }
    return qpkt(pkt);
}

struct task *handlerfn(struct packet *pkt)
{
    if (pkt)
        append(pkt, (struct packet *) (pkt->p_kind == K_WORK ? &v1 : &v2));

    if (v1) {
        struct packet *workpkt = (struct packet *) v1;
        int count = workpkt->p_a1;

        if (count > BUFSIZE) {
            v1 = (uintptr_t) (((struct packet *) v1)->p_link);
            return qpkt(workpkt);
        }

        if (v2) {
            struct packet *devpkt = (struct packet *) v2;
            v2 = (uintptr_t) (((struct packet *) v2)->p_link);
            devpkt->p_a1 = workpkt->p_a2[count];
            workpkt->p_a1 = count + 1;
            return qpkt(devpkt);
        }
    }
    return wait_task();
}

struct task *devfn(struct packet *pkt)
{
    if (!pkt) {
        if (!v1)
            return wait_task();
        pkt = (struct packet *) v1;
        v1 = 0;
        return qpkt(pkt);
    }
    v1 = (uintptr_t) pkt;
    return hold_self();
}

void append(struct packet *pkt, struct packet *ptr)
{
    pkt->p_link = NULL;

    while (ptr->p_link)
        ptr = ptr->p_link;

    ptr->p_link = pkt;
}

int bench()
{
    struct packet *wkq = NULL;

    create_task(I_IDLE, 0, wkq, S_RUN, idlefn, 1, COUNT);

    wkq = pkt(0, 0, K_WORK);
    wkq = pkt(wkq, 0, K_WORK);

    create_task(I_WORK, 1000, wkq, S_WAITPKT, workfn, I_HANDLERA, 0);

    wkq = pkt(0, I_DEVA, K_DEV);
    wkq = pkt(wkq, I_DEVA, K_DEV);
    wkq = pkt(wkq, I_DEVA, K_DEV);

    create_task(I_HANDLERA, 2000, wkq, S_WAITPKT, handlerfn, 0, 0);

    wkq = pkt(0, I_DEVB, K_DEV);
    wkq = pkt(wkq, I_DEVB, K_DEV);
    wkq = pkt(wkq, I_DEVB, K_DEV);

    create_task(I_HANDLERB, 3000, wkq, S_WAITPKT, handlerfn, 0, 0);

    wkq = NULL;
    create_task(I_DEVA, 4000, wkq, S_WAIT, devfn, 0, 0);
    create_task(I_DEVB, 5000, wkq, S_WAIT, devfn, 0, 0);

    tcb = task_list;

    qpkt_count = hold_count = 0;

    schedule();

    if (!(qpkt_count == QPKT_COUNT_FINAL && hold_count == HOLD_COUNT_FINAL)) {
        printf("qpkt_count = %d  hold_count = %d\n", qpkt_count, hold_count);
        printf("These results are incorrect");
        exit(1);
    }
    return qpkt_count;
}


int inner_loop(int inner)
{
    int r = 0;
    while (inner > 0) {
        r += bench();
        inner--;
    }
    return r;
}

static unsigned long microseconds()
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return (t.tv_sec * 1000 * 1000) + t.tv_usec;
}

int main(int argc, char *argv[])
{
    int iterations = 5;
    int inner_iterations = 20;

    printf("Richards benchmark starting...\n");

    int result = 0;
    while (iterations > 0) {
        unsigned long start = microseconds();
        result += inner_loop(inner_iterations);
        unsigned long elapsed = microseconds() - start;
        printf("  runtime: %lu us\n", elapsed);
        iterations--;
    }
    return 0;
}
