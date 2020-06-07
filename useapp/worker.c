#include <dirent.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <stdatomic.h>

#include "queue.h"
#include "sender.h"
#include "worker.h"
#include "tlog.h"
#include "common.h"
#include "api_config.h"
#include "csnetlink.h"

#define MAX_BUFF_SIZE           (4096)

struct datafile_info {
    const char *path;
    int max_fcnt;
    int max_fsize;
    int file_count;
    FILE *curr_fp;
    int curr_fsize;
    char curr_fname[256];
    struct message *names_msg;
};

static atomic_int g_quit = ATOMIC_VAR_INIT(0);

void 
notify_workers_quit(void)
{
    atomic_fetch_add(&g_quit, 1);
    return;
}

int 
check_workers_quit(void)
{
    return atomic_load(&g_quit) == 1;
}

static struct datafile_info *
datafile_init(const char *dir, int max_fsize, int max_fcnt)
{
    struct stat st = {0};
    if (stat(dir, &st) == -1) {
        if (0 != mkdir(dir, 0700))
        {
            tlog(TLOG_ERROR, "create dir %s failed:%s", dir, strerror(errno));
            return NULL;
        }
    }

    struct datafile_info *info = calloc(1, sizeof(struct datafile_info));
    info->path = strdup(dir);
    info->max_fcnt = max_fcnt > 5 ? 4 : max_fcnt;
    info->max_fsize = max_fsize > (1024 * 1024 * 2) ? (1024 * 1024 * 2) : max_fsize;
    return info;
}

static int
create_datafile(struct datafile_info *info)
{
    time_t now;
    time(&now);
    int path_len = strlen(info->path);
    strncpy(info->curr_fname, info->path, sizeof(info->curr_fname) - 21);
    if (info->curr_fname[path_len-1] != '/')
    {
        info->curr_fname[path_len++] = '/';
    }
    strftime(info->curr_fname + path_len, 20, "%Y%m%d_%H%M%S.dat", localtime(&now));  // 20190117_133040.dat
    
    info->curr_fp = fopen(info->curr_fname, "a");
    if (NULL == info->curr_fp)
    {
        tlog(TLOG_ERROR, "creat file:%s failed:%s\n", info->curr_fname, strerror(errno));
        memset(info->curr_fname, 0, sizeof(info->curr_fname));
        return -1;
    } 

    return 0;
}

static void
close_datafile(struct datafile_info *info)
{
    if (NULL != info->curr_fp)
    {
        fclose(info->curr_fp);
        info->curr_fp = NULL;
    }
    memset(info->curr_fname, 0, sizeof(info->curr_fname));
    info->curr_fsize = 0;
    return;
}

static void
datafile_reset_currfile(struct datafile_info *info)
{   

    if (info->curr_fp)
    {
        if(info->file_count > 0)
        {
            info->file_count--;
        }
        info->names_msg->len -= (strlen(info->curr_fname) + 1);
        memset(info->curr_fname, 0, sizeof(info->curr_fname));
        
    }
    close_datafile(info);
    return;
}

static int
datafile_write_datamsg(struct datafile_info *info, struct message *data_msg)
{
    if (info->file_count >= info->max_fcnt && (info->curr_fsize + data_msg->len) > info->max_fsize)
    {
        tlog(TLOG_ERROR, "local file cache is full");
        return -1;
    }

    if (NULL != info->curr_fp)
    {
        if ((info->curr_fsize + data_msg->len) > info->max_fsize)
        {
            close_datafile(info);
        }
    }

    if (NULL == info->curr_fp)
    {
        if (0 != create_datafile(info))
        {
            tlog(TLOG_ERROR, "create data file failed");
            return -1;
        }
        info->file_count++;
        info->curr_fsize = 0;
        if (NULL == info->names_msg)
        {
            info->names_msg = message_new((info->max_fcnt+1)* (sizeof(info->curr_fname) + 1));
            info->names_msg->type = 1;
        }
        strcpy(info->names_msg->data + info->names_msg->len, info->curr_fname);
        info->names_msg->len += strlen(info->curr_fname) + 1; 
    }
    
    if (1 != fwrite(data_msg->data, data_msg->len, 1, info->curr_fp))
    {
        tlog(TLOG_ERROR, "write %d bytes into %s failed:%s", data_msg->len, info->curr_fname, strerror(errno));
        return -1;
    }

    info->curr_fsize += data_msg->len;
    return 0;
}

static void 
datafile_detach_namemsg(struct datafile_info *info)
{
    close_datafile(info);
    info->file_count = 0;
    info->names_msg = NULL;
    return;
}

struct queue *g_queue = NULL;

