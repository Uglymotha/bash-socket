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

#ifndef __SOCKET_BUILTIN_H
#define __SOCKET_BUILTIN_H 1

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "bashtypes.h"
#include <errno.h>
#include <time.h>
#include <limits.h>
#include "typemax.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <poll.h>
#include "loadables.h"


#define USAGE(x) do { \
    builtin_usage();  \
    return EX_USAGE;  \
    } while (0)

#define SOCKET_FAIL_EXECUTION SOCKET_INTERNAL_FAIL_EXECUTION
#define ACCEPT_FAIL_EXECUTION SOCKET_INTERNAL_FAIL_EXECUTION
#define POLL_FAIL_EXECUTION SOCKET_INTERNAL_FAIL_EXECUTION
#define SOCKET_INTERNAL_FAIL_EXECUTION(fd1, fd2, msg, ...) do { \
    int err = errno;                                                         \
    if (msg) builtin_error(msg, __VA_ARGS__);                                \
    if (fd1 >= 0) close (fd1);                                               \
    if (fd2 >= 0) close (fd2);                                               \
    return err != 0 ? err : EXECUTION_FAILURE; \
    } while (0)

#ifdef IPV6_RECVPKTINFO
  #define PKTINFO6 IPV6_RECVPKTINFO
  #define PKTINFO6S IPV6_PKTINFO
  static const int pktinfo6 = 1;
#elif IPV6_PKTINFO
  #define PKTINFO6 IPV6_PKTINFO
  #define PKTINFO6S IPV6_PKTINFO
  static const int pktinfo6 = 1;
#endif
#ifdef IP_PKTINFO
  #define PKTINFO IP_PKTINFO
  #define PKTINFOS IP_PKTINFO
  static const int pktinfo4 = 1;
#elif IP_RCVDSTADDR
  #ifdef IP_SENDSSRCADDR
    #define PKTINFO IP_RECVDSTADDR
    #define PKTINFOS IP_SENDSRCADDR
    static const int pktinfo4 = 2;
  #endif
#endif
#ifndef IP_PKTINFO
  struct in_pktinfo { int ipi_spec_dst, ipi_addr; };
#endif
#ifndef IPV6_PKTINFO
  struct in6_pktinfo { int ipi6_addr; };
#endif
#ifndef PKTINFO
  #define PKTINFO -1
  #define PKTINFOS -1
  static const int pktinfo4 = 0;
#endif
#ifndef PKTINFO6
  #define PKTINFO6 -1
  #define PKTINFO6S -1
  static const int pktinfo6 = 0;
#endif
#ifdef IP_RECVERR
  #ifdef IPV6_RECVERR
    #include <linux/errqueue.h>
    #define IPERR IP_RECVERR
    #define IPERR6 IPV6_RECVERR
    static const int iperr = 1;
  #endif
#endif
#ifndef IPERR
  #undef MSQ_ERRQUEUE
  #define MSG_ERRQUEUE -1
  #define SO_EE_OFFENDER(x)  NULL
  #define IPERR -1
  #define IPERR6 -1
  struct sock_extended_err { int ee_errno, ee_origin, ee_type,
                                 ee_code, ee_info; };
  static const int iperr = 0;
#endif
#ifdef IP_MTU_DISCOVER
  #define IPMTU IP_MTU_DISCOVER
#elif IP_DONTFRAG
  #define IPMTU IP_DONTFRAG
#else
  #define IPMTU -1
#endif
#ifdef IPV6_MTU_DISCOVER
  #define IPMTU6 IPV6_MTU_DISCOVER
#elif IPV6_DONTFRAG
  #define IPMTU6 IPV6_DONTFRAG
#else
  #define IPMTU6 -1
#endif

#define VARSIZE 32
#define SA struct sockaddr *
static const int pktinfo46 = (pktinfo4 && pktinfo6);

typedef union { struct sockaddr     sa;
                struct sockaddr_in  in;
                struct sockaddr_in6 in6;
                struct sockaddr_un  un; } soaddr;
typedef struct { char *base, *fd, *sock, *host, *port, *lhost, *lport, *size;
                 char *eerrno, *eorig, *etype, *ecode, *eoffen, *esize;
                 char *readfd, *writefd, *prifd, *errfd, *hupfd, *invfd; } vars;

typedef union { struct in_pktinfo        ip;
                struct in6_pktinfo       ip6;
                struct in_addr           in; } pktinfo;

static const size_t cmsglen = CMSG_SPACE(sizeof(pktinfo))
                            + CMSG_SPACE(sizeof(struct sock_extended_err)
                                         + sizeof(soaddr));

#define socket_bind_variable socket_bind_variable_and_check
#define accept_bind_variable socket_bind_variable_and_check
#define poll_bind_variable socket_bind_variable_and_check
extern const int socket_bind_variable_and_check
(const char *const, const int, const soaddr *, char *);

#define socket_unbind_variables socket_unbind_variables_and_check
#define accept_unbind_variables socket_unbind_variables_and_check
#define poll_unbind_variables socket_unbind_variables_and_check
extern void socket_unbind_variables_and_check(const vars);

#define socket_parse_address socket_internal_parse_address
#define accept_parse_address socket_internal_parse_address
extern const int socket_internal_parse_address
(WORD_LIST **, soaddr *, char *, const int);

#define socket_timeout socket_internal_timeout
#define accept_timeout socket_internal_timeout
extern const int socket_internal_timeout
(const int, struct timespec *const, const char *const);

#endif  /* __SOCKET_BUILTIN_H */
