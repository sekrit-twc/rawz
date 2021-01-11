#ifndef _WIN32
  #define _LARGEFILE_SOURCE
#endif

#include <cassert>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <system_error>
#include "io.h"

#ifdef _WIN32
  #include <Windows.h>

  #define WSTR(x) L##x
  #define filechar_t wchar_t

  #define fdopen _fdopen
  #define fileno _fileno
  #define fseeko _fseeki64
  #define fstat _fstat64
  #define ftello _ftelli64
  #define stat __stat64
#else
  #include <cerrno>
  #define WSTR(x) x
  #define filechar_t char
  static_assert(sizeof(off_t) == sizeof(int64_t), "64-bit files required");
#endif


namespace rawz {

namespace {

static_assert(IOStream::seek_set == SEEK_SET, "seek macro mismatch");
static_assert(IOStream::seek_cur == SEEK_CUR, "seek macro mismatch");
static_assert(IOStream::seek_end == SEEK_END, "seek macro mismatch");


[[noreturn]] void throw_system_error()
{
#ifdef _WIN32
	throw std::system_error{ static_cast<int>(GetLastError()), std::system_category() };
#else
	throw std::system_error{ errno, std::generic_category() };
#endif
}


struct FileCloser {
	void operator()(std::FILE *f) { if (f) std::fclose(f); }
};

typedef std::unique_ptr<std::FILE, FileCloser> unique_file;


unique_file unicode_open(const char *path, const filechar_t *mode)
{
#ifdef _WIN32
	std::wstring ws;
	int len = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
	if (len <= 0)
		throw std::runtime_error{ "unicode translation error" };
	ws.resize(len);

	len = MultiByteToWideChar(CP_UTF8, 0, path, -1, &ws.front(), len);
	if (len <= 0)
		throw std::runtime_error{ "unicode translation error" };
	ws.resize(len);

	return unique_file{ _wfopen(ws.c_str(), mode) };
#else
	return unique_file{ std::fopen(path, mode) };
#endif
}

uint64_t file_length(std::FILE *file)
{
	struct stat st{};
	int fd = fileno(file);
	static_assert(sizeof(st.st_size) == sizeof(int64_t), "");

	if (fstat(fd, &st))
		throw_system_error();

	assert(st.st_size >= 0);
	return st.st_size;
}


class FileIOStream : public IOStream {
	unique_file m_file;
	uint64_t m_offset;
	uint64_t m_length;
	mutable uint64_t m_where;
	bool m_seekable;
	mutable bool m_valid_pos;

	void do_tell() const
	{
		int64_t pos = ftello(m_file.get());
		if (pos < 0 || static_cast<uint64_t>(pos) < m_offset)
			throw_system_error();
		m_where = pos;
		m_valid_pos = true;
	}
public:
	FileIOStream(unique_file file, bool seekable, uint64_t offset) :
		m_file{ std::move(file) },
		m_offset{},
		m_length{},
		m_where{},
		m_seekable{ seekable },
		m_valid_pos{}
	{
		if (seekable) {
			m_offset = offset;
			m_length = file_length(m_file.get());

			if (m_offset > m_length)
				throw std::runtime_error{ "offset past end of file" };
			if (fseeko(m_file.get(), m_offset, SEEK_SET))
				throw std::runtime_error{ "seek error" };

			do_tell();
		}
	}

	bool seekable() const override { return m_seekable; }

	void read(void *buf, size_t n) override
	{
		std::FILE *file = m_file.get();
		unsigned char *buf_p = static_cast<unsigned char *>(buf);

		while (n) {
			size_t res = std::fread(buf_p, 1, n, file);
			assert(res <= n);
			m_where += res;
			n -= res;

			if (std::ferror(file))
				throw_system_error();
			if (std::feof(file))
				throw eof{};
		}
	}