void*
receiver_worker(void *arg)
{
    fd_set read_fds;				
    int sockfd, nfds, nbytes;	
    struct timeval tv = {0, 0};        //sec, usec
    struct configure *conf = (struct configure *)arg;

    if((sockfd = netlink_sock_open(conf->listener)) < 0)
    {
        tlog(TLOG_ERROR, "open netlink:%d for monitor data failed\n", conf->listener);
        exit(1);
    }

    struct datafile_info *finfo = datafile_init(conf->data.path, conf->data.max_fsize, conf->data.max_fcnt);
    if (NULL == finfo)
    {
        tlog(TLOG_ERROR, "local datafile init failed\n");
        exit(1);
    }

    struct message *msg = NULL;
    while(1)
    { 			
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);
        tv.tv_sec = conf->recv_timeout;
		if ((nfds = select(sockfd + 1, &read_fds, NULL, NULL, &tv)) < 0)
        {
            tlog(TLOG_ERROR, "select err\n");
            continue;
        } 

        if (nfds == 0)      //检测到系统退出信号 优先退出 timeout
        {
            if(check_workers_quit() > 0)
            {
                if (NULL != msg && msg->len > 0)
                {
                    queue_push_wait(g_queue, msg, 0.5);
                    tlog(TLOG_INFO, "push last message(%d bytes) into queue before quit", msg->len);
                }
                tlog(TLOG_INFO, "log-receiver thread get notify, ending ...");
                break;
            }
        }

        if (msg == NULL)
        {
           msg = message_new(conf->queue.node_size);
        }

        if (NULL != finfo->names_msg)          //上一次写入文件, 本次优先检查能否push file-data message
        {
            if (0 == queue_push_wait(g_queue, finfo->names_msg, 0.5))
            {
                tlog(TLOG_DEBUG, "push a file-data message (%d bytes)into queue success\n", msg->len);
                datafile_detach_namemsg(finfo);
                msg->type = 0;      //push message to queue
            } else {
                tlog(TLOG_ERROR, "push a file-data message into queue failed, continue write to file...\n");
                msg->type = 1;      //file message to file
            }
        }

        if (nfds > 0 && FD_ISSET(sockfd, &read_fds)) 
        {
            if((nbytes = netlink_sock_recv(sockfd, CSNETLINK_A_LOG, &msg->data[msg->len], MAX_BUFF_SIZE)) < 0)
            {
                tlog(TLOG_ERROR, "ERROR in netlink_sock_recv()");
            } else 
            {
                msg->len += nbytes;
                tlog(TLOG_DEBUG, "netlink_sock_recv %d bytes data\n", nbytes);
            }
        }
        
        if (msg->len > 0 && (nfds == 0 || (msg->cap - msg->len) < (MAX_BUFF_SIZE + 1)))
        {
            if (0 == msg->type)
            {
                if (0 == queue_push_wait(g_queue, msg, 0.5))
                {
                    tlog(TLOG_DEBUG, "push a buff-data message (%d bytes) into queue success\n", msg->len);
                    MESSAGE_DETACH(msg);
                } else 
                {
                    msg->type = 1;
                    tlog(TLOG_WARN, "push a buff-data message (%d bytes) into queue failed, change to file-data\n", msg->len);
                }
            }

            if (NULL != msg && 1 == msg->type)
            {
                if (0 == datafile_write_datamsg(finfo, msg))
                {
                    tlog(TLOG_WARN, "write buff-data (%d bytes) into file success\n", msg->len);
                    MESSAGE_BZERO(msg);
                } else 
                {
                    tlog(TLOG_ERROR, "write buff-data (%d bytes) into file failed, drop message!!!\n", msg->len);
                    MESSAGE_DROP(msg);
                    datafile_reset_currfile(finfo);    
                }
            }    
        }
    }

    close(sockfd);
    return (void*)0;
}

static void
messge_filename_forward(struct message *msg)
{
    int namelen = strlen(msg->data);
    if (1 == msg->type)
    {
        if (msg->len > (namelen + 1))
        {
            memmove(msg->data, msg->data + namelen + 1, msg->len - namelen - 1);
            msg->len -= (namelen + 1);
        } else 
        {
            msg->len = 0;
            msg->data[0] = '\0';
        }
    }
    return;
}

