#include "checked_int.h"
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
	// Too much alignment.
	if (format.alignment > 12)
		return false;
	return true;
}

size_t planar_frame_size(const rawz_format &format)
{
	checked_size_t sz = 0;
	size_t alignment = static_cast<size_t>(1) << format.alignment;

	for (unsigned p = 0; p < MAX_PLANES; ++p) {
		if (!(format.planes_mask & (1U << p)))
			continue;

		size_t width = is_chroma_plane(p) ? subsampled_dim(format.width, format.subsample_w) : format.width;
		size_t height = is_chroma_plane(p) ? subsampled_dim(format.height, format.subsample_h) : format.height;
		checked_size_t rowsize = ceil_aligned(checked_size_t{ width } * format.bytes_per_sample, format.alignment);
		sz += rowsize * height;
	}

	return sz.get();
}

void skip_plane(IOStream *io, unsigned width, unsigned height, unsigned bytes_per_sample, unsigned alignment)
{
	checked_size_t rowsize = checked_size_t{ width } * bytes_per_sample;
	checked_size_t rowsize_aligned = ceil_aligned(rowsize, alignment);
	checked_size_t n = rowsize_aligned * height;
	io->skip(n.get());
}

void blit_plane(IOStream *io, unsigned width, unsigned height, unsigned bytes_per_sample, unsigned alignment, void *dst, ptrdiff_t stride)
{
	checked_size_t rowsize = checked_size_t{ width } * bytes_per_sample;
	checked_size_t rowsize_aligned = ceil_aligned(rowsize, alignment);
	size_t diff = rowsize_aligned.get() - rowsize.get();

	for (unsigned i = 0; i < height; ++i) {
		io->read(dst, rowsize.get());
		if (diff)
			io->skip(diff);

		dst = advance_ptr(dst, stride);
	}
}

void blit_planar_frame(IOStream *io, const rawz_format &format, void * const planes[4], const ptrdiff_t stride[4])
{
	for (unsigned p = 0; p < MAX_PLANES; ++p) {
		if (!(format.planes_mask & (1U << p)))
			continue;

		unsigned width = is_chroma_plane(p) ? subsampled_dim(format.width, format.subsample_w) : format.width;
		unsigned height = is_chroma_plane(p) ? subsampled_dim(format.height, format.subsample_h) : format.height;

		if (planes[p])
			blit_plane(io, width, height, format.bytes_per_sample, format.alignment, planes[p], stride[p]);
		else
			skip_plane(io, format.width, height, format.bytes_per_sample, format.alignment);
	}
}

} // namespace rawz
