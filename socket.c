/* accept - listen for and accept a remote network connection on a given port */
/*
   Copyright (C) 2020,2022,2023,2026 Free Software Foundation, Inc.

   This file is part of GNU Bash.
   Bash is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Bash is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Bash.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "socket.h"

#define FAIL_EXECUTION(fd, ...) do {                        \
    SOCKET_FAIL_EXECUTION(fd, -1, __VA_ARGS__); } while (0)

static const int socket_send(intmax_t, WORD_LIST *, vars);
static const int socket_recv(intmax_t, WORD_LIST *, vars);

static int
socket_builtin(WORD_LIST *list) {
    soaddr addr, laddr;
    intmax_t opt, queue = 5, sendfd = -1, recvfd = -1, sockfd = -1, n;
    int af = -1, socktype = -1, op = -1;
    char varbuf[VARSIZE];
    socklen_t addrlen;
    vars var;
    memset((char *)&laddr, 0, sizeof(soaddr));
    memset((char *)&addr, 0, sizeof(soaddr));
    memset((char *)&var, 0, sizeof(var));

    /* First check for -h, -s or -r on the command line */
    if (list == NULL)
        USAGE();
    reset_internal_getopt ();
    while ((opt = internal_getopt (list, "e:hm:r:s:")) != -1) {
        if (opt == 's' || opt == 'r') {
            if (!valid_number(list_optarg, opt == 'r' ? &recvfd : &sendfd))
                FAIL_EXECUTION(-1, "invalid file descriptor: %s",
                               list_optarg);
            list = list->next;
            return opt == 's' ? socket_send(sendfd, list, var)
                              : socket_recv(recvfd, list, var);
        } else if (opt == 'e' || opt == 'm') {
            if (opt == 'e' && iperr == 0)
                FAIL_EXECUTION(-1, "socket error queue not supported", 0);
            n = sizeof(addr);
            op = sizeof(socktype);
            if (    !valid_number(list_optarg, &recvfd)
                 || getsockname(recvfd, (SA)&addr, (socklen_t *)&n) < 0
                 || getsockopt(recvfd, SOL_SOCKET, SO_TYPE, &socktype, &op) < 0
                 || addr.sa.sa_family == AF_UNIX)
                FAIL_EXECUTION(-1, "invalid file descriptor: %s", list_optarg);
            n = 1;
            op = sizeof(n);
            if (    (list = list->next) != NULL
                 && (!valid_number(list->word->word, &n) || n < 0 || n > 1))
                FAIL_EXECUTION(-1, "%s must be 0 or 1",
                               opt == 'e' ? "errqueue" : "mtu");
            af = opt == 'e' ? IPERR : IPMTU;
            if (setsockopt(recvfd, IPPROTO_IP, af, &n, op) < 0)
                FAIL_EXECUTION(sockfd, "setsockopt %s: %s",
                               af == IPERR ? "IPERR" : "IPMTU",
                               strerror(errno));
            af = opt == 'e' ? IPERR6 : IPMTU6;
            if (    addr.sa.sa_family == AF_INET6
                 && setsockopt(recvfd, IPPROTO_IPV6, af, &n, op) < 0)
                FAIL_EXECUTION(sockfd, "setsockopt %s: %s",
                               af == IPERR6 ? "IPERR6" : "IPMTU6",
                               strerror(errno));
            return EXECUTION_SUCCESS;
        } else
            USAGE();
    }
    if (((list = loptend) == NULL || no_options(list)) && sendfd == -1)
        USAGE();

    /* Result variable name */
    if (    strcasecmp("SOCK_STREAM", list->word->word) != 0
         && strcasecmp("SOCK_DGRAM", list->word->word) != 0
         && strcasecmp("STREAM", list->word->word) != 0
         && strcasecmp("DGRAM", list->word->word) != 0) {
        if (strlen(list->word->word) > VARSIZE - 1 - 3)
            FAIL_EXECUTION(-1, "variable base must be <%d characters",
                           VARSIZE - 3);
        var.base = list->word->word;
        if (!valid_identifier(var.base))
            FAIL_EXECUTION(-1, "%s: invalid variable name", var.sock);
        list = list->next;
    } else
        var.base = "SOCK";
    if (   (n = snprintf(varbuf, sizeof(varbuf), "%s_%s", var.base, "FD")) < 0
         || n > sizeof(varbuf))
        FAIL_EXECUTION(-1, "buffer error: %s", strerror(errno));
    var.sock = varbuf;
    if (list == NULL)
        USAGE();
    socket_unbind_variables(var);
    /* Parse socket type keyword. */
    if (    strcasecmp("SOCK_STREAM", list->word->word) == 0
         || strcasecmp("STREAM", list->word->word) == 0)
        socktype = SOCK_STREAM;
    else if (    strcasecmp("SOCK_DGRAM", list->word->word) == 0
              || strcasecmp("DGRAM", list->word->word) == 0)
        socktype = SOCK_DGRAM;
    if (socktype < 0 || (list = list->next) == NULL)
        USAGE();
    /* 0 = bind + listen, 1 = connect */
    if (strcasecmp("local", list->word->word) == 0)
        op = 0;
    else if (strcasecmp("peer", list->word->word) == 0)
        op = 1;
    if (op < 0 || (list = list->next) == NULL)
        USAGE();
    /* Parse address */
    addrlen = af == AF_INET  ? sizeof(addr.in) :
                    AF_INET6 ? sizeof(addr.in6)
                             : sizeof(addr.un);
    if ((errno = socket_parse_address(&list, &addr, NULL, 1)) < 0)
        FAIL_EXECUTION(-1, NULL, 0);
    if (op == 1 && list != NULL && strcasecmp(list->word->word, "local") == 0) {
        if ((list = list->next) == NULL)
            USAGE();
        if ((errno = socket_parse_address(&list, &laddr, NULL, 2)) < 0)
            FAIL_EXECUTION(-1, NULL, 0);
    }
    af = addr.sa.sa_family;

    if (op == 0) {
        if (list != NULL && (sscanf(list->word->word, "%u", &queue) < 1))
            FAIL_EXECUTION(-1, "invalid queue size: %s", list->word->word);
        /* Fail on extraneous arguments. */
        if (list != NULL && (list = list->next) && list != NULL)
            USAGE();
        if ((sockfd = socket(af, socktype, 0)) < 0)
            FAIL_EXECUTION(-1, "%s", strerror(errno));
        if (bind(sockfd, (SA)&addr, addrlen) < 0)
            FAIL_EXECUTION(sockfd, "bind: %s", strerror(errno));
        if (socktype == SOCK_STREAM && listen(sockfd, queue) < 0) {
            if (af == AF_UNIX)
              unlink(addr.un.sun_path);
            FAIL_EXECUTION(sockfd, "listen: %s", strerror(errno));
        }
    } else {
        /* Fail on extraneous arguments. */
        if (list != NULL)
            USAGE();
        if ((sockfd = socket(af, socktype, 0)) < 0)
            FAIL_EXECUTION(-1, "%s", strerror(errno));
        /* Bind local socket if specified */
        if (laddr.sa.sa_family != 0 && bind(sockfd, (SA)&laddr, addrlen) < 0)
            FAIL_EXECUTION(sockfd, "bind: %s", strerror(errno));
        if (connect(sockfd, (SA)&addr, addrlen) < 0) {
            if (laddr.sa.sa_family == AF_UNIX)
              unlink(laddr.un.sun_path);
            FAIL_EXECUTION(sockfd, "connect: %s", strerror(errno));
        }
    }
    if (pktinfo46 != 0 && socktype == SOCK_DGRAM && af != AF_UNIX) {
        op = 1;
        opt = af == AF_INET ? PKTINFO : PKTINFO6;
        socktype = af == AF_INET ? IPPROTO_IP : IPPROTO_IPV6;
        if (setsockopt(sockfd, socktype, opt, &op, sizeof(op)) < 0)
            FAIL_EXECUTION(sockfd, "setsockopt IP%s_PKTINFO: %s",
                           socktype == AF_INET ? "" : "V6", strerror(errno));
    }
    if (!socket_bind_variable(var.sock, sockfd, 0, NULL))
        FAIL_EXECUTION(sockfd, NULL, 0);
    return EXECUTION_SUCCESS;
}

