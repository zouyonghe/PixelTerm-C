#ifndef TEST_OUTPUT_STREAM_H
#define TEST_OUTPUT_STREAM_H

#include <stdio.h>
#include <unistd.h>
#include <glib.h>

#ifdef __APPLE__
typedef struct {
    void *data;
    ssize_t (*write)(void *, const char *, size_t);
} TestOutputCookie;

static int test_output_write(void *data, const char *bytes, int length) {
    TestOutputCookie *cookie = data;
    return (int)cookie->write(cookie->data, bytes, (size_t)length);
}

static int test_output_close(void *data) {
    g_free(data);
    return 0;
}
#endif

static FILE *test_output_stream(void *data, ssize_t (*write_fn)(void *, const char *, size_t)) {
#ifdef __APPLE__
    TestOutputCookie *cookie = g_new(TestOutputCookie, 1);
    *cookie = (TestOutputCookie){data, write_fn};
    FILE *stream = funopen(cookie, NULL, test_output_write, NULL, test_output_close);
    if (!stream) g_free(cookie);
    return stream;
#else
    cookie_io_functions_t hooks = {.write = write_fn};
    return fopencookie(data, "w", hooks);
#endif
}

#endif
