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

const int socket_bind_variable_and_check
(const char *const varname, const int intval, const soaddr *client,
 char *str) {
    /* Set varname to either intval or client IP string */
    SHELL_VAR *v;
    char ibuf[PATH_MAX], *p;
    void *addr;
    int opt = 1;

    if (varname == NULL || (intval < 0
                            && (client == NULL
                                || client->sa.sa_family == AF_UNSPEC)
                            && str == NULL))
        return (1);

    if (client != NULL && client->sa.sa_family == AF_INET)
        addr = (void *)&client->in.sin_addr;
    else if (client != NULL)
        addr = (void *)&client->in6.sin6_addr;

    /* If client is not AF_INET(6) var value = "-" */
    if (intval >= 0)
        p = fmtulong(intval, 10, ibuf, sizeof (ibuf), 0);
    else if (client != NULL) {
        if (client->sa.sa_family == AF_INET || client->sa.sa_family == AF_INET6)
            p = (char *)inet_ntop(client->sa.sa_family, addr, ibuf,
                                  sizeof(ibuf));
        else if ((p = ibuf)) {
            opt = snprintf(ibuf, sizeof(ibuf), "%s", client->un.sun_path);
            if (opt <= 0 || opt > sizeof(ibuf))
                builtin_error("buffer error: %s", strerror(errno));
        }
    } else
        p = str;
    if (opt > 0 && p != NULL)
        v = builtin_bind_variable((char *)varname, p, 0);
    if (opt < 0 || p == NULL || v == 0 || readonly_p (v) || noassign_p (v)) {
        if (v != 0)
            unbind_variable(varname);
        v = 0;
        builtin_error("%s: cannot set variable: %s", varname, strerror(errno));
    }
    return (v != 0);
}

void socket_unbind_variables_and_check(const vars var) {
    /* Reset all shell variables, var.sock is only set with -n and not -f */
    if (var.fd)
        unbind_variable(var.fd);
    if (var.sock)
        unbind_variable(var.sock);
    if (var.host)
        unbind_variable(var.host);
    if (var.port)
        unbind_variable(var.port);
    if (var.lhost)
        unbind_variable(var.lhost);
    if (var.lport)
        unbind_variable(var.lport);
    if (var.size)
        unbind_variable(var.size);
    if (var.eerrno)
        unbind_variable(var.eerrno);
    if (var.eorig)
        unbind_variable(var.eorig);
    if (var.etype)
        unbind_variable(var.etype);
    if (var.ecode)
        unbind_variable(var.ecode);
    if (var.eoffen)
        unbind_variable(var.eoffen);
    if (var.esize)
        unbind_variable(var.esize);
    if (var.readfd)
        unbind_variable(var.readfd);
    if (var.writefd)
        unbind_variable(var.writefd);
    if (var.prifd)
        unbind_variable(var.prifd);
    if (var.errfd)
        unbind_variable(var.errfd);
    if (var.hupfd)
        unbind_variable(var.hupfd);
    if (var.invfd)
        unbind_variable(var.invfd);
}

const int socket_internal_parse_address
(WORD_LIST **list, soaddr *addr, char *word, const int port) {
    struct addrinfo *raddrs, *a, hints;
    intmax_t n, f;
    char *p = word != NULL ? word : (*list)->word->word;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = -1;

    if (*list == NULL)
        return -1;

    /* If -4 or -6 was specified resolve the right ip version */
    if (strcmp(p, "-4") == 0)
        hints.ai_family = AF_INET;
    else if (strcmp(p, "-6") == 0)
        hints.ai_family = AF_INET6;
    if (hints.ai_family > 0 && word == NULL)
        *list = (*list)->next;
    if (hints.ai_family > 0 || word == NULL)
        p = (*list)->word->word;
    if (hints.ai_family < 0)
        hints.ai_family = AF_UNSPEC;

    /* IPv4? -> IPv6? -> AF_UNIX? -> resolve */
    addr->sa.sa_family = AF_INET;
    if (inet_pton(AF_INET, p, &addr->in.sin_addr) != 1)
        addr->sa.sa_family = AF_INET6;
    if (addr->sa.sa_family == AF_INET6
        && inet_pton(AF_INET6, p, &addr->in6.sin6_addr) != 1)
        addr->sa.sa_family = 0;
    if (addr->sa.sa_family == 0 && strstr(p, "/") != NULL) {
        if ((n = snprintf(addr->un.sun_path, 107, "%s", p)) > 0 && n <= 107)
            addr->sa.sa_family = AF_UNIX;
    } else if (addr->sa.sa_family == 0) {
        a = raddrs = NULL;
        n = getaddrinfo(p, NULL, &hints, &raddrs);
        if (n == 0 && raddrs != NULL)
        for (a = raddrs; a; a = a->ai_next)
            if ((f = a->ai_family) && (f == AF_INET || f == AF_INET6)
                && (hints.ai_family == AF_UNSPEC || hints.ai_family == f))
                break;
        if (a) {
            addr->sa.sa_family = a->ai_family;
            if (a->ai_family == AF_INET)
                memcpy(&addr->in, a->ai_addr, sizeof(addr->in));
            else
                memcpy(&addr->in6, a->ai_addr, sizeof(addr->in6));
        } else {
            builtin_error("unable to resolve %s: %s",
                          p, gai_strerror(n));
        }
        if (raddrs)
            freeaddrinfo(raddrs);
        if (n != 0)
            return n < 0 ? n : -n;
    }
    if (addr->sa.sa_family == 0) {
        builtin_error("invalid socket address: %s", p);
        return -1;
    }
    /* For inet sockets a port is also required */
    if (port > 0 && addr->sa.sa_family != AF_UNIX) {
        if (addr->sa.sa_family == AF_INET)
            addr->in.sin_port = 0;
        else
            addr->in6.sin6_port = 0;
        if (   (word == NULL || hints.ai_family != AF_UNSPEC)
            && (*list = (*list)->next) == NULL) {
            if (port > 1)
                return 1;
            builtin_error("port number is required");
            return -1;
        }
        if (!valid_number((*list)->word->word, &n)
            || n < 0 || n > (unsigned short)-1) {
            if (port > 1)
                return 1;
            builtin_error("invalid port: %s", (*list)->word->word);
            return -1;
        }
        if (addr->sa.sa_family == AF_INET)
            addr->in.sin_port = (unsigned short)htons(n);
        else
            addr->in6.sin6_port = (unsigned short)htons(n);
    }
    *list = (*list)->next;
    return 1;
}

const int socket_internal_timeout
(const int fd, struct timespec *const timeout, const char *const var) {
    struct pollfd pollfd;
    int res = 1;

    /* Skip timeout if -t not set, set SOCK_FD when socket is opened with -n */
    if (timeout->tv_sec >= 0 && timeout->tv_nsec >= 0) {
        pollfd = (struct pollfd){ fd, POLLIN, 0 };
        res = ppoll(&pollfd, 1, timeout, NULL);
    }
    if (res < 0)
        builtin_error("ppoll failure: %s", strerror(errno));
    else if (res == 0) {
        if (pollfd.revents & POLLHUP) {
            builtin_error("ppoll failure: %s", strerror(ECONNRESET));
            res = -1;
        } else if (pollfd.revents & POLLNVAL) {
            builtin_error("ppoll failure: %s", strerror(EINVAL));
            res = -1;
        }
    }
    if (res >= 0 && !socket_bind_variable_and_check(var, fd, NULL, NULL))
        res = -1;
    return res;
}