static const int socket_send
(intmax_t fd, WORD_LIST *list, vars var) {
    soaddr saddr, daddr;
    intmax_t queue, r, s, n, opt;
    unsigned char *databuf, varbuf[VARSIZE], *p;
    struct iovec iov[1];
    unsigned char cmsg[cmsglen];
    struct cmsghdr *cmsgh = (struct cmsghdr *)&cmsg;
    struct msghdr msg = { NULL, 0, iov, 1, NULL, 0, 0 };
    pktinfo *pkti;
    memset((char *)&saddr, 0, sizeof(soaddr));
    memset((char *)&daddr, 0, sizeof(soaddr));
    memset((char *)&cmsg, 0, cmsglen);

    /* Remote and local address is optional for a connected socket */
    /* For a server socket (bind only) a remote address is required */
   if (list && strcasecmp(list->word->word, "-b") == 0) {
        if ((list = list->next) == NULL || list->next != NULL)
            USAGE();
        if (!valid_number(list->word->word, &n) || n < 65536 || n > 2147483647)
            FAIL_EXECUTION(-1, "invalid buffer size: %s", list->word->word);
        if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &n, sizeof(n)) < 0)
            FAIL_EXECUTION(-1, "setsockopt SO_SNDBUF: %s", strerror(errno));
        return EXECUTION_SUCCESS;
   } else if (    list && strcasecmp(list->word->word, "peer") == 0
               && (list = list->next)) {
        if ((errno = socket_parse_address(&list, &daddr, NULL, 1)) < 0)
            FAIL_EXECUTION(-1, NULL, 0);
        if (    list != NULL && strcasecmp(list->word->word, "local") == 0
             && (list = list->next)) {
            if (pktinfo46 == 0)
                builtin_error("local send address not supported.", 0);
            if ((errno = socket_parse_address(&list, &saddr, NULL, 0)) < 0)
                FAIL_EXECUTION(-1, NULL, 0);
        }
    }
    /* Parse the rest of the command line for varname and queue size */
    for (queue = 0, var.base = NULL; list; list = list->next) {
        /* Fail on extraneous arguments. */
        if (var.base != NULL && queue != 0)
            USAGE();
        if (valid_number(list->word->word, &queue)) {
            if (queue  < 1)
                FAIL_EXECUTION(-1, "must specify send size > 0", 0);
        } else if (valid_identifier(list->word->word)) {
            if (strlen(list->word->word) > VARSIZE - 1 - 6)
                FAIL_EXECUTION(-1, "variable base must be <%d characters",
                               VARSIZE - 6);
            var.base = list->word->word;
        } else
            FAIL_EXECUTION(-1, "invalid vrariable name or size: %s",
                           list->word->word);
    }
    if (var.base == NULL)
        var.base = "SOCK";
    if (  (n = snprintf(varbuf, sizeof(varbuf), "%s_%s", var.base, "SSIZE")) < 0
         || n > sizeof(varbuf))
        FAIL_EXECUTION(-1, "buffer error: %s", strerror(errno));
    var.size = varbuf;
    if (queue == 0)
        queue = 8192;
    socket_unbind_variables(var);

    /* Initialze control message in case a local address was specified 
     * We only send one msg. FRSTHDR macro leads to segfault if
     * controllen is not set first, so we use a direct pointer to the cmsg */
    if (daddr.sa.sa_family != 0) {
        msg.msg_name = &daddr;
        msg.msg_namelen = sizeof(soaddr);
    }
    if (pktinfo46 != 0 && (    saddr.sa.sa_family == AF_INET
                            || saddr.sa.sa_family == AF_INET6)) {
        msg.msg_control = &cmsg;
        pkti = (pktinfo *)CMSG_DATA(cmsgh);
        if (saddr.sa.sa_family == AF_INET6) {
            msg.msg_controllen = CMSG_SPACE(sizeof(pkti->ip6));
            cmsgh->cmsg_level = IPPROTO_IPV6;
            cmsgh->cmsg_type = PKTINFO6S;
            cmsgh->cmsg_len = CMSG_LEN(sizeof(pkti->ip6));
            memcpy(&pkti->ip6.ipi6_addr, &saddr.in6.sin6_addr,
                   sizeof(pkti->ip6));
        } else {
            cmsgh->cmsg_level = IPPROTO_IP;
            cmsgh->cmsg_type = PKTINFOS;
            if (pktinfo4 == 1) {
                msg.msg_controllen = CMSG_SPACE(sizeof(pkti->ip));
                cmsgh->cmsg_len = CMSG_LEN(sizeof(pkti->ip));
                memcpy(&pkti->ip.ipi_spec_dst, &saddr.in.sin_addr,
                       sizeof(pkti->ip));
            } else {
                msg.msg_controllen = CMSG_SPACE(sizeof(pkti->in));
                cmsgh->cmsg_len = CMSG_LEN(sizeof(pkti->in));
                memcpy(&pkti->in, &saddr.in.sin_addr, sizeof(pkti->in));
            }
        }
    }
    /* Receive from stdin and send to fd, check if sizes match, if they
     * don't this is an error and we report the amount of data sent */
    if ((databuf = malloc(queue)) == NULL)
        FAIL_EXECUTION(-1, "malloc failure", strerror(errno));
    for (s = r = n = 0, opt = 1; opt > 0 && s == r; r += n, s += opt, n = 0) {
        while ((opt = read(0, databuf, queue - n)) > 0 && (n += opt));
        iov[0] = (struct iovec){ databuf, n };
        if (n > 0)
            opt = sendmsg(fd, &msg, MSG_DONTWAIT);
    }
    n = errno;
    free(databuf);
    if (s >= 0 && !socket_bind_variable(var.size, s, NULL, NULL)) {
        /* Receiving or sending 0 bytes is valid */
        FAIL_EXECUTION(-1, NULL, 0);
    }
    if (opt < 0 || s != r) {
        errno = n;
        FAIL_EXECUTION(-1, "send failure: (%d/%d) %s", s, r, strerror(errno));
    }
    return EXECUTION_SUCCESS;
}

