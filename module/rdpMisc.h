/*
Copyright 2005-2017 Jay Sorg

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

the rest

*/

#ifndef __RDPMISC_H
#define __RDPMISC_H

#include <xorg-server.h>
#include <xorgVersion.h>
#include <xf86.h>

#include <X11/Xos.h>

#include <config_ac.h>

#if defined(HAVE_FUNC_ATTRIBUTE_FORMAT)
#define printflike(arg_format, arg_first_check) \
 __attribute__((__format__(__printf__, arg_format, arg_first_check)))
#else
#define printflike(arg_format, arg_first_check)
#endif


extern _X_EXPORT int
crc_start(void);
extern _X_EXPORT int
crc_process_data(int crc, const void *data, int data_bytes);
extern _X_EXPORT int
crc_end(int crc);

extern _X_EXPORT int
rdpBitsPerPixel(int depth);
extern _X_EXPORT int
g_sck_can_recv(int sck, int millis);
extern _X_EXPORT int
g_sck_recv(int sck, void *ptr, int len, int flags);
extern _X_EXPORT void
g_sck_close(int sck);
extern _X_EXPORT int
g_sck_last_error_would_block(int sck);
extern _X_EXPORT void
g_sleep(int msecs);
extern _X_EXPORT int
g_sck_send(int sck, const void *ptr, int len, int flags);
extern _X_EXPORT void
g_sprintf(char *dest, const char *format, ...);
extern _X_EXPORT int
g_sck_tcp_socket(void);
extern _X_EXPORT int
g_sck_local_socket_dgram(void);
extern _X_EXPORT int
g_sck_local_socket_stream(void);
extern _X_EXPORT void
g_memcpy(void *d_ptr, const void *s_ptr, int size);
extern _X_EXPORT void
g_memset(void *d_ptr, const unsigned char chr, int size);
extern _X_EXPORT int
g_sck_tcp_set_no_delay(int sck);
extern _X_EXPORT int
g_sck_set_non_blocking(int sck);
extern _X_EXPORT int
g_sck_accept(int sck);
extern _X_EXPORT int
g_sck_select(int sck1, int sck2, int sck3);
extern _X_EXPORT int
g_sck_tcp_bind(int sck, char *port);
extern _X_EXPORT int
g_sck_local_bind(int sck, char *port);
extern _X_EXPORT int
g_sck_listen(int sck);
extern _X_EXPORT int
g_create_dir(const char *dirname);
extern _X_EXPORT int
g_directory_exist(const char *dirname);
extern _X_EXPORT int
g_chmod_hex(const char *filename, int flags);
extern _X_EXPORT const char *
g_socket_dir(void);
extern _X_EXPORT void
g_hexdump(const void *p, long len);
extern _X_EXPORT int
g_sck_send_fd_set(int sck, const void *ptr, unsigned int len,
                  int fds[], unsigned int fdcount);
extern _X_EXPORT int
g_alloc_shm_map_fd(void **addr, int *fd, size_t size);
extern _X_EXPORT int
g_alloc_map_fd(void **addr, int *fd, size_t size);
extern _X_EXPORT void
g_free_unmap_fd(void *addr, int fd, size_t size);

/* glib-style memory allocation macros */
#define g_new(struct_type, n_structs) \
    (struct_type *) xnfalloc(sizeof(struct_type) * (n_structs))
#define g_new0(struct_type, n_structs) \
    (struct_type *) xnfcalloc((n_structs), sizeof(struct_type))


#if defined(X_BYTE_ORDER)
#  if X_BYTE_ORDER == X_LITTLE_ENDIAN
#    define L_ENDIAN
#  else
#    define B_ENDIAN
#  endif
#else
#  error Unknown endianness in rdp.h
#endif
/* check if we need to align data */
#if defined(__sparc__) || defined(__alpha__) || defined(__hppa__) || \
    defined(__AIX__) || defined(__PPC__) || defined(__mips__) || \
    defined(__ia64__) || defined(__ppc__) || defined(__arm__)
#define NEED_ALIGN
#endif

/* parser state */
struct stream
{
    char *p;
    char *end;
    char *data;
    int size;
    /* offsets of various headers */
    char *iso_hdr;
    char *mcs_hdr;
    char *sec_hdr;
    char *rdp_hdr;
    char *channel_hdr;
    char *next_packet;
};

/******************************************************************************/
#define s_push_layer(s, h, n) \
do {                          \
    (s)->h = (s)->p;          \
    (s)->p += (n);            \
} while (0)

