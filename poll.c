/* poll - poll a set of file descriptors for i/o */
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

#define CLEANUP do {                                           \
    for (l = lmin; l < lmax; free(fd[l]), l++);                \
    for (l = lmin; l < lmax; free(buf[l]), l++);              \
    free(pollfd); } while (0)
#define EXIT_OK(x) do {                                        \
    CLEANUP;                                                   \
    return EXECUTION_SUCCESS; } while (0)
#define FAIL_EXECUTION(msg, ...) do {                          \
    CLEANUP;                                                   \
    POLL_FAIL_EXECUTION(-1, -1, msg, __VA_ARGS__); } while (0)

enum lists { lmin = 0, lread = 0, lwrite, lpri,
             lerr, lhup, linval, lmax };

static const int poll_builtin(WORD_LIST *list) {
    vars var;
    intmax_t c[lmax], res, e, i, n, l, s;
    intmax_t nfd[lerr] = { 0, 0, 0 }, *fd[lmax];
    struct timespec timeout, *to = NULL;
    char *buf[lmax], varbuf[6][VARSIZE], *name;
    struct pollfd *pollfd = NULL;
    SHELL_VAR *shvar;
    memset((char *)&var, 0, sizeof(var));
    memset(fd, 0, lmax * sizeof(void *));
    memset(buf, 0, lmax * sizeof(void *));

    if (list == NULL || strcasecmp(list->word->word, "-h") == 0)
        USAGE();
    if (strcasecmp(list->word->word, "-v") == 0) {
        if ((list = list->next) == NULL || !valid_identifier(list->word->word))
            var.base = "POLL";
        else if (strlen(list->word->word) > VARSIZE - 6 - 1)
            FAIL_EXECUTION("variable base must be <%d characters", VARSIZE - 6);
        else {
            var.base = list->word->word;
            if ((list = list->next) == NULL)
                USAGE();
        }
    }
    if (!uconvert(list->word->word, &timeout.tv_sec, &timeout.tv_nsec, NULL))
        FAIL_EXECUTION("%s: invalid timeout", list->word->word);
    if (timeout.tv_sec >= 0 && timeout.tv_nsec >= 0) {
        timeout.tv_nsec *= 1000;
        to = &timeout;
    }
    for (l = i = 0; (var.base != NULL && l < lmax) || (list = list->next);
         nfd[l] += (var.base == NULL && n >= nfd[l]), l += (var.base != NULL)) {
        if (var.base == NULL)
            switch (*(char *)list->word->word) {
            case 'r': case 'R':
                l = lread, list = list->next; break;
            case 'w': case 'W':
                l = lwrite, list = list->next; break;
            case 'p': case 'P':
                l = lpri, list = list->next; break;
            }
        if ((nfd[l] % 64) == 0) {
            s = (nfd[l] + 64) * sizeof(intmax_t);
            if ((fd[l] = realloc(fd[l], s)) == NULL)
                FAIL_EXECUTION("realloc failure", strerror(errno));
        }
        if (var.base == NULL && !valid_number(list->word->word, &fd[l][nfd[l]]))
            FAIL_EXECUTION("invalid fd: %s", list->word->word);
        else if (var.base == NULL)
            /* Skip duplicates */
            for (n = 0; n < nfd[l] && fd[l][n] != fd[l][nfd[l]]; n++);
        else if (var.base != NULL) {
            name = l == lread ? "READFD" : l == lwrite ? "WRITEFD" :
                   l == lpri  ? "PRIFD"  : l == lerr   ? "ERRFD" :
                   l == lhup  ? "HUPFD"  : "INVFD";
            if ( (i = snprintf(varbuf[l], VARSIZE, "%s_%s", var.base, name)) < 0
                 || i > VARSIZE)
                FAIL_EXECUTION("buffer error", 0);
            if (l >= lerr)
                continue;
            if ((shvar = find_variable(varbuf[l])) != NULL) {
                name = strtok(shvar->value, " ");
                while (name != NULL && valid_number(name, &fd[l][nfd[l]])) {
                    for (n = 0; n < nfd[l] && fd[l][n] != fd[l][nfd[l]]; n++);
                    nfd[l] += (n >= nfd[l]);
                    name = strtok(NULL, " ");
                }
                if (name != NULL)
                    FAIL_EXECUTION("invalid fd: %s", name);
            }
        }
    }
    if (var.base != NULL) {
        var.readfd = varbuf[lread];
        var.writefd = varbuf[lwrite];
        var.prifd = varbuf[lpri];
        var.errfd = varbuf[lerr];
        var.hupfd = varbuf[lhup];
        var.invfd = varbuf[linval];
    }
    poll_unbind_variables(var);

    if (nfd[0] + nfd[1] + nfd[2] == 0)
        FAIL_EXECUTION("you must specify at least one fd", 0);
    for (n = 0, l = e = lmin; l < lerr; l++)
    for (i = s = 0; i < nfd[l]; i++, n += (s == n), e = l) {
        /* Create and fill pollfds for each list of fds parsed from cli */
        if (n == 0 || ((n % 64) == 0 && e == l)) {
            /* Alloc per 64 pollfds. Special case: when just alloced and then
             * immediately the list is switched we must not realloc again */
            s = (n + 64) * sizeof(struct pollfd);
            if ((pollfd = realloc(pollfd, s)) == NULL) 
                FAIL_EXECUTION("realloc failure: %s",strerror(errno));
            for (e = n; e < n + 64; e++)
                pollfd[e] = (struct pollfd){ -1, 0, 0 };
        }
        for (s = 0; s < n && pollfd[s].fd != fd[l][i]; s++);
        pollfd[s].fd = fd[l][i];
        pollfd[s].events |= (l == lread  ? POLLIN  :
                             l == lwrite ? POLLOUT : POLLPRI);
    }

    if ((res = ppoll(pollfd, n, to, NULL)) < 0)
        FAIL_EXECUTION("poll failure: %s", strerror(errno));
    if (res == 0)
        EXIT_OK();
    for (i = 0; i < n; i++)
    for (l = lmin, s = n * 24; l < lmax; l++) {
        /* Go over all pollfds and lists. Only output when fds are ready */
        if (buf[l] == NULL) {
            /* 64b int < 24 chars, alloc buffers for each output list. */
            if ((buf[l] = malloc(s)) == NULL)
                FAIL_EXECUTION("malloc failure", strerror(errno));
            buf[l][0] = l == lread ? 'R' : l == lwrite ? 'W' :
                        l == lpri  ? 'P' : l == lerr   ? 'E' :
                        l == lhup  ? 'H' : 'I';
            buf[l][1] = '\0';
            c[l] = 1;
        }
        /* Check for events and output fd to respective buffer */
        e = l == lread ? POLLIN  : l == lwrite ? POLLOUT :
            l == lpri  ? POLLPRI : l == lerr   ? POLLERR :
            l == lhup  ? POLLHUP : POLLNVAL;
        if (pollfd[i].revents & e) {
            c[l] += snprintf(buf[l] + c[l], s - c[l], " %d", pollfd[i].fd);
            if (c[l] >= s || c[l] <= 0)
                FAIL_EXECUTION("buffer error", 0);
        }
    }
    for (l = lmin; l < lmax; l++) {
        /* Output each list when not empty */
        if (c[l] > 1) {
            if (var.base == NULL)
                fprintf(stdout, "%s\n", buf[l]);
            else if (!poll_bind_variable(varbuf[l], -1, NULL, &buf[l][2]))
                FAIL_EXECUTION(NULL, 0);
        }
    }
    EXIT_OK();
}

