#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include "rawz.h"
#include "vsxx_pluginmain.h"

using namespace std::string_literals;
using namespace vsxx;

namespace {

struct RawzIOStreamClose {
	void operator()(rawz_io_stream *ptr) { rawz_io_stream_close(ptr); }
};

struct RawzVideoStreamFree {
	void operator()(rawz_video_stream *ptr) { rawz_video_stream_free(ptr); }
};

[[noreturn]] void rawz_rethrow_exception()
{
	throw std::runtime_error{ rawz_get_last_error() };
}

typedef std::unique_ptr<rawz_io_stream, RawzIOStreamClose> rawz_io_stream_ptr;
typedef std::unique_ptr<rawz_video_stream, RawzVideoStreamFree> rawz_video_stream_ptr;


unsigned int64_to_uint(int64_t x)
{
	if (x < 0 || x > static_cast<int64_t>(UINT_MAX))
		throw std::runtime_error{ "integer out of bounds" };
	return static_cast<unsigned>(x);
}

int check_uint(unsigned x)
{
	if (x > static_cast<unsigned>(INT_MAX))
		throw std::runtime_error{ "integer out of bounds" };
	return x;
}

std::pair<int64_t, int64_t> normalize_rational(int64_t num, int64_t den)
{
	vs_normalizeRational(&num, &den);
	if (den < 0) {
		num = num == INT64_MIN ? INT64_MAX : -num;
		den = den == INT64_MIN ? INT64_MAX : -den;
	}
	return{ num, den };
}


const std::unordered_map<std::string, rawz_packing_mode> g_packing_mode_table{
	{ "argb",  RAWZ_ARGB },
	{ "rgba",  RAWZ_RGBA },
	{ "rgb",   RAWZ_RGB },
	{ "rgb30", RAWZ_RGB30 },
	{ "nv",    RAWZ_NV },
	{ "yuyv",  RAWZ_YUYV },
	{ "uyvy",  RAWZ_UYVY },
	{ "v210",  RAWZ_V210 },
};

} // namespace


class SourceFilter : public FilterBase {
	enum class Y4MMode {
		AUTO = 0,
		FORCE = 1,
		DISABLE = 2,
	};

	rawz_video_stream_ptr m_stream;
	VideoFrame m_prop_holder;
	VSVideoInfo m_vi;
	bool m_alpha;

	void init_format(const rawz_format &formatz, bool rgb, const VapourCore &core)
	{
		VSVideoInfo vi{};
		VSColorFamily cm = cmGray;
		VSSampleType st = formatz.floating_point ? stFloat : stInteger;
		int bits_per_sample = formatz.bits_per_sample;
		int subsample_w = 0;
		int subsample_h = 0;

		vi.width = check_uint(formatz.width);
		vi.height = check_uint(formatz.height);

		if (formatz.planes_mask & 0x6) {
			subsample_w = check_uint(formatz.subsample_w);
			subsample_h = check_uint(formatz.subsample_h);
			if (subsample_w > 2 || subsample_h > 2)
				throw std::runtime_error{ "subsampling >4x not supported" };

			if (vi.width & ((1 << subsample_w) - 1)) {
				unsigned subsample_w_ratio = 1 << subsample_w;
				unsigned padded_w = (static_cast<unsigned>(vi.width) + (subsample_w_ratio - 1)) &
					~static_cast<unsigned>(subsample_w_ratio - 1);
				vi.width = check_uint(padded_w);
			}
			if (vi.height & ((1 << subsample_h) - 1)) {
				unsigned subsample_h_ratio = 1 << subsample_h;
				unsigned padded_h = (static_cast<unsigned>(vi.height) + (subsample_h_ratio - 1)) &
					~static_cast<unsigned>(subsample_h_ratio - 1);
				vi.height = check_uint(padded_h);
			}

			cm = rgb ? cmRGB : cmYUV;
		} else {
			cm = cmGray;
		}

		if (!formatz.bytes_per_sample || formatz.bytes_per_sample > 4)
			throw std::runtime_error{ "invalid bit depth" };

		int bytes_per_sample = formatz.bytes_per_sample;
		if (bits_per_sample <= (bytes_per_sample - 1) * 8 || bits_per_sample > bytes_per_sample * 8)
			bits_per_sample = formatz.bytes_per_sample * 8;

		vi.format = core.register_format(cm, st, bits_per_sample, subsample_w, subsample_h);
		vi.fpsNum = 25;
		vi.fpsDen = 1;
		vi.numFrames = int64ToIntS(rawz_video_stream_framecount(m_stream.get()));
		m_vi = vi;
	}

