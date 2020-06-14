#include <curl/curl.h>
#include <string.h>
#include <sys/time.h>

#include "cJSON.h"
#include "sender.h"
#include "common.h"
#include "tlog.h"

#define MAX_RESP_BUFF_SIZE  511
#define MAX_CHUNKED_DATA_SIZE   (1024*4)

#define HTTP_CONN_TIMEOUT   10L       //timeout for connect to server
#define HTTP_OPER_TIMEOUT  65L       //timeout for server operate

struct resp_buffer {
    char buff[MAX_RESP_BUFF_SIZE+1];
    int pos;
};

static size_t
receive_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = 0;
    struct resp_buffer *mem = (struct resp_buffer *)userp;
    if(mem == NULL)
    {
        printf("not buffer for receive response\n");
        return 0;
    }
    
    if (mem->pos < MAX_RESP_BUFF_SIZE)
    {
        realsize = (size * nmemb) > (MAX_RESP_BUFF_SIZE - mem->pos) ? (MAX_RESP_BUFF_SIZE - mem->pos) : (size * nmemb);
        memcpy(&mem->buff[mem->pos], contents,  realsize);
        mem->pos += realsize;
    } else 
    {
        mem->buff[MAX_RESP_BUFF_SIZE] = '\0';
    }

    return (size * nmemb);
}

/*
上报日志应答码: 200:成功  2006:用户未登录 1005: 请求参数错误 1001: 服务器内部错误
其中用户未登录情况  http为200 服务器应答吗2006两者不一致, 所以不能根据http应答吗来判断上送日志是否成功
*/
int
send_log_file(const char *url, const char *token, const char*path)
{
    CURLcode rc;
    long hc = 0;
    int flen = 0;
    int ret = 0;
    CURL *curl= NULL;
    struct curl_httppost *post=NULL;
    struct curl_httppost *last=NULL;
    struct curl_slist *chunk = NULL;

    if ((flen = get_file_size(path)) < 0)
    {
        tlog(TLOG_ERROR, "get_file_size %s failed", path);
        return SND_ERR_SYS;
    }

    if (NULL== (curl = curl_easy_init()))
    {
        tlog(TLOG_ERROR, "curl_easy_init failed");
        return SND_ERR_SYS;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    /* open debug output to stdout */
#ifdef DEBUG
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, HTTP_CONN_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, HTTP_OPER_TIMEOUT);

    // disable expect       
    chunk = curl_slist_append(chunk, "Expect:");
    if (flen > MAX_CHUNKED_DATA_SIZE)
    {
        chunk = curl_slist_append(chunk, "Transfer-Encoding: chunked");
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    
    //request form data
    curl_formadd(&post, &last, 
        CURLFORM_COPYNAME,"access_token", 
        CURLFORM_COPYCONTENTS, token, CURLFORM_END);
    
    curl_formadd(&post, &last, 
        CURLFORM_COPYNAME,"file", 
        CURLFORM_FILE, path,
        CURLFORM_FILENAME, "", CURLFORM_END);
    curl_easy_setopt(curl,CURLOPT_HTTPPOST, post);

    //response
    struct resp_buffer resp = {{0}, 0};
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&resp);

    /* Perform the request, res will get the return code */ 
    switch ((rc = curl_easy_perform(curl)))
    {
        case CURLE_OK:
            curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &hc);
            if (200 != hc) 
            {
                tlog(TLOG_ERROR, "curl performed, server operation failed, code: %ld", hc);
                ret = SND_ERR_SRV;
            } else 
            {
                cJSON *json = cJSON_Parse(resp.buff);
                cJSON *item = cJSON_GetObjectItem(json, "code");
                if (item && 200 == item->valueint)
                {
                    ret = SND_SUCCESS;
                } else 
                {
                    ret = SND_ERR_SRV;  //其他错误码没必要再去重发
                    tlog(TLOG_ERROR, "curl performed, server operation failed, err:%d", item ? item->valueint : 99);
                }
                cJSON_Delete(json);
            }
            break;
        case CURLE_COULDNT_RESOLVE_PROXY:
        case CURLE_COULDNT_RESOLVE_HOST:    /* 6 */
        case CURLE_COULDNT_CONNECT:         /* 7 */
        case CURLE_SEND_ERROR:              /* 55 - failed sending network data */
            tlog(TLOG_ERROR, "curl perform failed:%s", curl_easy_strerror(rc));
            ret = SND_ERR_NET;
            break;
        case CURLE_OPERATION_TIMEDOUT:      /* 28 - the timeout time was reached */
        case CURLE_RECV_ERROR:              /* 56 - failure in receiving network data */
            tlog(TLOG_ERROR, "curl performed, wait for server resp failed:%s", curl_easy_strerror(rc));
            ret = SND_ERR_SRV;
            break;
        default:
            ret = SND_ERR_SYS;
    }
      
    /* always cleanup */ 
    curl_easy_cleanup(curl);
    curl_formfree(post);
    curl_slist_free_all(chunk);

    return ret;
}

