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

#define FAIL_EXECUTION ACCEPT_FAIL_EXECUTION

int accept_builtin (WORD_LIST *list) {
    intmax_t iport, bl = 1;
    int opt, portfd = 0, noclose = 0;
    char *tmoutarg = NULL, *bindaddr = NULL, *queue = NULL;
    vars var;
    int servsock = -1, clisock = -1;
    soaddr server, client;
    socklen_t clientlen = sizeof(client);
    struct timespec timeout = { -1, -1 };
    struct linger linger = { 0, 0 };

    /* Parse command line, validate input and variables */
    memset ((char *)&var, 0, sizeof (var));
    memset ((char *)&server, 0, sizeof (soaddr));
    memset ((char *)&client, 0, sizeof (soaddr));
    reset_internal_getopt ();
    while ((opt = internal_getopt(list, "b:fnp:q:r:s:t:v:")) != -1) {
        if (opt == 'r' || opt == 'p' || opt == 'v' || opt == 's')
            if (    !valid_identifier(list_optarg)
                 && !valid_array_reference(list_optarg, 0))
                FAIL_EXECUTION(-1, -1, "%s: invalid variable name", list_optarg);
        switch (opt) {
        case 'b':
            bindaddr = list_optarg;
            break;
        case 'r':
            var.host = list_optarg;
           break;
        case 't':
            tmoutarg = list_optarg;
            break;
        case 'v':
            var.fd = list_optarg;
            break;
        case 'p':
            var.port = list_optarg;
            break;
        case 'q':
            queue = list_optarg;
            break;
        case 's':
            var.sock = list_optarg;
            break;
        case 'f':
            portfd = 1;  // Fall Through
        case 'n':
            noclose = 1;
            break;
        CASE_HELPOPT;
        default:
            USAGE();
        }
    }
    if ((list = loptend) == NULL)
        USAGE();
    if (tmoutarg) {
        opt = uconvert(tmoutarg, &timeout.tv_sec, &timeout.tv_nsec, (char **)0);
        if (opt == 0 || timeout.tv_sec < 0 || timeout.tv_nsec < 0)
            FAIL_EXECUTION(-1, -1, "%s: invalid timeout specification", tmoutarg);
        timeout.tv_nsec *= 1000;
    }
    if (queue && (!valid_number(queue, &bl) || bl < 1 || bl > TYPE_MAXIMUM(int)))
        FAIL_EXECUTION(-1, -1, "%s: invalid queue size", queue);
    /* -f (portfd = 1) port = fd */
    if (portfd == 1 && (    !valid_number(list->word->word, &iport)
                         || iport < 0 || iport > TYPE_MAXIMUM (int)))
        FAIL_EXECUTION(-1, -1, "%s: invalid socket fd", list->word->word);
    if (    portfd == 0
         && (errno = accept_parse_address(&list, &server, bindaddr, 1)) < 0)
        FAIL_EXECUTION(-1, -1, NULL, 0);
    if (var.fd == NULL)
        var.fd = "ACCEPT_FD";
    if (var.sock == NULL && noclose == 1 && portfd != 1)
        var.sock = "SOCK_FD";
    accept_unbind_variables(var);

    /* Accept the fd (-f). On timeout return success with ACCEPT_FD NOT set.
     * Never set SOCK_FD or close the fd */
    if (portfd == 1 && (opt = accept_timeout(iport, &timeout, NULL)) < 0)
        FAIL_EXECUTION(-1, -1, NULL, 0);
    if (portfd == 1 && opt > 0) {
        if ((clisock = accept(iport, (SA)&client, &clientlen)) < 0)
            FAIL_EXECUTION(-1, -1, "accept failure: %s", strerror(errno));
        else if (    !accept_bind_variable
                  ||  (var.port, htons(client.in.sin_port), NULL, NULL)
                  || !accept_bind_variable(var.host, -1, &client, NULL)
                  || !accept_bind_variable(var.fd, clisock, NULL, NULL))
            FAIL_EXECUTION(clisock, -1, NULL, 0);
    }
    if (portfd == 1)
        return EXECUTION_SUCCESS;

    /* Open bind and listen to socket, on failure close fd and do not set vars */
    if ((servsock = socket(server.sa.sa_family, SOCK_STREAM, IPPROTO_IP)) < 0)
        FAIL_EXECUTION(-1, -1, "socket failure: %s", strerror(errno));
    opt = 1;
    if ( setsockopt
         (servsock, SOL_SOCKET, SO_REUSEADDR, (void *)&opt, sizeof(opt)) < 0
         ||
         setsockopt
         (servsock, SOL_SOCKET, SO_LINGER, (void *)&linger, sizeof(linger)) < 0)
        FAIL_EXECUTION(servsock, -1, "setsockopt failure: %s", strerror(errno));
    if (bind(servsock, (SA)&server, sizeof(server)) < 0)
        FAIL_EXECUTION(servsock, -1, "bind failure: %s", strerror(errno));
    if (listen (servsock, bl) < 0)
        FAIL_EXECUTION(servsock, -1, "listen failure: %s", strerror(errno));

    /* On timeout with -n return success, failure otherwise */
    if ((opt = accept_timeout(servsock, &timeout, var.sock)) < 0)
        FAIL_EXECUTION(servsock, -1, NULL, 0);
    if (opt == 0 && noclose != 0)
        return EXECUTION_SUCCESS;

    /* Accept connection and set appropriate variables. On error close fds
     * and clear all variables */
    if ((clisock = accept(servsock, (SA)&client, &clientlen)) < 0)
          builtin_error("accept failure: %s", strerror(errno));
    if (    clisock < 0
         || !accept_bind_variable(var.fd, clisock, NULL, NULL)
         || !accept_bind_variable(var.host, -1, &client, NULL)
         || !accept_bind_variable
             (var.port, htons(client.in.sin_port), NULL, NULL))
        FAIL_EXECUTION(servsock, clisock, NULL, 0);

    if (noclose == 0)
        close (servsock);
    return EXECUTION_SUCCESS;
}

