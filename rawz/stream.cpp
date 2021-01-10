#include "common.h"
#include "io.h"
#include "stream.h"

namespace rawz {

namespace {

constexpr unsigned MAX_PLANES = 4;

} // namespace


rawz_metadata default_metadata()
{
	rawz_metadata metadata{};
	metadata.fullrange = -1;
	metadata.fieldorder = -1;
	metadata.chromaloc = -1;
	return metadata;
}

bool is_valid_format(const rawz_format &format)
{
	// Missing width or height.
	if (!format.width || !format.height)
		return false;
	// Missing primary planes or too many planes.
	if ((format.planes_mask & ~0xF) || !(format.planes_mask & 0xF))
		return false;
	// Impossible bit depth.
	if (!format.bytes_per_sample || format.bits_per_sample > format.bytes_per_sample * 8)
		return false;
	// Impossible subsampling.
	if (format.subsample_w > 2 || format.subsample_h > 2)
		return false;
	return true;
}

size_t planar_frame_size(const rawz_format &format)
{
	size_t sz = 0;
	size_t alignment = static_cast<size_t>(1) << format.alignment;

	for (unsigned p = 0; p < MAX_PLANES; ++p) {
		if (!(format.planes_mask & (1U << p)))
			continue;

		size_t width = is_chroma_plane(p) ? subsampled_dim(format.width, format.subsample_w) : format.width;
		size_t height = is_chroma_plane(p) ? subsampled_dim(format.height, format.subsample_h) : format.height;
		size_t rowsize = ceil_aligned(width * format.bytes_per_sample, format.alignment);
		sz += rowsize * height;
	}

	return sz;
}

void blit_plane(IOStream *io, unsigned width, unsigned height, unsigned bytes_per_sample, unsigned alignment, void *dst, ptrdiff_t stride)
{
	size_t rowsize = static_cast<size_t>(width) * bytes_per_sample;
	size_t rowsize_aligned = ceil_aligned(rowsize, alignment);

	for (unsigned i = 0; i < height; ++i) {
		io->read(dst, rowsize);
		if (rowsize_aligned != rowsize)
			io->skip(rowsize_aligned - rowsize);

		dst = advance_ptr(dst, stride);
	}
}

void blit_planar_frame(IOStream *io, const rawz_format &format, void * const planes[4], const ptrdiff_t stride[4])
{
	unsigned alignment = 1U << format.alignment;

	for (unsigned p = 0; p < MAX_PLANES; ++p) {
		if (!planes[p] || !(format.planes_mask & (1U << p)))
			continue;

		unsigned width = is_chroma_plane(p) ? subsampled_dim(format.width, format.subsample_w) : format.width;
		unsigned height = is_chroma_plane(p) ? subsampled_dim(format.height, format.subsample_h) : format.height;
		blit_plane(io, width, height, format.bytes_per_sample, alignment, planes[p], stride[p]);
	}
}

} // namespace rawz
