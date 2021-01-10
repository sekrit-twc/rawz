#ifndef RAWZ_H_
#define RAWZ_H_

#include <stddef.h>
#include <stdint.h>

typedef enum rawz_packing_mode {
	RAWZ_PLANAR,
	RAWZ_Y4M,
	RAWZ_ARGB,
	RAWZ_RGBA,
	RAWZ_RGB,
	RAWZ_RGB30,
	RAWZ_NV,
	RAWZ_YUYV,
	RAWZ_UYVY,
	RAWZ_V210,
} rawz_packing_mode;

typedef struct rawz_format {
	rawz_packing_mode mode;
	unsigned width;
	unsigned height;
	unsigned planes_mask;
	unsigned subsample_w; /* log2 */
	unsigned subsample_h; /* log2 */
	unsigned bytes_per_sample;
	unsigned bits_per_sample;
	unsigned alignment; /* log2 */
	unsigned char floating_point;
} rawz_format;

typedef struct rawz_metadata {
	int64_t sarnum;
	int64_t sarden;
	int64_t fpsnum;
	int64_t fpsden;
	int fullrange;
	int fieldorder; /* 0 = progressive, 1 = TFF, 2 = BFF */
	int chromaloc; /* As defined in ITU-T H.265 */
} rawz_metadata;


const char *rawz_get_last_error(void);

void rawz_clear_last_error(void);


typedef struct rawz_io_stream rawz_io_stream;

typedef int (*rawz_io_user_read)(void *buf, size_t n, void *user); /* 0 = success, positive = eof, negative = error */
typedef int (*rawz_io_user_seek)(int64_t offset, int whence, void *user);
typedef int64_t (*rawz_io_user_tell)(void *user);
typedef void (*rawz_io_user_close)(void *user);

rawz_io_stream *rawz_io_open_file(const char *path, int seekable, uint64_t offset);

rawz_io_stream *rawz_io_open_fd(int fd, int seekable, uint64_t offset);

rawz_io_stream *rawz_io_wrap_user(rawz_io_user_read read, rawz_io_user_seek seek, rawz_io_user_tell tell, rawz_io_user_close close,
                                  int64_t length, void *user);

void rawz_io_stream_close(rawz_io_stream *ptr);


typedef struct rawz_video_stream rawz_yuv_stream;

/* Takes ownership of io, or closes io on error. Updates format with actual parameters. */
rawz_video_stream *rawz_video_stream_create(rawz_io_stream *io, rawz_format *format);

int64_t rawz_video_stream_framecount(const rawz_video_stream *ptr);

void rawz_video_stream_metadata(const rawz_video_stream *ptr, rawz_metadata *metadata);

int rawz_video_stream_read(rawz_video_stream *ptr, int n, void * const planes[4], const ptrdiff_t stride[4]);

void rawz_video_stream_free(rawz_video_stream *ptr);


void rawz_format_default(rawz_format *ptr);

#endif /* RAWZ_H_ */
