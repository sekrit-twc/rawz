#pragma once

#ifndef RAWZ_COMMON_H_
#define RAWZ_COMMON_H_

#include <cstddef>

namespace rawz {

template <class T>
T ceil_aligned(T val, unsigned log2alignment)
{
	T alignment = static_cast<T>(1) << log2alignment;
	return (val + (alignment - 1)) & ~(alignment - 1);
}

template <class T>
T subsampled_dim(T val, unsigned log2subsampling)
{
	T subsampling = static_cast<T>(1) << log2subsampling;
	return (val >> log2subsampling) + (val % subsampling ? 1 : 0);
}

template <class T>
T *advance_ptr(T ptr, ptrdiff_t stride)
{
	return (T *)((const unsigned char *)ptr + stride);
}

} // namespace rawz

#endif // RAWZ_COMMON_H_