static const int socket_recv
(const intmax_t fd, WORD_LIST *list, vars var) {
    soaddr addr1, addr2, *saddr = &addr1, *daddr = &addr2;
    intmax_t queue, s, r, n, opt, err;
    unsigned char *databuf, varbuf[11][VARSIZE], *p;
    struct timespec timeout = { -1, -1 };
    struct iovec iov[1];
    unsigned char cmsg[cmsglen];
    struct cmsghdr *cmsgh;
    struct msghdr msg;
    struct sock_extended_err *serr;
    pktinfo *pkti;
    memset((char *)saddr, 0, sizeof(soaddr));
    memset((char *)daddr, 0, sizeof(soaddr));
    memset((char *)cmsg, 0, sizeof(struct cmsghdr));

    /* Parse command line unitl all parameters are set, or no more words. */
   if (list && strcasecmp(list->word->word, "-b") == 0) {
        if ((list = list->next) == NULL || list->next != NULL)
            USAGE();
        if (!valid_number(list->word->word, &n) || n < 65536 || n > 2147483647)
            FAIL_EXECUTION(-1, "invalid buffer size: %s", list->word->word);
        if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &n, sizeof(n)) < 0)
            FAIL_EXECUTION(-1, "setsockopt SO_RCVBUF: %s", strerror(errno));
        return EXECUTION_SUCCESS;
   } else for (err = opt = queue = 0; list; list = list->next) {
        /* Fail on extraneous arguments. */
        if (err != 0 && queue != 0 && timeout.tv_sec != -1 && var.base != NULL)
            USAGE();
        /* Nrs are not valid variable names */
        if (strcasecmp(list->word->word, "-e") == 0) {
            err = MSG_ERRQUEUE;
        } else if (strcasecmp(list->word->word, "-m") == 0) {
            n = sizeof(s);
            if (getsockopt(fd, IPPROTO_IP, IP_MTU, &s, (void *)&n) < 0)
                FAIL_EXECUTION(-1, "getsockopt: %s", strerror(errno));
            fprintf(stdout, "%d\n", s);
            return EXECUTION_SUCCESS;
        } else if (!valid_identifier(list->word->word)) {
            if (    (queue == 0 && !valid_number(list->word->word, &opt))
                 || (queue != 0 && ( uconvert(list->word->word, &timeout.tv_sec,
                                              &timeout.tv_nsec, (char **)0) == 0
                                    || timeout.tv_sec < 0
                                    || timeout.tv_nsec < 0)))
                FAIL_EXECUTION(-1, "%s: invalid name, queue or timeout",
                               list->word->word);
            if (queue == 0 && opt > 0)
                queue = opt;
        } else if (var.base == NULL) {
            if (strlen(list->word->word) > VARSIZE - 1 - 6)
                FAIL_EXECUTION(-1, "variable base must be <%d characters",
                               VARSIZE - 6);
            var.base = list->word->word;
        }
    }
    if (queue <= 0)
        FAIL_EXECUTION(-1, "receive size must be > 0", 0);
    if (timeout.tv_nsec > 0)
        timeout.tv_nsec *= 1000;
    if (var.base == NULL)
        var.base = "SOCK";
    for (n = 0; n < 11; n++) {
        p =  n == 0 ? "SADDR" : n == 1 ? "SPORT" : n == 2 ? "DADDR" :
             n == 3 ? "DPORT" : n == 4 ? "RSIZE" : n == 5 ? "EERRNO" :
             n == 6 ? "EORIG" : n == 7 ? "ETYPE" : n == 8 ? "ECODE" :
             n == 9 ? "EOFFEN" : "ESIZE";
        s = sizeof(varbuf[n]);
        if ((r = snprintf(varbuf[n], s, "%s_%s", var.base, p)) < 0 || r > s)
            FAIL_EXECUTION(-1, "buffer error: %s", strerror(errno));
    }
    var.host = varbuf[0], var.port = varbuf[1];
    var.lhost = varbuf[2], var.lport = varbuf[3], var.size = varbuf[4];
    var.eerrno = varbuf[5], var.eorig = varbuf[6];
    var.etype = varbuf[7], var.ecode = varbuf[8];
    var.eoffen = varbuf[9], var.esize = varbuf[10];
    socket_unbind_variables(var);

    /* Check for data with timeout */
    if ((opt = socket_timeout(fd, &timeout, NULL)) < 0)
        FAIL_EXECUTION(-1, NULL, 0);
    if (opt == 0)
        return EXECUTION_SUCCESS;

    /* Receive data from socket and send to stdout check if sizes match
     * don't this is an error and we report the amount of data sent */
    if ((databuf = malloc(queue)) == NULL)
        FAIL_EXECUTION(-1, "malloc failure", strerror(errno));
    iov[0] = (struct iovec){ databuf, queue };
    msg = (struct msghdr){ saddr, sizeof(soaddr), iov, 1, &cmsg, cmsglen, 0 };
    s = r = -1;
    errno = opt = 0;
    if ((r = recvmsg(fd, &msg, err)) >= 0)
        s = write(1, databuf, r);
    opt = errno;
    free(databuf);
    if (opt == 0 && msg.msg_flags & MSG_TRUNC)
        opt = ENOBUFS;

    for (cmsgh = CMSG_FIRSTHDR(&msg); cmsgh; cmsgh = CMSG_NXTHDR(&msg, cmsgh)) {
        /* Get the destination IP in the control message if supported by OS */
        if (pktinfo46 != 0) {
            pkti = (pktinfo *)CMSG_DATA(cmsgh);
            if (    cmsgh->cmsg_level == IPPROTO_IPV6
                 && cmsgh->cmsg_type == PKTINFO6S) {
                daddr->sa.sa_family = AF_INET6;
                memcpy(&daddr->in6.sin6_addr, &pkti->ip6.ipi6_addr,
                       sizeof(struct in6_addr));
            } else if (    pktinfo4 == 1 && cmsgh->cmsg_level == IPPROTO_IP
                        && cmsgh->cmsg_type == PKTINFO) {
                daddr->sa.sa_family = AF_INET;
                memcpy(&daddr->in.sin_addr, &pkti->ip.ipi_addr,
                       sizeof(struct in_addr));
            } else if (    pktinfo4 == 2 && cmsgh->cmsg_level == IPPROTO_IP
                        && cmsgh->cmsg_type == PKTINFO) {
                daddr->sa.sa_family = AF_INET;
                memcpy(&daddr->in.sin_addr, &pkti->in, sizeof(struct in_addr));
            }
        }
        /* When receiving error queue, set error variables */
        if (    iperr != 0 && err == MSG_ERRQUEUE
             && (cmsgh->cmsg_type == IPERR || cmsgh->cmsg_type == IPERR6)) {
            serr = (struct sock_extended_err *)CMSG_DATA(cmsgh);
            if (   !socket_bind_variable(var.eerrno, serr->ee_errno, NULL, NULL)
                 || !socket_bind_variable(var.eorig, serr->ee_origin, NULL,
                                          NULL)
                 || !socket_bind_variable(var.etype, serr->ee_type, NULL, NULL)
                 || !socket_bind_variable(var.ecode, serr->ee_code, NULL, NULL)
                 || !socket_bind_variable(var.eoffen, -1,
                                          (void *)SO_EE_OFFENDER(serr), NULL)
                 || (    serr->ee_info != 0
                      && !socket_bind_variable(var.esize, serr->ee_info, NULL,
                                             NULL)))
                FAIL_EXECUTION(-1, NULL, 0);
        }
    }

    /* Set variables, rhost, rport and lhost when data was received
     * and size when data was sent to stdout. When receiving error queue
     * the source and destination address are swapped, so that the original
     * source and destination address match the packet that was sent. */
    if (err == MSG_ERRQUEUE)
        saddr = &addr2, daddr = &addr1;
    if (s >= 0 && !socket_bind_variable(var.size, s, NULL, NULL))
        /* Receiving or sending 0 bytes is valid */
        FAIL_EXECUTION(-1, NULL, 0);
    if (r >= 0 && saddr->sa.sa_family != 0) {  /* 0 = STREAM socket */
        if (!socket_bind_variable(var.host, -1, saddr, NULL))
            FAIL_EXECUTION(-1, NULL, 0);
        n = saddr->sa.sa_family == AF_INET  ? saddr->in.sin_port :
                                   AF_INET6 ? saddr->in6.sin6_port : 0;
        if (n != 0 && !socket_bind_variable(var.port, ntohs(n), NULL, NULL))
            FAIL_EXECUTION(-1, NULL, 0);
        if (    daddr->sa.sa_family != 0
             && !socket_bind_variable(var.lhost, -1, daddr, NULL))
            FAIL_EXECUTION(-1, NULL, 0);
        n = daddr->sa.sa_family == AF_INET  ? daddr->in.sin_port :
                                   AF_INET6 ? daddr->in6.sin6_port : 0;
        if (n != 0 && !socket_bind_variable(var.lport, ntohs(n), NULL, NULL))
            FAIL_EXECUTION(-1, NULL, 0);
    }
    if (r < 0 || s < 0 || s != r || opt != 0) {
        errno = opt;
        FAIL_EXECUTION(-1, "receive failure: %s", strerror(opt));
    }
    return EXECUTION_SUCCESS;
}