int
send_log_buff(const char *url, const char *token, const char *buff, int len)
{   
    CURLcode rc;
    long hc = 0;
    int ret = 0;
    CURL *curl= NULL;
    struct curl_httppost *post=NULL;
    struct curl_httppost *last=NULL;
    struct curl_slist *chunk = NULL;

    if (NULL== (curl = curl_easy_init()))
    {
        tlog(TLOG_ERROR, "curl_easy_init failed");
        return SND_ERR_SYS;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    /* open debug output to stdout */ 
#ifdef DEBUG
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif
    // disable expect       
    chunk = curl_slist_append(chunk, "Expect:");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, HTTP_CONN_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, HTTP_OPER_TIMEOUT);

    //request form data
    curl_formadd(&post, &last, 
        CURLFORM_COPYNAME,"access_token", 
        CURLFORM_COPYCONTENTS, token, 
       /* CURLFORM_CONTENTLEN, strlen(token),*/ CURLFORM_END);
    curl_formadd(&post, &last, 
        CURLFORM_COPYNAME,"file", 
        CURLFORM_BUFFER, "",        //set filename="", muset has 'filename' 
        CURLFORM_BUFFERPTR, buff,     
        CURLFORM_BUFFERLENGTH, len, CURLFORM_END);
    curl_easy_setopt(curl,CURLOPT_HTTPPOST, post);

    //response
    struct resp_buffer resp = {{0}, 0};
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&resp);

    /* Perform the request, res will get the return code */ 
    switch ((rc = curl_easy_perform(curl)))
    {
        case CURLE_OK:
            curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &hc);
            if (200 != hc) 
            {
                tlog(TLOG_ERROR, "curl performed, server operation failed, code: %ld", hc);
                ret = SND_ERR_SRV;
            } else 
            {
                cJSON *json = cJSON_Parse(resp.buff);
                cJSON *item = cJSON_GetObjectItem(json, "code");
                if (item && 200 == item->valueint)
                {
                    ret = SND_SUCCESS;
                } else 
                {
                    ret = SND_ERR_SRV;  //其他错误码没必要再去重发
                    tlog(TLOG_ERROR, "curl performed, server operation failed, err:%d", item ? item->valueint : 99);
                }
                cJSON_Delete(json);
            }
            break;
        case CURLE_COULDNT_RESOLVE_PROXY:
        case CURLE_COULDNT_RESOLVE_HOST:    /* 6 */
        case CURLE_COULDNT_CONNECT:         /* 7 */
        case CURLE_SEND_ERROR:              /* 55 - failed sending network data */
            tlog(TLOG_ERROR, "curl perform failed:%s", curl_easy_strerror(rc));
            ret = SND_ERR_NET;
            break;
        case CURLE_OPERATION_TIMEDOUT:      /* 28 - the timeout time was reached */
        case CURLE_RECV_ERROR:              /* 56 - failure in receiving network data */
            tlog(TLOG_ERROR, "curl performed, wait for server resp failed:%s", curl_easy_strerror(rc));
            ret = SND_ERR_SRV;
            break;
        default:
            ret = SND_ERR_SYS;
    }

    /* always cleanup */
    curl_easy_cleanup(curl);
    curl_formfree(post);
    curl_slist_free_all(chunk);
    return ret;
}