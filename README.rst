Description
===========

Raw video reader library and VapourSynth frontend.

VapourSynth Usage
=================

::

  rawz.Source(string source, int "width", int "height", int "format",
    string "packing", string "offset", int "alignment", int "y4m",
    bint "alpha", int "fpsnum", int "fpsden", int "sarnum", int "sarden")

Parameters:
  *source*
    Path to raw file. If the path ends with ".y4m", it will be processed as a
    YUV4MPEG2 (Y4M) file. For Y4M files, no other parameters are required.

  *width*
    Width of luma plane. The VapourSynth clip will be rounded-up to the nearest
    multiple of the chroma subsampling ratio. For example, a 639x479 (4:2:0)
    source will produce a 640x480 clip. The padding pixels contain
    uninitialized values.
    
    Ignored for Y4M files. Required for other files.

  *height*
    Height of luma plane.

    Ignored for Y4M files. Required for other files.

  *format*
    VapourSynth format ID corresponding to the raw format. For interleaved
    raws, the format corresponds to the de-interleaved planar data.

    Ignored for Y4M files. Required for other files.

  *packing*
    Interleaving format. Takes precedence over the specified VapourSynth format
    in case of conflict. If not specified, the input is assumed to be planar.
  
    Supported packing modes:

    * **argb**:  32 or 64-bit word with 8 or 16-bit elements, with alpha channel in MSB
    * **rgba**:  Same as argb, but with alpha channel in LSB
    * **rgb**:   24 or 48-bit packed word. Channel order R-G-B
    * **rgb30**: D3D11 A2R10G10B10 bitfields
    * **nv**:    Biplanar with interleaved chroma samples
    * **yuyv**:  4:2:2 packing with sample sequence (byte-wise) Y-U-Y-V
    * **uyvy**:  Same as yuyv, but with alternate sequence U-Y-V-Y
    * **v210**:  ProRes v210

    Ignored for Y4M files.

  *offset*
    Offset to video data within file. This can be used to read pixel data from
    image formats such as BMP, EXR, TIFF, etc.

  *alignment*
    Log2 alignment for scanline. For example, BMP files are 4-byte aligned, so
    the appropriate value would be 2. v210 alignment is handled automatically.

  *y4m*
    Y4M handling mode:
    
    * -1: Disable Y4M
    * 0: Detected Y4M based on file extension (default)
    * 1: Force Y4M

  *alpha*
    If true, the alpha plane (e.g. argb, rgba, rgb30) is returned as an
    embedded video frame (_Alpha).
    
    Default: false

  *fpsnum*
    Framerate numerator. Overridden by embedded Y4M metadata.

    Default: 25

  *fpsden*
    Framerate denominator.
    
    Default: 1

  *sarnum*:
    Sample aspect ratio numerator. Overridden by embedded Y4M metadata.
    
    Default: 0 (undefined)
  
  *sarden*:
    SAR denominator.
    
    Default: 0

Other remarks:
  If the source has an alternative channel order (e.g. BGR, GBR), use
  std.ShufflePlanes after loading the raw::

    # GBRP file
    c = core.rawz.Source('myfile.bin', width=1, height=1, format=vs.RGB24)
    c = core.std.ShufflePlanes(c, [2, 0, 1], vs.RGB)

  If the source pixel values need some other adjustment (e.g. endianness or
  mixed-depth), use std.Lut::

    # Big-endian RGBP file
    c = core.rawz.Source('myfile.bin', width=1, height=1, format=vs.RGB48)
    c = core.std.Lut(c, function=lambda x: int(x >> 8) | ((x & 0xFF) << 8))
    c = core.std.ShufflePlanes(c, [2, 0, 1], vs.RGB)

    # 2-bit alpha from A2R10G10B10 file
    c = core.rawz.Source('myfile.bin', width=1, height=1, format=vs.RGB30, packing='rgb30')
    a = core.std.PropToClip(c)
    a = core.std.Lut(a, function=lambda x: min(x * 65535 / 3, 65535))


Compilation
===========

Be sure to fetch the submodules with `git submodule update --init`.

Use the Makefile.