/******************************************************************************/
#define s_pop_layer(s, h) \
do {                      \
    (s)->p = (s)->h;      \
} while (0)

/******************************************************************************/
#if defined(B_ENDIAN) || defined(NEED_ALIGN)
#define out_uint16_le(s, v)                \
do {                                       \
    *((s)->p) = (unsigned char)((v) >> 0); \
    (s)->p++;                              \
    *((s)->p) = (unsigned char)((v) >> 8); \
    (s)->p++;                              \
} while (0)
#else
#define out_uint16_le(s, v)                             \
do {                                                    \
    *((unsigned short*)((s)->p)) = (unsigned short)(v); \
    (s)->p += 2;                                        \
} while (0)
#endif

/******************************************************************************/
#define init_stream(s, v)                    \
do {                                         \
    if ((v) > (s)->size)                     \
    {                                        \
        free((s)->data);                   \
        (s)->data = g_new(char, (v));        \
        (s)->size = (v);                     \
    }                                        \
    (s)->p = (s)->data;                      \
    (s)->end = (s)->data;                    \
    (s)->next_packet = 0;                    \
} while (0)

/******************************************************************************/
#define out_uint8p(s, v, n)     \
do {                            \
    g_memcpy((s)->p, (v), (n)); \
    (s)->p += (n);              \
} while (0)

/******************************************************************************/
#define out_uint8a(s, v, n)    \
do  {                          \
    out_uint8p((s), (v), (n)); \
} while (0)

/******************************************************************************/
#define out_uint8(s, v)                    \
do {                                       \
    *((s)->p) = (unsigned char)((v) >> 0); \
    (s)->p++;                              \
} while (0)

/******************************************************************************/
#if defined(B_ENDIAN) || defined(NEED_ALIGN)
#define out_uint32_le(s, v)                 \
do {                                        \
    *((s)->p) = (unsigned char)((v) >> 0);  \
    (s)->p++;                               \
    *((s)->p) = (unsigned char)((v) >> 8);  \
    (s)->p++;                               \
    *((s)->p) = (unsigned char)((v) >> 16); \
    (s)->p++;                               \
    *((s)->p) = (unsigned char)((v) >> 24); \
    (s)->p++;                               \
} while (0)
#else
#define out_uint32_le(s, v)           \
do {                                  \
    *((unsigned int*)((s)->p)) = (v); \
    (s)->p += 4;                      \
} while (0)
#endif

/******************************************************************************/
#if defined(B_ENDIAN) || defined(NEED_ALIGN)
#define in_uint32_le(s, v)                            \
do {                                                  \
    (v) = (unsigned int)                              \
        (                                             \
            (*((unsigned char*)((s)->p + 0)) << 0) |  \
            (*((unsigned char*)((s)->p + 1)) << 8) |  \
            (*((unsigned char*)((s)->p + 2)) << 16) | \
            (*((unsigned char*)((s)->p + 3)) << 24)   \
        );                                            \
    (s)->p += 4;                                      \
} while (0)
#else
#define in_uint32_le(s, v)            \
do {                                  \
    (v) = *((unsigned int*)((s)->p)); \
    (s)->p += 4;                      \
} while (0)
#endif

/******************************************************************************/
#if defined(B_ENDIAN) || defined(NEED_ALIGN)
#define in_uint16_le(s, v)                           \
do {                                                 \
    (v) = (unsigned short)                           \
        (                                            \
            (*((unsigned char*)((s)->p + 0)) << 0) | \
            (*((unsigned char*)((s)->p + 1)) << 8)   \
        );                                           \
    (s)->p += 2;                                     \
} while (0)
#else
#define in_uint16_le(s, v)              \
do {                                    \
    (v) = *((unsigned short*)((s)->p)); \
    (s)->p += 2;                        \
} while (0)
#endif

/******************************************************************************/
#define in_uint8a(s, v, n)      \
do {                            \
    g_memcpy((v), (s)->p, (n)); \
    (s)->p += (n); \
} while (0)

/******************************************************************************/
#define s_mark_end(s)   \
do {                    \
    (s)->end = (s)->p;  \
} while (0)

/******************************************************************************/
#define make_stream(s)                                        \
do {                                                          \
    (s) = g_new0(struct stream, 1);                           \
} while (0)

/******************************************************************************/
#define free_stream(s)     \
do {                       \
    if ((s) != 0)          \
    {                      \
        free((s)->data); \
    }                      \
    free((s));           \
} while (0)

#endif
