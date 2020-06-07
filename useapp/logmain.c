#include <stdio.h> 
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>  

#include <pthread.h>
#include <signal.h>

#include "common.h"
#include "queue.h"
#include "conf.h"
#include "worker.h"
#include "tlog.h"

void
exit_handler(void)
{
    tlog_exit();
    fprintf(stderr, "Process %d exiting ......\n", getpid());
    return;
}

extern struct queue *g_queue;

struct workers_arg {
    pthread_t *rtid, *stid;
    int wait_fd;
    struct configure *conf;
};

static void * 
start_workers(void * arg);

int main (int argc, const char*argv[])
{
    if (2 != argc)
    {
        fprintf(stderr, "Usage: %s logfile\n", argv[0]);
        exit(1);
    }

    //load configure
    int conf_fd;
    struct configure *conf;
    if ((conf_fd = configure_cli_open(1)) < 0) 
    {
        exit(-1);
    }
    if ((conf = load_configuration(conf_fd)) == NULL)
    {
        exit(-1);
    }
    
    sigset_t   waitset;
    sigemptyset(&waitset);
    sigaddset(&waitset, SIGHUP);
    sigaddset(&waitset, SIGQUIT);
    sigaddset(&waitset, SIGINT);
    sigaddset(&waitset, SIGTERM);
    sigaddset(&waitset, SIGTSTP);
    pthread_sigmask(SIG_BLOCK, &waitset, NULL);

    tlog_init(argv[1], 1024*1024*20, 0, 0, 1024*512, 0);
#ifdef DEBUG
    tlog_setlogscreen(1);   //for debug, output to stdout
    tlog_setlevel(TLOG_DEBUG);
#else
    tlog_setlevel(TLOG_INFO);
#endif
    atexit(exit_handler);

    g_queue = queue_init(conf->queue.node_count);

    pthread_t tid = 0, rtid = 0, stid = 0;
    struct workers_arg args;
    args.rtid = &rtid;
    args.stid = &stid;
    args.wait_fd = conf_fd;
    args.conf = conf;

    if (0 != pthread_create(&tid, NULL, start_workers, (void*)&args))
    {
        tlog(TLOG_ERROR, "pthread_create start_workers failed: %s\n", strerror(errno));
        exit(1);
    }

    siginfo_t  info;
    while (1)  
    {
        if (sigwaitinfo(&waitset, &info) == -1) 
        {
            tlog(TLOG_ERROR, "sigwaitinfo returned err: %d; %s\n", errno, strerror(errno));
            continue;
        }
        if (1 == info.si_pid)
        {
            tlog(TLOG_INFO, "receive signal %d from kernel_init, start quiting gracefully\n", info.si_signo);
            break;
        } else if(conf->ctrl_proc == info.si_pid && SIGHUP == info.si_signo)    //reload gracefully
        {
            tlog(TLOG_INFO, "receive signal HUP from cs ctrl-proc, starting reload\n");
            break;
        } else if (conf->ctrl_proc == info.si_pid && SIGQUIT == info.si_signo)  //shut down gracefully
        {
            tlog(TLOG_INFO, "receive signal QUIT from cs ctrl-proc, starting exit\n");
            break;
        } else if (conf->ctrl_proc == info.si_pid && SIGTERM == info.si_signo)  //shut down quickly
        {
            tlog(TLOG_INFO, "receive signal TERM from cs ctrl-proc, exit quickly\n");
            exit(0);
        } else 
        {
            tlog(TLOG_INFO, "receive signal %d from %d, ignore it\n", info.si_signo, info.si_pid);
        }
    }

    notify_workers_quit();
    if (rtid > 0)
    {
        pthread_join(rtid, 0);
        tlog(TLOG_INFO, "pthread_join receiver_worker returned");
    } else 
    {
        tlog(TLOG_INFO, "receiver_worker not runing, no need pthread_join");
    }
    
    if (stid > 0)
    {
        pthread_join(stid, 0);
        tlog(TLOG_INFO, "pthread_join sender_worker returned");
    } else 
    {
        tlog(TLOG_INFO, "sender_worker not runing, no need pthread_join");
    }
   
    tlog(TLOG_INFO, "report service process end ...");
    if (SIGHUP == info.si_signo)
    {
        if (execlp(argv[0], argv[0], argv[1], NULL) < 0)
        {
            tlog(TLOG_ERROR, "restart a new process by exec failed: %s", strerror(errno));
        }
    }

    exit(0);
}

static void * 
start_workers(void * arg)
{
    pthread_detach(pthread_self());
    pthread_t *rtid = ((struct workers_arg *)arg)->rtid;
    pthread_t *stid = ((struct workers_arg *)arg)->stid;
    struct configure *conf = ((struct workers_arg *)arg)->conf;
    int fd = ((struct workers_arg *)arg)->wait_fd;
    
    tlog(TLOG_INFO, "waiting for user account loggin ...");
    wait_user_loggin(fd, conf);
    tlog(TLOG_INFO, "user account loggin success!!!");
    configure_cli_close(1, fd);

    int iret;
    if (0 != (iret = pthread_create(rtid, NULL, receiver_worker, (void*)conf)))
    {
        tlog(TLOG_ERROR, "pthread_create receiver_worker failed, return code: %d\n", iret);
        exit(1);
    }
    if (0 != pthread_create(stid, NULL, sender_worker, (void*)conf))
    {
        tlog(TLOG_ERROR, "pthread_create start_workers failed: %s\n", strerror(errno));
        exit(1);
    }

    tlog(TLOG_INFO, "Linux safe-agent logd service start working !!!");

    return (void *)0;
}

