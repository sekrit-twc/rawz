#pragma once

#ifndef RAWZ_IO_H_
#define RAWZ_IO_H_

#include <cstdint>
#include <exception>
#include <memory>
#include "rawz.h"

struct rawz_io_stream {
protected:
	~rawz_io_stream() = default;
};


namespace rawz {

class IOStream : public rawz_io_stream {
public:
	struct eof : public std::exception {
		const char *what() const noexcept override { return "eof"; }
	};

	static constexpr int seek_set = 0;
	static constexpr int seek_cur = 1;
	static constexpr int seek_end = 2;

	virtual ~IOStream() = default;

	IOStream &operator=(IOStream &) = delete;

	virtual bool seekable() const = 0;

	virtual void read(void *buf, size_t n) = 0;

	virtual void seek(int64_t offset, int whence) = 0;

	virtual uint64_t tell() const = 0;

	virtual uint64_t length() const = 0;

	virtual void skip(size_t n);

	template <class T>
	void read(T &t) { read(&t, sizeof(t)); }

	template <class T>
	T read() { T t; read(t); return t; }
};


// Helper function. Performs a seek if the stream is not positioned at frame n.
void seek_to_frame(IOStream *io, int64_t &cur_frame, int64_t n, uint64_t packet_size, uint64_t base_offset=0);


std::unique_ptr<IOStream> create_stdio_stream(const char *path, bool seekable, uint64_t offset);

std::unique_ptr<IOStream> create_stdio_stream_fd(int fd, bool seekable, uint64_t offset);

std::unique_ptr<IOStream> create_user_stream(rawz_io_user_read read, rawz_io_user_seek seek, rawz_io_user_tell tell, rawz_io_user_close close, int64_t length, void *user);

} // namespace rawz

#endif // RAWZ_IO_H_