	void seek(int64_t offset, int whence) override
	{
		static_assert(sizeof(offset) == sizeof(m_where), "");
		static_assert(~int64_t(0) == int64_t(-1), "twos complement");

		auto throw_error = []() { throw std::runtime_error{ "offset out of bounds" }; };

		if (!m_valid_pos) {
			do_tell();
			if (!m_valid_pos)
				throw_error();
		}

		uint64_t abs_offset = m_where;

		// Rebase the offset.
		switch (whence) {
		case seek_set:
			if (offset < 0)
				throw_error();
			abs_offset = m_offset + static_cast<uint64_t>(offset);
			break;
		case seek_end:
			if (offset > 0)
				throw_error();
			abs_offset = m_length + static_cast<uint64_t>(offset);
			break;
		case seek_cur:
			if (offset < 0 && m_where <= static_cast<uint64_t>(INT64_MAX) && offset < -static_cast<int64_t>(m_where))
				throw_error();
			if (offset >= 0 && static_cast<uint64_t>(offset) > m_length - m_offset)
				throw_error();
			abs_offset = m_where + static_cast<uint64_t>(offset);
			break;
		default:
			break;
		}

		if (abs_offset > static_cast<uint64_t>(INT64_MAX))
			throw_error();

		if (fseeko(m_file.get(), abs_offset, SEEK_SET)) {
			m_valid_pos = false;
			throw_system_error();
		}
		m_where = abs_offset;
	}

	uint64_t tell() const override
	{
		if (!m_valid_pos)
			do_tell();
		if (m_where < m_offset)
			throw std::runtime_error{ "invalid file position" };
		return m_where - m_offset;
	}

	uint64_t length() const override { return m_length - m_offset; }
};


class UserIOStream : public IOStream {
	rawz_io_user_read m_read;
	rawz_io_user_seek m_seek;
	rawz_io_user_tell m_tell;
	rawz_io_user_close m_close;
	uint64_t m_length;
	void *m_user;
public:
	UserIOStream(rawz_io_user_read read, rawz_io_user_seek seek, rawz_io_user_tell tell, rawz_io_user_close close,
	             int64_t length, void *user) :
		m_read{ read },
		m_seek{ seek },
		m_tell{ tell },
		m_close{ close },
		m_length{ seek ? static_cast<uint64_t>(length) : 0 },
		m_user{ user }
	{}

	UserIOStream(const UserIOStream &) = delete;

	~UserIOStream() { m_close(m_user); }

	UserIOStream &operator=(const UserIOStream &) = delete;

	bool seekable() const override { return !!m_seek; }

	void read(void *buf, size_t n) override
	{
		int res = m_read(buf, n, m_user);

		if (res < 0)
			throw std::runtime_error{ "user read error" };
		else if (res > 0)
			throw eof{};
	}

	void seek(int64_t offset, int whence) override
	{
		if (m_seek(offset, whence, m_user))
			throw std::runtime_error{ "user seek error" };
	}

	uint64_t tell() const override
	{
		int64_t pos;

		if ((pos = m_tell(m_user)) < 0)
			throw std::runtime_error{ "user tell error" };

		return pos;
	}

	uint64_t length() const override { return m_length; }
};

} // namespace


void IOStream::skip(size_t n)
{
	constexpr size_t thresh = 4096;

	if (n >= thresh && seekable()) {
		while (n) {
			int64_t count = std::min(static_cast<uint64_t>(n), static_cast<uint64_t>(INT64_MAX));
			seek(static_cast<int64_t>(n), seek_cur);
		}
	} else {
		char buf[4096];

		while (n) {
			size_t cur = std::min(n, sizeof(buf));
			read(buf, cur);
			n -= cur;
		}
	}
}


void seek_to_frame(IOStream *io, int64_t &cur_frame, int64_t n, uint64_t packet_size, uint64_t base_offset) try
{
	if (cur_frame == n)
		return;
	if (cur_frame == INT64_MAX)
		throw IOStream::eof{};
	if (n < 0)
		throw IOStream::eof{};
	if (static_cast<uint64_t>(n) > (static_cast<uint64_t>(INT64_MAX) - base_offset) / packet_size)
		throw IOStream::eof{};

	uint64_t where = base_offset + static_cast<uint64_t>(n) * packet_size;
	io->seek(where, IOStream::seek_set);
	cur_frame = n;
} catch (...) {
	cur_frame = -1;
	throw;
}


std::unique_ptr<IOStream> create_stdio_stream(const char *path, bool seekable, uint64_t offset)
{
	return std::make_unique<FileIOStream>(unicode_open(path, WSTR("rb")), seekable, offset);
}

std::unique_ptr<IOStream> create_stdio_stream_fd(int fd, bool seekable, uint64_t offset)
{
	return std::make_unique<FileIOStream>(unique_file{ fdopen(fd, "rb") }, seekable, offset);
}

std::unique_ptr<IOStream> create_user_stream(rawz_io_user_read read, rawz_io_user_seek seek, rawz_io_user_tell tell, rawz_io_user_close close,
                                             int64_t length, void *user)
{
	return std::make_unique<UserIOStream>(read, seek, tell, close, length, user);
}
} // namespace rawz