	void init_metadata(const rawz_metadata &metadata, const VapourCore &core)
	{
		m_prop_holder = core.new_video_frame(*core.format_preset(pfGray8), 1, 1);
		PropertyMapRef props = m_prop_holder.frame_props();

		if (metadata.sarnum && metadata.sarden) {
			auto val = normalize_rational(metadata.sarnum, metadata.sarden);
			if (val.first > 0) {
				props.set_prop("_SARNum", val.first);
				props.set_prop("_SARDen", val.second);
			}
		}

		if (metadata.fpsnum && metadata.fpsden) {
			auto val = normalize_rational(metadata.fpsnum, metadata.fpsden);
			if (val.first > 0) {
				m_vi.fpsNum = val.first;
				m_vi.fpsDen = val.second;
			}
		}

		if (metadata.fullrange == 0)
			props.set_prop("_ColorRange", 1);
		else if (metadata.fullrange == 1)
			props.set_prop("_ColorRange", 0);

		if (metadata.fieldorder >= 0 && metadata.fieldorder <= 2)
			props.set_prop("_FieldBased", metadata.fieldorder);

		if (metadata.chromaloc >= 0 && metadata.chromaloc <= 5)
			props.set_prop("_ChromaLocation", metadata.chromaloc);
	}
public:
	SourceFilter(void * = nullptr) : m_vi(), m_alpha{} {}

	const char *get_name(int) noexcept override
	{
		return "Source";
	}

