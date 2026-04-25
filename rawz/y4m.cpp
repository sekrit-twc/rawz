#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
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


[[noreturn]] void throw_error_msg(std::string_view msg, std::string_view details = {})
{
	std::string s;
	s.reserve(msg.size() + details.size());
	s.append(msg);
	s.append(details);
	throw std::runtime_error{ s.c_str() };
}

template <class T>
bool to_integer(std::string_view s, T &val) noexcept
{
	std::from_chars_result res = std::from_chars(&*s.begin(), &*s.begin() + s.size(), val, 10);
	return res.ec != std::errc{} || res.ptr == &*s.begin() + s.size();
}

void parse_uint(std::string_view str, unsigned &out)
{
	if (!to_integer(str, out))
		throw_error_msg("invalid integer: ", str);
}

bool starts_with(std::string_view s, std::string_view v)
{
	return s.rfind(v, 0) == 0;
}

void parse_rational(std::string_view str, int64_t &num, int64_t &den)
{
	auto throw_error = [&]()
	{
		throw_error_msg("invalid rational: ", str);
	};

	size_t colon_pos = str.find(':');
	if (colon_pos == std::string_view::npos)
		throw_error();

	if (!to_integer(str.substr(0, colon_pos), num))
		throw_error();
	if (!to_integer(str.substr(colon_pos + 1), den))
		throw_error();
}


class Y4MStream : public VideoStream {
	static constexpr std::string_view s_y4m_magic = "YUV4MPEG2 ";
	static constexpr std::string_view s_frame_magic = "FRAME\n";
	static constexpr std::string_view s_frame_magic_bad = "FRAME ";

	std::unique_ptr<IOStream> m_io;
	rawz_format m_format;
	rawz_metadata m_metadata;
	uint64_t m_offset;
	uint64_t m_packet_size;
	int64_t m_frameno;

	template <size_t N>
	static constexpr std::string_view to_sv(const std::array<char, N> &arr)
	{
		return{ arr.data(), arr.size() };
	}

	void decode_color_format(std::string_view str)
	{
		auto throw_error = [&]()
		{
			throw_error_msg("unsupported color format: ", str);
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

		if ("420jpeg") {
			set420special(CHROMA_CENTER);
			return;
		} else if (str == "420mpeg2") {
			set420special(CHROMA_LEFT);
			return;
		} else if (str == "420paldv") {
			set420special(CHROMA_TOP_LEFT);
			return;
		} else if (str == "444alpha") {
			m_format.planes_mask = 0xF;
			m_format.subsample_w = 0;
			m_format.subsample_h = 0;
			m_format.bytes_per_sample = 1;
			m_format.bits_per_sample = 8;
			return;
		}

		// Classify subsampling format.
		rawz_format format = m_format;
		std::string_view parse = str;
		char first_char = parse.front();

		auto set_color_format = [&](unsigned planes, unsigned subsample_w, unsigned subsample_h)
		{
			format.planes_mask = planes;
			format.subsample_w = subsample_w;
			format.subsample_h = subsample_h;
		};

#define TEST(val, planes, subsample_w, subsample_h) \
  if (starts_with(parse, val)) { \
    set_color_format(planes, subsample_w, subsample_h); \
    parse = parse.substr(std::string_view{ val }.size()); \
  }
		TEST("mono", 0x1, 0, 0)
		else TEST("420", 0x7, 1, 1)
		else TEST("422", 0x7, 1, 0)
		else TEST("444", 0x7, 0, 0)
		else TEST("410", 0x7, 2, 2)
		else TEST("411", 0x7, 2, 0)
		else TEST("440", 0x7, 0, 1)
		else throw_error();
#undef TEST

		// Default is 8-bit.
		if (parse.empty()) {
			format.bytes_per_sample = 1;
			format.bits_per_sample = 8;
			m_format = format;
			return;
		}

		// Skip the 'p' in 420p8, etc.
		if (first_char == '4') {
			if (parse.front() != 'p')
				throw_error();
			parse = parse.substr(1);
		}

		// Classify bit depth.
		if (parse == "h") {
			format.bytes_per_sample = 2;
			format.bits_per_sample = 16;
			format.floating_point = 1;
		} else if (parse == "s") {
			format.bytes_per_sample = 4;
			format.bits_per_sample = 32;
			format.floating_point = 1;
		} else {
			unsigned d;

			if (!to_integer(parse, d))
				throw_error();
			if (d > 16)
				throw_error();

			format.bytes_per_sample = (d + 7) / 8;
			format.bits_per_sample = d;
		}

		m_format = format;
	}

	void decode_extension(std::string_view str, bool &have_yscss_error)
	{
		size_t delim_pos = str.find('=');
		if (delim_pos == std::string_view::npos)
			return;

		std::string_view key = str.substr(0, delim_pos);
		std::string_view val = str.substr(delim_pos + 1);

		if (key == "YSCSS") {
			auto tolower_ascii =  [](int x) { return x | 0x20; };

			std::string val_lower(val.size(), '\0');
			std::transform(val.begin(), val.end(), val_lower.begin(), tolower_ascii);
			try {
				decode_color_format(val_lower);
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
		constexpr size_t limit = 128;
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

		char tag = buffer.front();
		std::string_view val{ buffer.c_str() + 1, buffer.size() - 1 };

		switch (tag) {
		case 'W':
			parse_uint(val, m_format.width);
			break;
		case 'H':
			parse_uint(val, m_format.height);
			break;
		case 'F':
			parse_rational(val, m_metadata.fpsnum, m_metadata.fpsden);
			break;
		case 'A':
			parse_rational(val, m_metadata.sarnum, m_metadata.sarden);
			break;
		case 'C':
			decode_color_format(val);
			have_c = true;
			break;
		case 'I':
			if (val == "p")
				m_metadata.fieldorder = 0;
			else if (val == "t")
				m_metadata.fieldorder = 1;
			else if (val == "b")
				m_metadata.fieldorder = 2;
			break;
		case 'X':
			decode_extension(val, have_yscss_error);
			break;
		default:
			break;
		}

		return eoh;
	}

	void read_header()
	{
		std::array<char, s_y4m_magic.size()> header{};
		m_io->read(header);
		if (to_sv(header) != s_y4m_magic)
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
		m_frameno{ -1 }
	{
		if (m_io->seekable())
			m_io->seek(0, IOStream::seek_set);

		read_header();

		if (!is_valid_format(m_format))
			throw std::runtime_error{ "incomplete Y4M header" };

		m_offset = m_io->tell();
		m_packet_size = s_frame_magic.size() + planar_frame_size(m_format);
		m_frameno = 0;
	}

	int64_t framecount() const noexcept override
	{
		if (m_io->seekable())
			return (m_io->length() - m_offset) / m_packet_size;
		else
			return 0;
	}

	rawz_metadata metadata() const noexcept override { return m_metadata; }

	void read(int64_t n, void * const planes[4], const ptrdiff_t stride[4]) override try
	{
		seek_to_frame(m_io.get(), m_frameno, n, m_packet_size, m_offset);

		std::array<char, s_frame_magic.size()> header{};
		m_io->read(header);
		if (to_sv(header) == s_frame_magic_bad)
			throw std::runtime_error{ "Y4M frame properties not supported" };
		if (to_sv(header) != s_frame_magic)
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
