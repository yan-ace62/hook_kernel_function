#ifndef WORKER_H
#define WORKER_H

#include "conf.h"

void 
notify_workers_quit(void);

int 
check_workers_quit(void);

void *
receiver_worker(void *arg);

void *
sender_worker(void *arg);

#endif