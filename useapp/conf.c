#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "conf.h"
#include "common.h"

static int 
get_configuration_int(int conf_fd, const char *path)
{
    int ret = -1;
    cJSON *result = NULL, *json = NULL;
    if(NULL == (result = configure_cli_do(conf_fd, CONF_JSON_GET, path, NULL)))
    {
        return ret;
    }

    do
    {
        json = cJSON_GetObjectItem(result, "code");
        if (0 != json->valueint)
        {
            //tlog(TLOG_ERROR, "config get path %s failed: %s\n", path, );
            break;
        }
         
        json = cJSON_GetObjectItem(result, "data");
        if (!json || json->type != cJSON_Number)
        {
            //tlog(TLOG_ERROR, "config get path %s value type not int or data empty\n", path);
            break;
        }
        ret = json->valueint;
    }while(0);
    
    cJSON_Delete(result);
    return ret;
}

static int 
set_configuration_int(int conf_fd, const char *path, int val)
{
    int ret = 0;
    cJSON *result = NULL, *json = NULL;
    json = cJSON_CreateNumber((double)val);
    result = configure_cli_do(conf_fd, CONF_JSON_SET, path, json);
    cJSON_Delete(json);

    json = cJSON_GetObjectItem(result, "code");
    if (!json || 0 != json->valueint)
    {
        ret = -1;
    }
    cJSON_Delete(result);
    return ret;
}

static const char*
get_configuration_string(int conf_fd, const char *path)
{
    const char* ret = NULL;
    cJSON *result = NULL, *json = NULL;
    if (NULL == (result = configure_cli_do(conf_fd, CONF_JSON_GET, path, NULL)))
    {
        return NULL;
    }

    do
    {
        json = cJSON_GetObjectItem(result, "code");
        if (0 != json->valueint)
        {
            //tlog(TLOG_ERROR, "config get path %s failed: %s\n", path, );
            break;
        }
         
        json = cJSON_GetObjectItem(result, "data");
        if (!json || json->type != cJSON_String)
        {
            //tlog(TLOG_ERROR, "config get path %s value type not string, type:%d\n", path, json->type);
            break;
        }
        ret = strdup(json->valuestring);
    }while(0);
    
    cJSON_Delete(result);
    return ret;
}

int 
wait_user_loggin(int conf_fd, struct configure *conf)
{   
    for(;;)
    {
        conf->access_token = get_configuration_string( conf_fd, "/user/access_token");
        if (conf->access_token)
        {
            break;
        }
        sleep(30);
    }

    return 0;
}

struct configure * 
load_configuration(int conf_fd)
{
    struct configure *conf = NULL; 

    do 
    {
        conf = calloc(1, sizeof(struct configure));
 
        conf->host = get_configuration_string(conf_fd, "/urls/addr");
        if (NULL == conf->host)
        {
            fprintf(stderr, "config get /urls/addr failed\n");
            break;
        }
       
        conf->listener = get_configuration_int(conf_fd, "/logd/listener");
        if (-1 == conf->listener)
        {
            fprintf(stderr, "config get /logd/listener failed\n");
            break;
        }
        
        conf->data.path = get_configuration_string(conf_fd, "/logd/data/path");
        if (NULL == conf->data.path)
        {
            fprintf(stderr, "config get /logd/data/path failed\n");
            break;
        }

        conf->data.max_fcnt = get_configuration_int(conf_fd, "/logd/data/max_fcount");
        if (-1 == conf->data.max_fcnt)
        {
            fprintf(stderr, "config get /logd/data/max_fcount failed\n");
            break;
        }

        conf->data.max_fsize = get_configuration_int(conf_fd, "/logd/data/max_fsize");
        if (-1 == conf->data.max_fsize)
        {
            fprintf(stderr, "config get /logd/data/max_fsize failed\n");
            break;
        }

        conf->queue.node_size = get_configuration_int(conf_fd, "/logd/queue/node_size");
        if (-1 == conf->queue.node_size)
        {
            fprintf(stderr, "config get /logd/queue/node_size failed\n");
            break;
        }

        conf->queue.node_count = get_configuration_int(conf_fd, "/logd/queue/node_count");
        if (-1 == conf->queue.node_count)
        {
            fprintf(stderr, "config get /logd/queue/node_count failed\n");
            break;
        }

        conf->recv_timeout = get_configuration_int(conf_fd, "/logd/recv_timeout");
        if (-1 == conf->recv_timeout)
        {
            fprintf(stderr, "config get /logd/recv_timeout failed\n");
            break;
        }

        conf->send_timeout = get_configuration_int(conf_fd, "/logd/send_timeout");
        if ( -1 == conf->send_timeout)
        {
            fprintf(stderr, "config get /logd/send_timeout failed\n");
            break;
        }

        conf->ctrl_proc = get_configuration_int(conf_fd, "/configd/pid");
        if ( -1 == conf->ctrl_proc)
        {
            fprintf(stderr, "config get /configd/pid failed\n");
            break;
        }

        if (-1 == set_configuration_int(conf_fd, "/logd/pid", getpid()))
        {
            fprintf(stderr, "config set /logd/pid failed\n");
            break;
        }

        return conf;
    } while(0);
    
    free(conf);
    return NULL;
}



