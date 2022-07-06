#include <algorithm>
#include <stdexcept>
#include <vector>
#include <p2p.h>
#include "checked_int.h"
#include "common.h"
#include "io.h"
#include "stream.h"

namespace p2p = p2p_rawz;

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
	checked_size_t sz = checked_size_t{ width_div6 } * 16;
	return sz.get();
}


class InterleavedVideoStream : public VideoStream {
	std::unique_ptr<IOStream> m_io;
	rawz_format m_format;
	unpack_func m_unpack;
	size_t m_rowsize;
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

		checked_size_t rowsize;
		switch (m_format.mode) {
		case RAWZ_ARGB:
		case RAWZ_RGBA:
			rowsize = checked_size_t{ m_format.width } * m_format.bytes_per_sample * 4U;
			break;
		case RAWZ_RGB:
			rowsize = checked_size_t{ m_format.width } * m_format.bytes_per_sample * 3U;
			break;
		case RAWZ_RGB30:
			rowsize = checked_size_t{ m_format.width } * 4U;
			break;
		case RAWZ_YUYV:
		case RAWZ_UYVY:
			rowsize = checked_size_t{ m_format.width } * m_format.bytes_per_sample * 2U;
			break;
		case RAWZ_V210:
			rowsize = v210_rowsize(m_format.width);
			break;
		default:
			break;
		}
		rowsize = ceil_aligned(rowsize, m_format.alignment);

		m_rowsize = rowsize.get();
		m_packet_size = (rowsize * m_format.height).get();
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
		m_rowsize{},
		m_packet_size{},
		m_frameno{ -1 }
	{
		init_format();
		if (!is_valid_format(format))
			throw std::runtime_error{ "invalid format" };

		init_unpack();
	}

	int64_t framecount() const noexcept
	{
		return m_io->seekable() ? m_io->length() / m_packet_size : 0;
	}

	rawz_metadata metadata() const noexcept { return default_metadata(); }

	void read(int64_t n, void * const planes[4], const ptrdiff_t stride[4]) try
	{
		seek_to_frame(m_io.get(), m_frameno, n, m_packet_size);

		void *plane_ptrs[4] = { planes[0], planes[1], planes[2], planes[3] };
		std::vector<uint8_t> buffer(m_rowsize);
		unsigned height = m_format.height;
		unsigned vstep = 1U << m_format.subsample_h;

		std::vector<uint8_t> tmp;
		for (unsigned p = 0; p < 3; ++p) {
			// Only the alpha channel is optional when invoking p2p.
			if (!plane_ptrs[p]) {
				tmp.resize(m_format.width * m_format.bytes_per_sample);
				plane_ptrs[p] = tmp.data();
			}
		}

		for (unsigned i = 0; i < height; i += vstep) {
			m_io->read(buffer.data(), buffer.size());
			m_unpack(buffer.data(), plane_ptrs, 0, m_format.width);

			for (unsigned p = 0; p < 4; ++p) {
				if (!plane_ptrs[p])
					continue;

				unsigned plane_vstep = is_chroma_plane(p) ? 1 : vstep;
				plane_ptrs[p] = advance_ptr(plane_ptrs[p], stride[p] * static_cast<ptrdiff_t>(plane_vstep));
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