static char *const socket_doc[] = {
    "Provides an interface for creating and using POSIX sockets",
    "The result socket handle is written to the variable denoted by varname.",
    "The default varname is SOCK_FD. Parameters are case insensitive.",
    "AF_UNIX, AF_INET, AF_INET6 (case insensitive) cannot be used as varname.",
    "The default queue (backlog) size is 5.",
    "",
    "socket -s and socket -r.",
    "Sends to or receives data from a socket. This mostly applies to SOCK_DGRAM",
    "sockets. read cannot be called on such sockets, as read() calls block.",
    "Data is read standard input and written to standard output.",
    "",
    "Receiving data requires a queue size to be set. By default no timeout is",
    "used. Three variables will be set, SOCK_RHOST, SOCK_RPORT and SOCK_RSIZE.",
    "If the socket is AF_UNIX SOCK_RHOST will be set to the remote socket path",
    "and SOCK_RPORT will not be set. SOCK_RSIZE is set to the size of the",
    "received datagram. Only a queue size is mandatory, the order is flexible,",
    "the first number will be the queue size, the second number the timeout,",
    "variable names are in order: RHOST, RPORT, RSIZE.",
    "On success 0 is returned and the variables will be set. On failure 1 is",
    "returned (or the OS value of EADDRINUSE).",
    "On timeout success is returned and no variables will be set",
    "",
    "Sending data requires a remote address and port, or remote socket path",
    "in case of an AF_UNIX socket. When sending data to an AF_UNIX socket, if",
    "a local path is not specified, the peer socket will not have a remote",
    "path and will not be able to reply. Message or FD passing is not",
    "implemented, neither are abstract sockets. A maximum [size] of data",
    "to be sent can optionally be specified. The size of the data sent to the",
    "socket is saved in variable svarsize (SOCK_SSIZE). When sending data to",
    "SOCK_STREAM sockets the remote address is ignored by the OS.",
    "",
    "NOTES: - On the client side of SOCK_DGRAM you can use normal IO redirection",
    "         like 'echo Hello >&fd', as the socket is fully connected.",
    "       - On the server side of SOCK_DGRAM sockets 'socket -s' with remote",
    "         adress is necesary. The OS needs an address to send to. >&fd will",
    "         yield an endpoint not connected error.",
    "       - Doing something like 'echo Hello | socket -s fd ...' is not useful.",
    "         Due to bash semantics this will be run in a subshell and the",
    "         SOCK_RSIZE variable will not be set in the calling shell. You have",
    "         to use here documents or IO redirection:",
    "           'socket -s fd ... <<<\"Hello\"'",
    "           'socket -s fd ... <&fd'",
    "           'socket -s fd ... < <(echo Hello)",
    "       - Receving on SOCK_STREAM sockets will not set RHOST and RPORT.",
    "       - Sending on SOCK_STREAM sockets will ignore the remote address",
    "       - SOCK_STREAM sockets can be used with normal bash read and write.",
    "         reliable delivery is assured by the OS, so checking send or",
    "         receive sizes is not necessary.",
    "         and port. The data is always sent to the connected endpoint.",
    "       - On SOCK_DGRAM sockets you can never send more then the maximum",
    "         datagram size, which is interface mtu - ip headers - udp header",
    "         for udp datagrams. For UNIX sockets the size depends on OS and",
    "         what is configured.",
    "       - When receiving data on SOCK_DGRAM sockets the entire datagram",
    "         must be read, the buffer must match the maximum datgram size",
    "         accordingly. Any data not read from a datagram will be discarded.",
    "",
    "Examples:",
    "         socket SOCKFD AF_UNIX SOCK_STREAM local /tmp/sock",
    "           Opens a local UNIX STREAM socket in /tmp/sock.",
    "           SOCKFD will be set to the fd",
    "         socket SOCKFD af_inet6 sock_dgram local :: 12345",
    "           Opens a ipv6 udp socket on all ip addresses port 12345.",
    "           SOCKFD will be set to the fd",
    "         socket AF_INET sock_stream local 1.2.3.4 80 100",
    "           Opens ipv4 tcp socket on ip 1.2.3.4 port 80 queue size 100.",
    "           SOCK_FD will be set to the fd.",
    "         socket AF_INET6 SOCK_STREAM PEER 2a00:400::1 443",
    "           Connects to ipv6 adress 2a00:400::1 on tcp port 443.",
    "           SOCK_FD will be set to te fd.",
    "         socket fd af_unix sock_dgram PEER /tmp/sock",
    "           Connects to UNIX datagram socket /tmp/sock.",
    "           fd will be set to the fd",
    "         socket -s 3 1.2.3.4 1234",
    "           Sends data from stdin on the socket fd 3 to 1.2.3.4:1234.",
    "         socket -r 3 1000 10 RHOST RPORT RSIZE",
    "           Receives max 1000 bytes of data on socket fd 3, RHOST",
    "           contains the remote ip / path, RPORT the remote port and",
    "           RSIZE the recieved size. Timeout of 10 seconds.",
    "         socket -r 3 RHOST 1000 RPORT 10 RSIZE",
    "           Same as above.a,"
    "         socket -r 3 RHOST 1000 10.1",
    "           In this case the default of SOCK_RPORT and SOCK_RSIZE",
    "           will be used and a timeout of 10.1 seconds.",
    NULL
};

struct builtin socket_struct = {
    .name = "socket",
    .function = socket_builtin,
    .flags = BUILTIN_ENABLED,
    .long_doc = socket_doc,
    .short_doc =
    "socket [varname] SOCK_STREAM|SOCK_DGRAM local|peer ADDRESS [port] [queue]\n"
    "               socket -s fd [[peer addr [port]] [[local [addr]] [varbase] [size]\n"
    "               socket -r fd size [timeout] [varbase]",
    .handle = NULL
};
