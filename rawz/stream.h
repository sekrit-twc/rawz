#pragma once

#ifndef RAWZ_STREAM_H_
#define RAWZ_STREAM_H_

#include <cstddef>
#include <memory>
#include "rawz.h"

struct rawz_video_stream {
protected:
	~rawz_video_stream() = default;
};


namespace rawz {

class IOStream;

class VideoStream : public rawz_video_stream {
public:
	VideoStream &operator=(const VideoStream &) = delete;

	virtual int64_t framecount() const noexcept = 0;

	virtual rawz_metadata metadata() const noexcept = 0;

	virtual void read(int n, void * const planes[4], const ptrdiff_t stride[4]) = 0;
};


rawz_metadata default_metadata();

bool is_valid_format(const rawz_format &format);

size_t planar_frame_size(const rawz_format &format);

void blit_plane(IOStream *io, unsigned width, unsigned height, unsigned bytes_per_sample, unsigned alignment, void *dst, ptrdiff_t stride);

void blit_planar_frame(IOStream *io, const rawz_format &format, void * const planes[4], const ptrdiff_t stride[4]);


std::unique_ptr<VideoStream> create_planar_stream(std::unique_ptr<IOStream> io, const rawz_format *format);

inline std::unique_ptr<VideoStream> create_nv_stream(std::unique_ptr<IOStream> io, rawz_format *format)
{
	throw 1;
}

std::unique_ptr<VideoStream> create_interleaved_stream(std::unique_ptr<IOStream> io, rawz_format *format);

std::unique_ptr<VideoStream> create_y4m_stream(std::unique_ptr<IOStream> io, rawz_format *format);

} // namespace rawz

#endif // RAWZ_STREAM_H_
