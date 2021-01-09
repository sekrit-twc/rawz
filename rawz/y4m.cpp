#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include "io.h"
#include "stream.h"

using namespace std::string_literals;


namespace rawz {

namespace {

constexpr int CHROMA_LEFT = 0;
constexpr int CHROMA_CENTER = 1;
constexpr int CHROMA_TOP_LEFT = 2;
constexpr int CHROMA_TOP = 3;
constexpr int CHROMA_BOTTOM_LEFT = 4;
constexpr int CHROMA_BOTTOM = 5;


void parse_uint(const char *str, unsigned &out)
{
	auto throw_error = [&]()
	{
		throw std::runtime_error{ "invalid integer: "s + str };
	};

	if (!*str)
		throw_error();

	char *endp = nullptr;
	long long x = strtoll(str, &endp, 10);

	// Invalid number.
	if (*endp)
		throw_error();

#if LLONG_MAX > UINT_MAX
	if (x > static_cast<long long>(UINT_MAX))
		throw_error();
#endif
	if (x < 0)
		throw_error();

	out = static_cast<unsigned>(x);
}

void parse_rational(const char *str, int64_t &num, int64_t &den)
{
	auto throw_error = [&]()
	{
		throw std::runtime_error{ "invalid rational: "s + str };
	};

	const char *colon = std::strchr(str, ':');
	if (!colon || !*(colon + 1))
		throw_error();

	char *endp = nullptr;

	long long num_val = strtoll(str, &endp, 10);
	if (endp != colon)
		throw_error();

	long long den_val = strtoll(colon + 1, &endp, 10);
	if (*endp)
		throw_error();

#if LLONG_MAX > INT64_MAX
	if (num > INT64_MAX || num < INT64_MIN || den > INT64_MAX || den < INT64_MIN)
		throw_error();
#endif

	num = num_val;
	den = den_val;
}


class Y4MStream : public VideoStream {
	std::unique_ptr<IOStream> m_io;
	rawz_format m_format;
	rawz_metadata m_metadata;
	uint64_t m_offset;
	uint64_t m_packet_size;
	int64_t m_frameno;

	void decode_color_format(const char *str)
	{
		auto throw_error = [&]()
		{
			throw std::runtime_error{ "unsupported color format: "s + str };
		};

		auto set420special = [&](int chromaloc)
		{
			m_format.planes_mask = 0x7;
			m_format.subsample_w = 1;
			m_format.subsample_h = 1;
			m_format.bytes_per_sample = 1;
			m_format.bits_per_sample = 8;
			m_metadata.chromaloc = chromaloc;
		};

		// Special formats.
		if (!std::strcmp(str, "420jpeg")) {
			set420special(CHROMA_CENTER);
			return;
		} else if (!std::strcmp(str, "420mpeg2")) {
			set420special(CHROMA_LEFT);
			return;
		} else if (!std::strcmp(str, "420paldv")) {
			set420special(CHROMA_TOP_LEFT);
			return;
		} else if (!std::strcmp(str, "444alpha")) {
			m_format.planes_mask = 0xF;
			m_format.subsample_w = 0;
			m_format.subsample_h = 0;
			m_format.bytes_per_sample = 1;
			m_format.bits_per_sample = 8;
			return;
		}

		// Classify subsampling format.
		rawz_format format = m_format;
		const char *depth = str;

		auto set_color_format = [&](unsigned planes, unsigned subsample_w, unsigned subsample_h)
		{
			format.planes_mask = planes;
			format.subsample_w = subsample_w;
			format.subsample_h = subsample_h;
		};

		if (!strncmp(str, "mono", 4)) {
			set_color_format(0x1, 0, 0);
			depth = str + 4;
		} else if (!strncmp(str, "420", 3)) {
			set_color_format(0x7, 1, 1);
			depth = str + 3;
		} else if (!strncmp(str, "422", 3)) {
			set_color_format(0x7, 1, 0);
			depth = str + 3;
		} else if (!strncmp(str, "444", 3)) {
			set_color_format(0x7, 0, 0);
			depth = str + 3;
		} else if (!strncmp(str, "410", 3)) {
			set_color_format(0x7, 2, 2);
			depth = str + 3;
		} else if (!strncmp(str, "411", 3)) {
			set_color_format(0x7, 2, 0);
			depth = str + 3;
		} else if (!strncmp(str, "440", 3)) {
			set_color_format(0x7, 0, 1);
			depth = str + 3;
		} else {
			throw_error();
		}

		// Default is 8-bit.
		if (!*depth) {
			format.bytes_per_sample = 1;
			format.bits_per_sample = 8;
			m_format = format;
			return;
		}

		// Skip the 'p' in 420p8, etc.
		if (*str == '4')
			++depth;

		// Classify bit depth.
		if (!strcmp(depth, "h")) {
			format.bytes_per_sample = 2;
			format.bits_per_sample = 16;
			format.floating_point = 1;
		} else if (!strcmp(depth, "s")) {
			format.bytes_per_sample = 4;
			format.bits_per_sample = 32;
			format.floating_point = 1;
		} else {
			unsigned d;

			parse_uint(depth, d);
			if (d > 16)
				throw_error();

			format.bytes_per_sample = (d + 7) / 8;
			format.bits_per_sample = d;
		}

		m_format = format;
	}

