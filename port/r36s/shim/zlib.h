/* R36S: the part of zlib port/vita/vita_vgl.c names (its test pictures packed into the log): not available here,
   deflateInit fails and the pictures are left out. */
#ifndef R36S_ZLIB_H
#define R36S_ZLIB_H
typedef unsigned char Bytef;
typedef unsigned int uInt;
typedef unsigned long uLong;
typedef struct z_stream_s { const Bytef *next_in; uInt avail_in; uLong total_in; Bytef *next_out; uInt avail_out; uLong total_out; void *opaque; } z_stream;
#define Z_OK 0
#define Z_STREAM_END 1
#define Z_STREAM_ERROR (-2)
#define Z_BUF_ERROR (-5)
#define Z_NO_FLUSH 0
#define Z_FINISH 4
#define Z_DEFAULT_COMPRESSION (-1)
static inline int deflateInit(z_stream *s, int level) { (void)s; (void)level; return Z_STREAM_ERROR; }
static inline int deflate(z_stream *s, int flush) { (void)s; (void)flush; return Z_STREAM_ERROR; }
static inline int deflateEnd(z_stream *s) { (void)s; return Z_OK; }
#endif