//只有在进程退出的时候, 发送日志由于网络原因发送失败才dump到本地文件
static void 
message_dump_to_file(const char *path, const char *access_token, struct message *msg)
{
    time_t now;
    FILE *fp = NULL;
    char namebuffer[512] = "";
    do 
    {
        if (1 == msg->type)
        {
            while(strlen(msg->data) > 0 && 0 == access(msg->data, F_OK))
            {
                tlog(TLOG_DEBUG, "filename %s in message need dump before quit(rename with token)\n", msg->data);
                snprintf(namebuffer, sizeof(namebuffer) - 1, "%s/%s_%s", path, access_token, basename(msg->data));
                if(0 == rename(msg->data, namebuffer))
                {
                    tlog(TLOG_INFO, "rename %s to %s success\n", msg->data, namebuffer);
                } else 
                {
                    tlog(TLOG_ERROR, "rename %s to %s failed:%s, drop this file(file reserved)\n", msg->data, namebuffer, strerror(errno));
                }
                messge_filename_forward(msg); 
            }
        } else 
        {
            if (NULL == fp)
            {
                time(&now);
                snprintf(namebuffer, sizeof(namebuffer) - 20, "%s/%s_", path, access_token);
                strftime(namebuffer + strlen(namebuffer), 20, "%Y%m%d_%H%M%S.dat",  localtime(&now));
                if (NULL == (fp = fopen(namebuffer, "a")))
                {
                    tlog(TLOG_ERROR, "create dump file %s failed:%s, drop all messages before quit\n", namebuffer, strerror(errno));
                    return;
                }
                tlog(TLOG_INFO, "dump file %s create success", namebuffer);
            }
            if (NULL != fp)
            {
                if (1 != fwrite(msg->data, msg->len, 1, fp))
                {
                    tlog(TLOG_ERROR, "write data(%d bytes) to dump file failed:%s, drop this message\n", msg->len, strerror(errno));
                }
            }
        }
        MESSAGE_DEL(msg);
    }while((msg = queue_pop_wait(g_queue, 5)));
}

void* 
sender_worker(void *arg)
{
    int ret = 0;
    struct message *msg = NULL;
    struct configure *conf = (struct configure *)arg;
    char url[1024] = {0};
    snprintf(url, sizeof(url) - 1, "%s%s", conf->host, MONITOR_URLSUFF);
    for(;;)
    {
        msg = queue_pop_wait(g_queue, 5);
        if (!msg)
        {
            if(check_workers_quit())    //超时才去检查是否需要退出, 队列有数据的情况下则会优先获取到数据
            {
                tlog(TLOG_INFO, "log-sender thread get notify, ending ...");
                break;
            }
            continue;
        }

        tlog(TLOG_DEBUG, "log-sender pop a [%d] message (%d bytes) from queue", msg->type, msg->len);
        
        do
        {
            if (0 == msg->type) //log data in buffer of msg
            {
                ret = send_log_buff(url, conf->access_token, msg->data, msg->len);
                if (SND_SUCCESS == ret)
                {
                    MESSAGE_DEL(msg);
                    tlog(TLOG_INFO, "send a log buff-data message to server success\n");
                } else if (SND_ERR_SYS == ret || SND_ERR_SRV == ret)
                {
                    MESSAGE_DROP(msg);
                    tlog(TLOG_ERROR, "send a log buff-data message to server failed, drop this messsage\n");
                } else 
                {
                    tlog(TLOG_ERROR, "send a log buff-data message to server failed: network err. continue trying...\n");
                }
            } else              //log data in file, filename in buffer of msg
            {
                while(strlen(msg->data) > 0 && 0 == access(msg->data, F_OK))
                {
                    tlog(TLOG_DEBUG, "filename %s in message need to be send", msg->data);
                    ret = send_log_file(url, conf->access_token, msg->data);
                    if (SND_SUCCESS == ret)
                    {
                        messge_filename_forward(msg);
                        tlog(TLOG_INFO, "send a log file-data[%s] message to server success\n", msg->data);
                    } else if (SND_ERR_SYS == ret || SND_ERR_SRV == ret)
                    {
                        messge_filename_forward(msg);
                        tlog(TLOG_ERROR, "send a log buff-data message to server failed, drop this file(file reserved)\n");
                    }  else
                    {
                        tlog(TLOG_ERROR, "send a log file-data[%s] message to server failed: network err, continue trying...\n", msg->data);
                        break;
                    }
                }
            }

            if(NULL != msg && check_workers_quit())
            {
                tlog(TLOG_WARN, "log-sender thread get notify, but network err for sending, need dump");
                break;
            }
        }while(NULL != msg);
        
        if (NULL != msg)
        {
            tlog(TLOG_WARN, "start dump all message to file...");
            message_dump_to_file(conf->data.path, conf->access_token, msg);
            tlog(TLOG_WARN, "log-sender thread ending after dump");
            break;
        }
    }

    return (void *)0;
}




/*
//20181128_100535_01.dat
static int 
cache_file_filter(const struct dirent *d) 
{
    if (22 == strlen(d->d_name) && 0 == strcmp(d->d_name + strlen(d->d_name)-4, ".dat"))
    {
        return 1;
    } else 
    {
        return 0;
    }
}*/