	std::pair<VSFilterMode, int> init(const ConstPropertyMap &in, const PropertyMap &out, const VapourCore &core) override
	{
		std::string path = in.get_prop<std::string>("source");
		Y4MMode y4m_mode = static_cast<Y4MMode>(in.get_prop<int>("y4m", map::Ignore{}));
		bool y4m = y4m_mode == Y4MMode::FORCE;

		// Check for Y4M file.
		if (y4m_mode == Y4MMode::AUTO) {
			size_t idx = path.rfind('.');
			std::string ext = idx == std::string::npos ? ""s : path.substr(idx);
			std::transform(ext.begin(), ext.end(), ext.begin(), static_cast<int(*)(int)>(std::tolower));
			if (ext == ".y4m")
				y4m = true;
		}

		rawz_format formatz;
		bool rgb = false;

		rawz_format_default(&formatz);

		if (y4m) {
			formatz.mode = RAWZ_Y4M;
		} else {
			if (in.contains("packing")) {
				std::string key = in.get_prop<std::string>("packing");
				auto it = g_packing_mode_table.find(key);
				if (it == g_packing_mode_table.end())
					throw std::runtime_error{ "unknown packing mode: " + key };
				formatz.mode = it->second;
			} else {
				formatz.mode = RAWZ_PLANAR;
			}

			formatz.width = int64_to_uint(in.get_prop<int64_t>("width"));
			formatz.height = int64_to_uint(in.get_prop<int64_t>("height"));

			const VSFormat *format = core.format_preset(static_cast<VSPresetFormat>(in.get_prop<int>("format")));
			if (!format)
				throw std::runtime_error{ "unregistered format" };

			formatz.planes_mask = format->numPlanes == 3 ? 0x7 : 0x1;
			formatz.subsample_w = format->subSamplingW;
			formatz.subsample_h = format->subSamplingH;
			formatz.bytes_per_sample = format->bytesPerSample;
			formatz.bits_per_sample = format->bitsPerSample;
			formatz.floating_point = format->sampleType == stFloat;
			rgb = format->colorFamily == cmRGB;

			formatz.alignment = in.get_prop<int>("alignment", map::Ignore{});
			if (formatz.alignment > 12)
				throw std::runtime_error{ "too much alignment" };
		}

		int64_t offset = in.get_prop<int64_t>("offset", map::Ignore{});
		offset = std::max(offset, static_cast<int64_t>(0));
		rawz_io_stream_ptr io{ rawz_io_open_file(path.c_str(), 1, offset) };
		if (!io)
			rawz_rethrow_exception();

		m_stream.reset(rawz_video_stream_create(io.release(), &formatz));
		if (!m_stream)
			rawz_rethrow_exception();

		init_format(formatz, rgb, core);
		if (!isConstantFormat(&m_vi))
			throw std::runtime_error{ "unsupported or incomplete format" };
		if (formatz.planes_mask & (1U << 3) && in.get_prop<bool>("_Alpha", map::Ignore{}))
			m_alpha = true;

		rawz_metadata metadata{};
		rawz_video_stream_metadata(m_stream.get(), &metadata);
		if (metadata.fpsnum < 0 && metadata.fpsden < 0) {
			metadata.fpsnum = in.get_prop<int64_t>("fpsnum", map::Ignore{});
			metadata.fpsden = in.get_prop<int64_t>("fpsden", map::Ignore{});
		}
		if (metadata.sarnum < 0 && metadata.sarden < 0) {
			metadata.sarnum = in.get_prop<int64_t>("sarnum", map::Ignore{});
			metadata.sarden = in.get_prop<int64_t>("sarden", map::Ignore{});
		}
		init_metadata(metadata, core);

		return{ fmUnordered, 0 };
	}

	std::pair<const VSVideoInfo *, size_t> get_video_info() noexcept override
	{
		return{ &m_vi, 1 };
	}

	ConstVideoFrame get_frame_initial(int n, const VapourCore &core, VSFrameContext *frame_ctx) override
	{
		return get_frame(n, core, frame_ctx);
	}

	ConstVideoFrame get_frame(int n, const VapourCore &core, VSFrameContext *frame_ctx) override
	{
		VideoFrame frame = core.new_video_frame(*m_vi.format, m_vi.width, m_vi.height, m_prop_holder);
		VideoFrame alpha;
		void *planes[4] = {};
		ptrdiff_t stride[4] = {};

		for (int p = 0; p < m_vi.format->numPlanes; ++p) {
			planes[p] = frame.write_ptr(p);
			stride[p] = frame.stride(p);
		}
		if (m_alpha) {
			const VSFormat *alpha_fmt = core.register_format(
				cmGray, static_cast<VSSampleType>(m_vi.format->sampleType), m_vi.format->bitsPerSample,
				m_vi.format->subSamplingW, m_vi.format->subSamplingH);
			alpha = core.new_video_frame(*alpha_fmt, m_vi.width, m_vi.height);
			planes[3] = alpha.write_ptr(0);
			stride[3] = alpha.stride(0);
		}

		if (rawz_video_stream_read(m_stream.get(), n, planes, stride))
			rawz_rethrow_exception();
		if (alpha)
			frame.frame_props().set_prop("_Alpha", std::move(alpha));

		return frame;
	}
};


const PluginInfo g_plugin_info = {
	"who.you.gonna.call.when.they.come.for.you", "rawz", "VapourSynth Raw Source", {
		{ &FilterBase::filter_create<SourceFilter>, "Source",
			"source:data;width:int:opt;height:int:opt;format:int:opt;"
			"packing:data:opt;offset:int:opt;alignment:int:opt;y4m:int:opt;alpha:int:opt;"
			"fpsnum:int:opt;fpsden:int:opt;sarnum:int:opt;sarden:int:opt;" }
	}
};
