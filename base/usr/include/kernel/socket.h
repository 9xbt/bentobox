#pragma once
#include <stdint.h>
#include <kernel/list.h>
#include <kernel/vfs.h>

#define PF_UNSPEC	    0
#define PF_LOCAL	    1
#define PF_UNIX		    PF_LOCAL
#define PF_FILE		    PF_LOCAL
#define PF_INET		    2
#define PF_AX25		    3
#define PF_IPX		    4
#define PF_APPLETALK	5
#define PF_NETROM	    6
#define PF_BRIDGE	    7
#define PF_ATMPVC	    8
#define PF_X25		    9
#define PF_INET6	    10
#define PF_ROSE		    11
#define PF_DECnet	    12
#define PF_NETBEUI	    13
#define PF_SECURITY	    14
#define PF_KEY		    15
#define PF_NETLINK	    16
#define PF_ROUTE	    PF_NETLINK
#define PF_PACKET	    17
#define PF_ASH		    18
#define PF_ECONET	    19
#define PF_ATMSVC	    20
#define PF_RDS		    21
#define PF_SNA		    22
#define PF_IRDA		    23
#define PF_PPPOX	    24
#define PF_WANPIPE	    25
#define PF_LLC		    26
#define PF_IB		    27
#define PF_MPLS		    28
#define PF_CAN		    29
#define PF_TIPC		    30
#define PF_BLUETOOTH	31
#define PF_IUCV		    32
#define PF_RXRPC	    33
#define PF_ISDN		    34
#define PF_PHONET	    35
#define PF_IEEE802154	36
#define PF_CAIF		    37
#define PF_ALG		    38
#define PF_NFC		    39
#define PF_VSOCK	    40
#define PF_KCM		    41
#define PF_QIPCRTR	    42
#define PF_SMC		    43
#define PF_XDP		    44
#define PF_MCTP		    45

#define SO_DEBUG        1
#define SO_REUSEADDR    2
#define SO_TYPE         3
#define SO_ERROR        4
#define SO_DONTROUTE    5
#define SO_BROADCAST    6
#define SO_SNDBUF       7
#define SO_RCVBUF       8
#define SO_KEEPALIVE    9
#define SO_OOBINLINE    10
#define SO_LINGER       13
#define SO_RCVLOWAT     18
#define SO_SNDLOWAT     19
#define SO_ACCEPTCONN   30
#define SO_PROTOCOL     38
#define SO_DOMAIN       39

#define SOL_SOCKET      1

#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define SOCK_RAW        3
#define SOCK_RDM        4
#define SOCK_SEQPACKET  5
#define SOCK_DCCP       6
#define SOCK_PACKET     10

#define SOCK_CLOEXEC    02000000
#define SOCK_NONBLOCK   00004000

typedef unsigned short sa_family_t;
typedef unsigned int socklen_t;

struct sockaddr_un {
    sa_family_t sun_family;
    char        sun_path[108];
};

struct msghdr {
	void         *msg_name;
	socklen_t     msg_namelen;
	struct iovec *msg_iov;
	size_t        msg_iovlen;
	void         *msg_control;
	size_t        msg_controllen;
	int           msg_flags;
};

enum socket_state {
    SOCKET_NONE,
    SOCKET_LISTENING,
    SOCKET_CONNECTED
};

struct socket {
    int domain;
    int type;
    int backlog;
    enum socket_state state;
    vfs_node_t *node;
    list_t *pending;
    list_t *recv_queue;
    struct socket *peer;
    void *device;
};

struct socket_buffer {
    size_t len;
    size_t offset;
    void *data;
};

struct unix_socket;

int socket_new(int domain, int type, int protocol);
int socket_bind(int fd, const void *addr, uint32_t addrlen);
int socket_listen(int fd, int backlog);
int socket_connect(int fd, const void *addr, uint32_t addrlen);
int socket_accept(int fd, const void *addr, uint32_t *addrlen);
int socket_getsockopt(int fd, int level, int optname, void *optval, uint32_t *optlen);