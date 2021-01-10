#include <stdexcept>
#include "io.h"
#include "stream.h"

namespace rawz {

namespace {

class PlanarVideoStream : public VideoStream {
	std::unique_ptr<IOStream> m_io;
	rawz_format m_format;
	uint64_t m_packet_size;
	int64_t m_frameno;
public:
	PlanarVideoStream(std::unique_ptr<IOStream> io, const rawz_format &format) :
		m_io{ std::move(io) },
		m_format(format),
		m_packet_size{},
		m_frameno{ -1 }
	{
		if (!is_valid_format(format))
			throw std::runtime_error{ "invalid format" };

		m_packet_size = planar_frame_size(m_format);
	}

	int64_t framecount() const noexcept
	{
		return m_io->seekable() ? m_io->length() / m_packet_size : 0;
	}

	rawz_metadata metadata() const noexcept { return default_metadata(); }

	void read(int64_t n, void * const planes[4], const ptrdiff_t stride[4]) try
	{
		seek_to_frame(m_io.get(), m_frameno, n, m_packet_size);
		blit_planar_frame(m_io.get(), m_format, planes, stride);
		++m_frameno;
	} catch (...) {
		m_frameno = -1;
		throw;
	}
};

} // namespace


std::unique_ptr<VideoStream> create_planar_stream(std::unique_ptr<IOStream> io, const rawz_format *format)
{
	return std::make_unique<PlanarVideoStream>(std::move(io), *format);
}

} // namespace rawz