static char *const poll_doc[] = {
    "Polls a set of FDs for IO. Outputs the FDs that have data to e ready.",
    "Timeout can be any number, including fractions. A negative nr means",
    "no timeout. On timoeut success is returned with no output if no fds.",
    "are ready.",
    "poll takes sets of fd, one set to check for read, one for write and one",
    "for priority. Indicated by r, w, and p respectively (or any word starting",
    "with these letters). The default set is read.",
    "Examples:",
    "         poll 10 r 1 2 3 w 3 4 5 e 2 6 7",
    "           Polls FDs 1 2 3 for read events, 3 3 5 for write events and",
    "           2 6 7 for execptions. With a timout of 10 seconds.",
    "         poll 5.3 1 2 3 8",
    "           Polls FDs 1 2 3 8 for read events with a timeout of 5.3 seconds.",
    "         poll 0 1 2 3 w 5 6 7",
    "           Checks if FDs 1 2 3 are ready for read and 5 6 7 are ready for",
    "           write, returns immediately.",
    "         poll 0 1 2 3 e 5 6 7",
    "           Checks if FDs 1 2 3 are ready for read and 5 6 7 are have",
    "           execeptions, returns immediately.",
    "         poll 0 write 0 read 1 w 2 r 3",
    "           FD sets can be specified multiple time on the command line.",
    "",
    "Output:",
    "Poll will output any, either or all of the following lines:",
    "R fd ....      -  POLLIN   FDs (ready for read).",
    "W fd ....      -  POLLOUT  FDs (ready for write).",
    "P fd ....      -  POLLPRI  FDs (exceptions).",
    "E fd ....      -  POLERR   FDs (connection reset by peer).",
    "H fd ....      -  POLLHUP  FDs (hungup on the write end).",
    "I fd ....      -  POLLNVAL FDs (invalid / not exist).",
    NULL
};

struct builtin poll_struct = {
    .name = "poll",
    .function = poll_builtin,
    .flags = BUILTIN_ENABLED,
    .long_doc = poll_doc,
    .short_doc = "poll [-v [var]] timeout [r fd1 [fd2] ... w fd1 [fd2] ... e fd1 [fd2] ...]",
    .handle = NULL
};
