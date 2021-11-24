
#include <linux/netlink.h>
#include <linux/genetlink.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


void *pthread_work(void *id)
{
    pthread_t peer_portid = *(pthread_t *)id;
    int sock_fd;
    struct sockaddr_nl local, peer;  /* our local (user space) side of the communication */

    memset(&local, 0, sizeof(local)); /* fill-in local address information */
    local.nl_family = AF_NETLINK;
    local.nl_pid = pthread_self() + 2;

    printf("thread 0x%x\n", local.nl_pid);

    memset(&peer, 0, sizeof(peer)); /* fill-in local address information */
    peer.nl_family = AF_NETLINK;
    peer.nl_pid = peer_portid;

    sock_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);

    if (sock_fd < 0) {
        perror("crate socket errror");

        return NULL;
    }

    if (bind(sock_fd, (struct sockaddr *)&local, sizeof(struct sockaddr_nl)))
    {
        perror("bind socket errror");

        return NULL;
    }

    if (connect(sock_fd, (struct sockaddr *)&peer, sizeof(struct sockaddr)))
    {
        perror("connect socket errror");

        return NULL;
    }

    if (send(sock_fd, "Hello World", 12, 0) < 0)
    {
        perror("send socket errror");

        return NULL;
    }

    printf("child thread execute\n");

    return NULL;

}


int main(int argc, char *argv[])
{
    int sock_fd;
    pthread_t self_id, child;
    struct sockaddr_nl local;

    sock_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);

    if (sock_fd < 0) {
        perror("crate socket errror");

        return EXIT_FAILURE;
    }


    self_id = pthread_self();
    memset(&local, 0, sizeof(local)); /* fill-in local address information */
    local.nl_family = AF_NETLINK;
    local.nl_pid = self_id;

    printf("main 0x%x\n", local.nl_pid);

    if(bind(sock_fd, (struct sockaddr *)&local, sizeof(struct sockaddr_nl)))
    {
        perror("main bind socket errror");

        return EXIT_FAILURE;
    }

    if (pthread_create(&child, NULL, pthread_work, &self_id) < 0) 
    {
        perror("create thread error");
        return EXIT_FAILURE;
    }

    if (pthread_join(child, NULL) !=0){
        perror("join thread error");
        return EXIT_FAILURE;
    }

    // read mssage

    char buf[13] = {0,};

    if (recv(sock_fd, buf, 13, 0) == -1) {
        perror("recv  error");
        return EXIT_FAILURE;
    }

    printf("%s\n", buf);

}
