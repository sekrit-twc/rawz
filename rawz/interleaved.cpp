#include <algorithm>
#include <stdexcept>
#include <vector>
#include <p2p.h>
#include "io.h"
#include "stream.h"

namespace rawz {

namespace {

typedef decltype(&p2p::packed_to_planar<p2p::packed_rgb24>::unpack) unpack_func;

template <class T>
unpack_func make_unpack(T t = {})
{
	return p2p::packed_to_planar<T>::unpack;
}


size_t v210_rowsize(unsigned width)
{
	// 6 pixels per 4 DWORDs.
	unsigned width_div6 = width / 6 + ((width % 6) ? 1 : 0);
	return static_cast<size_t>(width_div6) * 16;
}


class InterleavedVideoStream : public VideoStream {
	std::unique_ptr<IOStream> m_io;
	rawz_format m_format;
	unpack_func m_unpack;
	size_t m_row_size;
	uint64_t m_packet_size;
	int64_t m_frameno;

	void init_format()
	{
		if (m_format.mode == RAWZ_PLANAR || m_format.mode == RAWZ_Y4M || m_format.mode == RAWZ_NV)
			throw std::runtime_error{ "wrong reader type" };

		if (m_format.mode == RAWZ_ARGB || m_format.mode == RAWZ_RGBA)
			m_format.planes_mask = 0xF;
		else
			m_format.planes_mask = 0x7;

		if (m_format.mode == RAWZ_RGB30) {
			m_format.bytes_per_sample = 2;
			m_format.bits_per_sample = 10;
		}

		if (m_format.mode == RAWZ_YUYV || m_format.mode == RAWZ_UYVY || m_format.mode == RAWZ_V210) {
			m_format.subsample_w = 1;
			m_format.subsample_h = 0;
		} else {
			m_format.subsample_w = 0;
			m_format.subsample_h = 0;
		}

		if (m_format.mode == RAWZ_V210)
			m_format.alignment = std::max(m_format.alignment, 7U); // 128-byte aligned

		switch (m_format.mode) {
		case RAWZ_ARGB:
		case RAWZ_RGBA:
			m_row_size = static_cast<size_t>(m_format.width) * m_format.bytes_per_sample * 4;
			break;
		case RAWZ_RGB:
			m_row_size = static_cast<size_t>(m_format.width) * m_format.bytes_per_sample * 3;
			break;
		case RAWZ_RGB30:
			m_row_size = static_cast<size_t>(m_format.width) * 4;
			break;
		case RAWZ_YUYV:
		case RAWZ_UYVY:
			m_row_size = static_cast<size_t>(m_format.width) * m_format.bytes_per_sample * 2;
			break;
		case RAWZ_V210:
			m_row_size = v210_rowsize(m_format.width);
			break;
		default:
			break;
		}

		size_t alignment_factor = static_cast<size_t>(1) << m_format.alignment;
		m_row_size = (m_row_size + (alignment_factor - 1)) & ~(alignment_factor - 1);
		m_packet_size = static_cast<uint64_t>(m_row_size) * m_format.height;
	}

	void init_unpack()
	{
		if (m_format.bytes_per_sample > 2)
			throw std::runtime_error{ ">16-bit interleaved formats not supported" };

		switch (m_format.mode) {
		case RAWZ_ARGB:
			m_unpack = m_format.bytes_per_sample == 2 ? make_unpack(p2p::packed_argb64{}) : make_unpack(p2p::packed_argb32{});
			break;
		case RAWZ_RGBA:
			m_unpack = m_format.bytes_per_sample == 2 ? make_unpack(p2p::packed_rgba64{}) : make_unpack(p2p::packed_rgba32{});
			break;
		case RAWZ_RGB:
			m_unpack = m_format.bytes_per_sample == 2 ? make_unpack(p2p::packed_rgb48{}) : make_unpack(p2p::packed_rgb24{});
			break;
		case RAWZ_RGB30:
			m_unpack = make_unpack(p2p::packed_rgb30{});
			break;
		case RAWZ_YUYV:
			m_unpack = m_format.bytes_per_sample == 2 ? make_unpack(p2p::packed_y216{}) : make_unpack(p2p::packed_yuy2{});
			break;
		case RAWZ_UYVY:
			m_unpack = m_format.bytes_per_sample == 2 ? make_unpack(p2p::packed_v216{}) : make_unpack(p2p::packed_uyvy{});
			break;
		default:
			throw std::runtime_error{ "unsupported interleaving" };
		}
	}
public:
	InterleavedVideoStream(std::unique_ptr<IOStream> io, const rawz_format &format) :
		m_io{ std::move(io) },
		m_format(format),
		m_unpack{},
		m_row_size{},
		m_packet_size{},
		m_frameno{}
	{
		init_format();
		if (!is_valid_format(format))
			throw std::runtime_error{ "invalid format" };

		init_unpack();

		if (m_io->seekable())
			m_io->seek(0, IOStream::seek_set);
	}

	int64_t framecount() const noexcept
	{
		return m_io->seekable() ? m_io->length() / m_packet_size : 0;
	}

	rawz_metadata metadata() const noexcept { return default_metadata(); }

	void read(int n, void * const planes[4], const ptrdiff_t stride[4]) try
	{
		if (m_frameno == INT64_MAX)
			throw IOStream::eof{};

		if (n != m_frameno) {
			uint64_t pos = m_packet_size * static_cast<uint64_t>(n);
			if (pos > static_cast<uint64_t>(INT64_MAX))
				throw std::runtime_error{ "invalid file position" };

			m_io->seek(pos, IOStream::seek_set);
		}

		void *xplanes[4] = { planes[0], planes[1], planes[2], planes[3] };
		std::vector<uint8_t> buffer(m_row_size);
		unsigned height = m_format.height;
		unsigned vstep = 1U << m_format.subsample_h;

		for (unsigned i = 0; i < height; i += vstep) {
			m_io->read(buffer.data(), buffer.size());
			m_unpack(buffer.data(), xplanes, 0, m_format.width);

			for (unsigned p = 0; p < 4; ++p) {
				if (xplanes[p])
					xplanes[p] = static_cast<uint8_t *>(xplanes[p]) + stride[p] * static_cast<ptrdiff_t>(vstep);
			}
		}

		++m_frameno;
	} catch (...) {
		m_frameno = -1;
		throw;
	}

	const rawz_format &format() const { return m_format; }
};

} // namespace


std::unique_ptr<VideoStream> create_interleaved_stream(std::unique_ptr<IOStream> io, rawz_format *format)
{
	std::unique_ptr<InterleavedVideoStream> stream = std::make_unique<InterleavedVideoStream>(std::move(io), *format);
	*format = stream->format();
	return std::move(stream);
}

} // namespace rawz
