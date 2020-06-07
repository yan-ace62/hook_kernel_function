#ifndef QUEUE_H
#define QUEUE_H

#include <semaphore.h>

struct message {
    int type;
    int len;
    int cap;
    char data[0];
};


struct queue {
    void **cirq;     //circle queue for void * 
    int wpos;       //write position
    int rpos;       //read position
    int size;
    int max;
    //int min;
    struct {
        sem_t mutex;
        sem_t nempty;       //max - size
        sem_t nstored;      //size
    }shared;
};

//#define MESSAGE_NEW(c)          message_new(c)
#define MESSAGE_DETACH(m)       do{m = NULL;}while(0)
#define MESSAGE_BZERO(m)        do{memset(m, 0, sizeof(struct message ) + m->cap);}while(0)
#define MESSAGE_DEL(m)          do{message_del(m);m = NULL;}while(0)
#define MESSAGE_DROP(m)         do{message_drop(m); m = NULL;}while(0)

struct message *
message_new(int cap);

void 
message_del(struct message *msg);

void 
message_drop(struct message *msg);

struct queue *
queue_init(int max);

int 
queue_push(struct queue *q, void *value);

int
queue_push_wait(struct queue *q, void *value, int sec);

void *
queue_pop(struct queue *q);

void *
queue_pop_wait(struct queue *q, int sec);

#endif