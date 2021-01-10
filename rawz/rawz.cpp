#include <stdexcept>
#include <string>
#include "io.h"
#include "rawz.h"
#include "stream.h"

namespace {

thread_local std::string g_last_error;

void update_last_error(const char *msg) noexcept
{
	try {
		g_last_error = msg;
	} catch (const std::bad_alloc &) {
		g_last_error = "";
	}
}

void record_exception() noexcept
{
	try {
		std::rethrow_exception(std::current_exception());
	} catch (const std::exception &e) {
		update_last_error(e.what());
	} catch (...) {
		g_last_error = "";
	}
}

} // namespace


const char *rawz_get_last_error()
{
	return g_last_error.c_str();
}

void rawz_clear_last_error()
{
	g_last_error.clear();
}

rawz_io_stream *rawz_io_open_file(const char *path, int seekable) try
{
	return rawz::create_stdio_stream(path, !!seekable).release();
} catch (...) {
	record_exception();
	return nullptr;
}

rawz_io_stream *rawz_io_open_fd(int fd, int seekable) try
{
	return rawz::create_stdio_stream_fd(fd, !!seekable).release();
} catch (...) {
	record_exception();
	return nullptr;
}

rawz_io_stream *rawz_io_wrap_user(rawz_io_user_read read, rawz_io_user_seek seek, rawz_io_user_tell tell, rawz_io_user_close close,
                                  int64_t length, void *user) try
{
	return rawz::create_user_stream(read, seek, tell, close, length, user).release();
} catch (...) {
	record_exception();
	return nullptr;
}

void rawz_io_stream_close(rawz_io_stream *ptr)
{
	delete static_cast<rawz::IOStream *>(ptr);
}

rawz_video_stream *rawz_video_stream_create(rawz_io_stream *io, rawz_format *format) try
{
	std::unique_ptr<rawz::IOStream> io_ptr{ static_cast<rawz::IOStream *>(io) };

	switch (format->mode) {
	case RAWZ_PLANAR:
		return rawz::create_planar_stream(std::move(io_ptr), format).release();
	case RAWZ_Y4M:
		return rawz::create_y4m_stream(std::move(io_ptr), format).release();
	case RAWZ_NV:
		return rawz::create_nv_stream(std::move(io_ptr), format).release();
	case RAWZ_ARGB:
	case RAWZ_RGBA:
	case RAWZ_RGB:
	case RAWZ_RGB30:
	case RAWZ_YUYV:
	case RAWZ_UYVY:
	case RAWZ_V210:
		return rawz::create_interleaved_stream(std::move(io_ptr), format).release();
	default:
		throw std::runtime_error{ "unsupported packing mode" };
	}
} catch (...) {
	record_exception();
	return nullptr;
}

int64_t rawz_video_stream_framecount(const rawz_video_stream *ptr)
{
	return static_cast<const rawz::VideoStream *>(ptr)->framecount();
}

void rawz_video_stream_metadata(const rawz_video_stream *ptr, rawz_metadata *metadata)
{
	*metadata = static_cast<const rawz::VideoStream *>(ptr)->metadata();
}

int rawz_video_stream_read(rawz_video_stream *ptr, int n, void * const planes[4], const ptrdiff_t stride[4]) try
{
	static_cast<rawz::VideoStream *>(ptr)->read(n, planes, stride);
	return 0;
} catch (const rawz::IOStream::eof &) {
	record_exception();
	return 1;
} catch (...) {
	record_exception();
	return -1;
}

void rawz_video_stream_free(rawz_video_stream *ptr)
{
	delete static_cast<rawz::VideoStream *>(ptr);
}

void rawz_format_default(rawz_format *ptr)
{
	*ptr = rawz_format{};
}
