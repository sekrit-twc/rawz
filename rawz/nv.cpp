#include <cstdint>
#include <stdexcept>
#include <vector>
#include "checked_int.h"
#include "common.h"
#include "io.h"
#include "stream.h"

namespace rawz {

namespace {

typedef void (*deinterleave_func)(const void *src, void *u, void *v, size_t n);

template <class T>
void deinterleave(const void *src, void *u, void *v, size_t n)
{
	const T *srcp = static_cast<const T*>(src);
	T *u_ptr = static_cast<T *>(u);
	T *v_ptr = static_cast<T *>(v);

	for (size_t i = 0; i < n; ++i) {
		T u_val = *srcp++;
		T v_val = *srcp++;
		if (u_ptr)
			*u_ptr++ = u_val;
		if (v_ptr)
			*v_ptr++ = v_val;
	}
}


class NVVideoStream : public VideoStream {
	std::unique_ptr<IOStream> m_io;
	rawz_format m_format;
	deinterleave_func m_deinterleave;
	size_t m_chroma_row_size;
	uint64_t m_packet_size;
	int64_t m_frameno;

	void init_deinterleave()
	{
		switch (m_format.bytes_per_sample) {
		case 1:
			m_deinterleave = deinterleave<uint8_t>;
			break;
		case 2:
			m_deinterleave = deinterleave<uint16_t>;
			break;
		case 4:
			m_deinterleave = deinterleave<uint32_t>;
			break;
		default:
			throw std::runtime_error{ "unsupported bit depth" };
		}
	}

	void calculate_packet_size()
	{
		size_t luma_width = m_format.width;
		size_t luma_height = m_format.height;

		checked_size_t luma_row_size = checked_size_t{ luma_width } * m_format.bytes_per_sample;
		luma_row_size = ceil_aligned(luma_row_size, m_format.alignment);

		size_t chroma_width = subsampled_dim(m_format.width, m_format.subsample_w);
		size_t chroma_height = subsampled_dim(m_format.height, m_format.subsample_h);

		checked_size_t chroma_row_size = checked_size_t{ chroma_width } * m_format.bytes_per_sample * 2U;
		chroma_row_size = ceil_aligned(chroma_row_size, m_format.alignment);

		checked_size_t sz = luma_row_size * luma_height + chroma_row_size * chroma_height;
		m_packet_size = sz.get();
	}

	void blit_nv_plane(void *u, void *v, ptrdiff_t stride_u, ptrdiff_t stride_v)
	{
		unsigned width = subsampled_dim(m_format.width, m_format.subsample_w);
		unsigned height = subsampled_dim(m_format.height, m_format.subsample_h);

		std::vector<uint8_t> buffer(m_chroma_row_size);

		for (unsigned i = 0; i < height; ++i) {
			m_io->read(buffer.data(), buffer.size());
			m_deinterleave(buffer.data(), u, v, width);

			if (u)
				u = advance_ptr(u, stride_u);
			if (v)
				v = advance_ptr(v, stride_v);
		}
	}
public:
	NVVideoStream(std::unique_ptr<IOStream> io, const rawz_format &format) :
		m_io{ std::move(io) },
		m_format(format),
		m_deinterleave{},
		m_chroma_row_size{},
		m_packet_size{},
		m_frameno{ -1 }
	{
		if (!is_valid_format(format))
			throw std::runtime_error{ "invalid format" };

		init_deinterleave();
		calculate_packet_size();
	}

	int64_t framecount() const noexcept
	{
		return m_io->seekable() ? m_io->length() / m_packet_size : 0;
	}

	rawz_metadata metadata() const noexcept { return default_metadata(); }

	void read(int64_t n, void * const planes[4], const ptrdiff_t stride[4]) try
	{
		seek_to_frame(m_io.get(), m_frameno, n, m_packet_size);

		if (planes[0])
			blit_plane(m_io.get(), m_format.width, m_format.height, m_format.bytes_per_sample, m_format.alignment, planes[0], stride[0]);
		else
			skip_plane(m_io.get(), m_format.width, m_format.height, m_format.bytes_per_sample, m_format.alignment);

		blit_nv_plane(planes[1], planes[2], stride[1], stride[2]);
		++m_frameno;
	} catch (...) {
		m_frameno = -1;
		throw;
	}

	const rawz_format &format() const { return m_format; }
};

} // namespace


std::unique_ptr<VideoStream> create_nv_stream(std::unique_ptr<IOStream> io, rawz_format *format)
{
	std::unique_ptr<NVVideoStream> stream = std::make_unique<NVVideoStream>(std::move(io), *format);
	*format = stream->format();
	return std::move(stream);
}

} // namespace rawz
