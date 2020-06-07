#ifndef CONF_H
#define CONF_H

struct configure { 
    struct {
        int node_size;
        int node_count;
    }queue;

    struct {
        const char *path;
        int max_fcnt;
        int max_fsize;
    }data;
    const char *host;
    const char *access_token;
    int listener;
    int recv_timeout;
    int send_timeout;
    int ctrl_proc;
};

struct configure * 
load_configuration(int conf_fd);

int 
wait_user_loggin(int conf_fd, struct configure *conf);

#endif