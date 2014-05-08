#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/un.h>

#ifndef __IOS__
#include <net/if_arp.h>
#endif
#include <netinet/tcp.h>

#include <net/if.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include "noly.h"

void noly_hexdump(unsigned char *start, int len)
{
    int i = 0;
    for(i = 0; i < len ; i++) {
        printf("%02X ", start[i]);
        if((i+1) % 8 == 0) printf("\n");
    }
    printf("\n");
}

void noly_set_tcp_nodelay(int sk)
{
    int enable = 1;
    setsockopt(sk, IPPROTO_TCP, TCP_NODELAY, (void*)&enable, sizeof(enable));
}

int noly_socket_set_reuseaddr(int sk)
{
    int on = 1;
    return setsockopt(sk, SOL_SOCKET, SO_REUSEADDR, (const char *) &on, sizeof(on));
}

int noly_socket_set_nonblock(int sk)
{
    unsigned long on = 1;
    return ioctl(sk, FIONBIO, &on);
}

int noly_udp_rand_socket(int *port)
{
    int sock;
    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(struct sockaddr_in));
    unsigned sin_len = sizeof(struct sockaddr_in);
    serv.sin_addr.s_addr = htonl(INADDR_ANY);
    serv.sin_port = htons(0);
    serv.sin_family = AF_INET;
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(bind(sock, (struct sockaddr *) &serv, sin_len)<0){
        fprintf(stderr, "bind socket port error\n");
    }
    if(getsockname(sock, (struct sockaddr *)&serv, &sin_len) < 0){
        fprintf(stderr, "get socket name error\n");
    }
    int sport = htons(serv.sin_port);
    fprintf(stdout, "create udp random port %d\n", sport);
    *port = sport;
    return sock;
}

int noly_udp_socket(int port)
{
    int sock;
    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(struct sockaddr_in));
    unsigned sin_len = sizeof(struct sockaddr_in);
    serv.sin_addr.s_addr = htonl(INADDR_ANY);
    serv.sin_port = htons(port);
    serv.sin_family = AF_INET;
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(bind(sock, (struct sockaddr *) &serv, sin_len)<0){
        fprintf(stderr, "bind socket port error\n");
    }
    if(getsockname(sock, (struct sockaddr *)&serv, &sin_len) < 0){
        fprintf(stderr, "get socket name eerror\n");
    }
    fprintf(stdout, "create udp random port %d\n", port);
    return sock;
}
int noly_udp_sender(char *addr, int port, char *payload, int len)
{
    int sock;
    struct sockaddr_in serv_addr;
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock < 0) { return -1; }
    if(payload == NULL) { return -1; }
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(addr);
    serv_addr.sin_port = htons(port);
    ssize_t n = sendto(sock, payload, len, 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    close(sock);
    return n;
}

int noly_tcp_socket(int port, int max_cli)
{
    int max;
    if(port < 1 || port > 65535) return -1;
    if(max_cli < 1 || max_cli > 65535)
        max = 10;
    else
        max = max_cli;
    int sock = -1;
    struct sockaddr_in srv_addr;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock > 0){
        memset(&srv_addr, 0, sizeof(srv_addr));
        srv_addr.sin_family = AF_INET;
        srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        srv_addr.sin_port = htons(port);
        noly_socket_set_reuseaddr(sock);
        noly_socket_set_nonblock(sock);
        if(bind(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0 || listen(sock, max) != 0){
            printf("bind error (%d) %s\n", errno, strerror(errno));
            close(sock);
            sock = -1;
        }
        printf("tcp socket %d port %d have being created\n", sock, port);
    }
    return sock;
}

int noly_tcp_connect(char *ip, int port)
{
    if(!ip || port < 0 || port > 65535) return -1;
    int sk;
    struct sockaddr_in dest;
    sk = socket(AF_INET, SOCK_STREAM, 0);
    if(sk < 0) return INVALID_SOCKET;
    bzero(&dest, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    dest.sin_addr.s_addr = inet_addr(ip);
    noly_set_tcp_nodelay(sk);
    int ret = connect(sk , (struct sockaddr *)&dest, sizeof(dest));
    if(ret == 0) return sk;
    return INVALID_SOCKET;
}

