#ifndef SENDER_H
#define SENDER_H

#define SND_SUCCESS  0
#define SND_ERR_SYS  1      //程序内部错误
#define SND_ERR_NET  2      //网络错误
#define SND_ERR_SRV  3      //远端服务器错误

int
send_log_file(const char *url, const char *token, const char*path);

int
send_log_buff(const char *url, const char *token, const char *buff, int len);

#endif