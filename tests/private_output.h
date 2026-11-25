#ifndef TEMPO_TEST_PRIVATE_OUTPUT_H
#define TEMPO_TEST_PRIVATE_OUTPUT_H
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
/* Test artifacts stay private even under a permissive invoking shell umask. */
static FILE *private_output(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    if (fd < 0) return NULL;
    if (fchmod(fd, S_IRUSR | S_IWUSR) != 0) { close(fd); return NULL; }
    FILE *stream = fdopen(fd, "wb");
    if (!stream) close(fd);
    return stream;
}
#endif