	void decode_extension(const char *str, bool &have_yscss_error)
	{
		const char *delim = std::strchr(str, '=');
		if (!delim)
			return;

		std::string key{ str, static_cast<size_t>(delim - str) };
		std::string val{ delim + 1 };

		if (key == "YSCSS") {
			std::string lower = str;
			std::transform(lower.begin(), lower.end(), lower.begin(), std::tolower);
			try {
				decode_color_format(lower.c_str());
				have_yscss_error = false;
			} catch (const std::runtime_error &) {
				have_yscss_error = true;
			}
		} else if (key == "COLORRANGE") {
			if (val == "FULL")
				m_metadata.fullrange = 1;
			else if (val == "LIMITED")
				m_metadata.fullrange = 0;
		}
	}

	// Return true on end of header.
	bool read_header_property(bool &have_c, bool &have_yscss_error)
	{
		const size_t limit = 128;
		std::string buffer;

		do {
			if (buffer.size() >= limit)
				throw std::runtime_error{ "Y4M header too long" };

			buffer.push_back(m_io->read<char>());
		} while (buffer.back() != ' ' && buffer.back() != '\n');

		bool eoh = buffer.back() == '\n';

		// Skip empty properties (e.g. multiple spaces).
		buffer.pop_back();
		if (buffer.empty())
			return eoh;

		switch (buffer.front()) {
		case 'W':
			parse_uint(buffer.c_str() + 1, m_format.width);
			break;
		case 'H':
			parse_uint(buffer.c_str() + 1, m_format.height);
			break;
		case 'F':
			parse_rational(buffer.c_str() + 1, m_metadata.fpsnum, m_metadata.fpsden);
			break;
		case 'A':
			parse_rational(buffer.c_str() + 1, m_metadata.sarnum, m_metadata.sarden);
			break;
		case 'C':
			decode_color_format(buffer.c_str() + 1);
			have_c = true;
			break;
		case 'I':
			if (buffer[1] == 'p')
				m_metadata.fieldorder = 0;
			else if (buffer[1] == 't')
				m_metadata.fieldorder = 1;
			else if (buffer[2] == 'b')
				m_metadata.fieldorder = 2;
			break;
		case 'X':
			decode_extension(buffer.c_str() + 1, have_yscss_error);
			break;
		default:
			break;
		}

		return eoh;
	}

	void read_header()
	{
		std::array<char, sizeof("YUV4MPEG2 ") - 1> header{};
		m_io->read(header);
		if (std::strncmp(header.data(), "YUV4MPEG2 ", header.size()))
			throw std::runtime_error{ "missing Y4M header" };

		bool have_c = false;
		bool have_yscss_error = false;

		while (!read_header_property(have_c, have_yscss_error)) {
			// ...
		}

		if (have_yscss_error && !have_c)
			throw std::runtime_error{ "invalid extended colorspace" };

		// Assume YUV420P8 format by default.
		if (!m_format.planes_mask) {
			m_format.planes_mask = 0x7;
			m_format.subsample_w = 1;
			m_format.subsample_h = 1;
			m_format.bytes_per_sample = 1;
			m_format.bits_per_sample = 8;
		}
	}
public:
	explicit Y4MStream(std::unique_ptr<IOStream> io) :
		m_io{ std::move(io) },
		m_metadata(default_metadata()),
		m_offset{},
		m_packet_size{},
		m_frameno{}
	{
		read_header();

		if (!is_valid_format(m_format))
			throw std::runtime_error{ "incomplete Y4M header" };

		m_offset = m_io->tell();
		m_packet_size = std::strlen("FRAME\n") + planar_frame_size(m_format);
	}

	int64_t framecount() const noexcept override
	{
		if (m_io->seekable())
			return (m_io->length() - m_offset) / m_packet_size;
		else
			return 0;
	}

	rawz_metadata metadata() const noexcept override { return m_metadata; }

	void read(int n, void * const planes[4], const ptrdiff_t stride[4]) override try
	{
		if (m_frameno == INT64_MAX)
			throw IOStream::eof{};

		if (n != m_frameno) {
			uint64_t pos = m_offset + m_packet_size * static_cast<uint64_t>(n);
			if (pos > static_cast<uint64_t>(INT64_MAX))
				throw std::runtime_error{ "invalid file position" };

			m_io->seek(pos, IOStream::seek_set);
		}

		std::array<char, sizeof("FRAME\n") - 1> header{};
		m_io->read(header);
		if (!std::strncmp(header.data(), "FRAME ", header.size()))
			throw std::runtime_error{ "Y4M frame properties not supported" };
		if (std::strncmp(header.data(), "FRAME\n", header.size()))
			throw std::runtime_error{ "missing Y4M frame header" };

		blit_planar_frame(m_io.get(), m_format, planes, stride);
		++m_frameno;
	} catch (...) {
		m_frameno = -1;
		throw;
	}

	const rawz_format &format() const { return m_format; }
};

} // namespace


std::unique_ptr<VideoStream> create_y4m_stream(std::unique_ptr<IOStream> io, rawz_format *format)
{
	std::unique_ptr<Y4MStream> stream = std::make_unique<Y4MStream>(std::move(io));
	*format = stream->format();
	return std::move(stream);
}

} // namespace rawz