char *accept_doc[] = {
    "Accept a socket or a network connection on a specified port.",
    "This builtin allows a bash script to act as a TCP/IP server.",
    "",
    "Options, if supplied, have the following meanings:",
    "    -b address    Use ADDRESS as the IP address to listen on; the",
    "                  default is INADDR_ANY. For AF_UNIX sockets a path",
    "                  can be specified",
    "    -t timeout    Wait TIMEOUT seconds for a connection. TIMEOUT may",
    "                  be a decimal number including a fractional portion",
    "    -v varname    Store the numeric file descriptor of the connected",
    "                  socket into VARNAME. The default VARNAME is ACCEPT_FD",
    "    -s sockname   Store the numeric file descriptor of the listening",
    "                  socket into SOCKNAME. The default VARNAME is SOCK_FD,",
    "                  and is only set when -n is specified and -f is not.",
    "    -r rhost      Store the IP address of the remote host into the shell",
    "                  variable RHOST, in dotted-decimal notation",
    "    -p rport      Store the port nr of the remote host into the shell",
    "                  variable RPORT",
    "    -q size       Specify the queue size of connections waiting to be",
    "                  accepted on the listening socket.",
    "    -n            Do not close the the listening socket and set SOCK_FD",
    "    -f            PORT is treated as a file descriptor of a STREAM",
    "                  socket, -s -b and -q are ignored -n is implied",
    "",
    "If successful, the shell variable ACCEPT_FD, or the variable named by the",
    "-v option, will be set to the fd of the connected socket, suitable for",
    "use as 'read -u$ACCEPT_FD'. RHOST, if supplied, will hold the IP address",
    "of the remote client, RPORT the remote port. The return status is 0.",
    "",
    "If TIMEOUT is reached and no connection was accepted, the return status",
    "is 0, with -n SOCKNAME will be set to the listening FD so accept can",
    "be called again with -f. ACCEPT_FD will not be set.",
    "",
    "With -f, if TIMEOUT is reached and no connection can be accepted, the",
    "return status is 0, SOCK_FD and ACCEPT_FD will not be set.",
    "",
    "On failure, the return status is 1 and all variables will be unset.",
    "",
    "With -n the server socket fd will not be closed after accept returns",
    "and SOCKNAME will be set to the listening fd. The fd can be polled",
    "with accept -t0 -f fd. If no connection is waiting to be accepted",
    "success will be returned with ACCEPT_FD unset.",
    "",
    "Examples:",
    "          accept -t 0 -n -s SOCK 1234",
    "            Creates a listening socket on all ipv4 addresses, port 1234,",
    "            immediately returns success and the socket fd in SOCK_FD.",
    "          accept -t 2 -f ${SOCK_FD}",
    "            Waits 2 seconds for a client connection. If a connection is",
    "            made success is returned and ACCEPT_FD will contain the FD",
    "            of the connection. On timeout success is returned and",
    "            ACCEPT_FD will be unset.",
    "          accept -t10 -n -b /tmp/sock",
    "            Creates an AF_UNIX socket bound to /tmp/sock. Waits 10 seconds",
    "            for a connection and saves the fd in SOCK_FD",
    "          accept -v FD -b 127.0.0.1 1234",
    "            Creates a one time listening server on localhost port 1234",
    "            Waits indefinitely for a connection, returns success and",
    "            the connection fd in FD, the listening socket will be closed.",
    "          accept -s SOCKFD -b ::1 -n 1234",
    "            Creates a one time listening server on localhost port 1234",
    "            Waits indefinitely for a connection, returns success with",
    "            the connection fd in ACCEPT_FD and the socket fd in SOCKFD.",
    "          accept -s SOCK -b :: -n -r RHOST -p RPORT -q 100 1234",
    "            Creates a listening socket on all ipv4/ipv6 addresses and",
    "            waits for connections indefinitely. If a connection is",
    "            accpted succes is returned, ACCEPT_FD will contain the fd",
    "            of the connection and SOCK_FD the fd of the listening socket,",
    "            RHOST the remote IP and RPORT the remote port.100 connections",
    "            can be queued waiting to be accepted.",
    "          accept -f -r RHOST -p RPORT 3",
    "            Waits indefinitely for connections on listening socket fd 3",
    "            If a connection is accepted success is returned ACCEPT_FD",
    "            contains the fd of the connection, RHOST to the remote IP",
    "            address and RPORT the remote port. If socket fd is AF_UNIX",
    "            RHOST will be set to the remote path and RPORT is undefined.",
    (char *) NULL
};

struct builtin accept_struct = {
    "accept",               /* builtin name */
    accept_builtin,         /* function implementing the builtin */
    BUILTIN_ENABLED,        /* initial flags for builtin */
    accept_doc,             /* array of long documentation strings. */
    "accept [-b address] [-t timeout] [-v varname] [-s sockname] [-r rhost] [-p rport] [-q size] [-n] [-f] [port]", /* usage synopsis; becomes short_doc */
    0                       /* reserved for internal use */
};
