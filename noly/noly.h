#ifndef __NOLY_H
#define __NOLY_H
#include <stdint.h>

#ifndef MAX
#define MAX(a,b) a>b?a:b
#endif
#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif


void noly_hexdump(unsigned char *start, int len);
void noly_set_tcp_nodelay(int sk);
int noly_socket_set_nonblock(int sk);
int noly_tcp_socket(int port, int max_cli);
int noly_udp_rand_socket(int *port);
int noly_udp_sender(char *addr, int port, char *payload, int len);
int noly_udp_socket_from(int start, int *port);
int noly_tcp_socket_from(int start, int *port, int max_cli);
int noly_tcp_connect(char *ip, int port);

#endif
