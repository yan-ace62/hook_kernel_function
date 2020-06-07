#include "queue.h"
#include "tlog.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

struct queue *
queue_init(int max)
{
    struct queue *q = (struct queue *)calloc(1, sizeof(struct queue));
    q->cirq = (void **)calloc(max, sizeof(void *));
    q->wpos = -1;
    q->rpos = 0;
    q->size = 0;
    q->max = max;
    //q->min = min;
    sem_init(&q->shared.mutex, 0, 1);
    sem_init(&q->shared.nempty, 0, max);
    sem_init(&q->shared.nstored, 0, 0);
    return q;
}

static int 
_circle_push(struct queue * q, void *value)
{
    int pos = (q->wpos + 1) % q->max;
    //write pos == read pos, full
    if (q->size >= q->max)
    {
        return -1;
    }
    // wpos step +1 first, then write
    q->cirq[pos] = value;
    q->wpos = pos;
    q->size++;
    return 0;
}

static void * 
_circle_pop(struct queue *q)
{
    //read pos== next write pos , empty
    if (q->size <= 0)
    {
        return NULL;    // empty
    }
    //read first, then rpos step +1. 
    void *v = q->cirq[q->rpos];
    q->cirq[q->rpos] = NULL;
    q->rpos = (q->rpos + 1) % q->max;
    q->size--;
    return v;
}

struct message *
message_new(int cap)
{
    struct message *msg = (struct message *)calloc(1, sizeof(struct message ) + cap);
    msg->type = 0;
    msg->len = 0;
    msg->cap = cap;
    return msg;
}

void 
message_del(struct message *msg)
{
    if (msg)
    {
#ifdef DEBUG_ALLLOG
        tlog(TLOG_DEBUG, "message len:%d, data:\n%s\n", msg->len, msg->data);
#endif
        free((void*)msg);
    }
    return;
}

void 
message_drop(struct message *msg)
{
    if (msg)
    {
        tlog(TLOG_WARN, "droped message len:%d, data:\n%s\n", msg->len, msg->data);
        free((void *)msg);
    }
    return;
}

int 
queue_push(struct queue *q, void *value)
{
    int r = 0;
    sem_wait(&q->shared.nempty);
    sem_wait(&q->shared.mutex);
    r = _circle_push(q, value);
    sem_post(&q->shared.mutex);
    sem_post(&q->shared.nstored);
    return r;
}

int 
queue_push_wait(struct queue *q, void *value, int sec)
{
    int r = 0;
    time_t raw = 0;
    time(&raw);
    struct timespec tv = {0, 0};
    tv.tv_sec = raw + sec;
    if (0 == sem_timedwait(&q->shared.nempty, &tv))
    {
        sem_wait(&q->shared.mutex);
        r = _circle_push(q, value);
        sem_post(&q->shared.mutex);
        sem_post(&q->shared.nstored);
        return r;
    }

    return 1;   //time out
}

void *
queue_pop(struct queue *q)
{
    void *v = NULL;
    sem_wait(&q->shared.nstored); //wait for at least 1 item stored
    sem_wait(&q->shared.mutex);   //lock the queue
    v = _circle_pop(q);
    sem_post(&q->shared.mutex);
    sem_post(&q->shared.nempty);
    return v;
}

void *
queue_pop_wait(struct queue *q, int sec)
{
    time_t raw = 0;
    time(&raw);
    struct timespec tv = {0, 0}; 
    tv.tv_sec = raw + sec;
    void *v = NULL;
    if (0 == sem_timedwait(&q->shared.nstored, &tv)) 
    {
        sem_wait(&q->shared.mutex);   //lock the queue
        v = _circle_pop(q);
        sem_post(&q->shared.mutex);
        sem_post(&q->shared.nempty);
    }
    
    return v;
}