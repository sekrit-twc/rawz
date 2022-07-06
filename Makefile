MY_CFLAGS := -O2 -fPIC $(CFLAGS)
MY_CXXFLAGS := -std=c++14 -O2 -fPIC -fvisibility=hidden $(CXXFLAGS)
MY_CPPFLAGS := -DP2P_USER_NAMESPACE=p2p_rawz -Ilibp2p -Irawz -Ivsxx -Ivsxx/vapoursynth $(CPPFLAGS)
MY_LDFLAGS := $(LDFLAGS)
MY_LIBS := $(LIBS)

rawz_HDRS = \
	rawz/checked_int.h \
	rawz/common.h \
	rawz/io.h \
	rawz/rawz.h \
	rawz/stream.h

rawz_OBJS = \
	rawz/interleaved.o \
	rawz/io.o \
	rawz/nv.o \
	rawz/planar.o \
	rawz/rawz.o \
	rawz/stream.o \
	rawz/y4m.o

p2p_HDRS = \
	libp2p/p2p.h \
	libp2p/simd/cpuinfo_x86.h \
	libp2p/simd/p2p_simd.h

p2p_OBJS = \
	libp2p/simd/cpuinfo_x86.o \
	libp2p/simd/p2p_simd.o \
	libp2p/simd/p2p_sse41.o \
	libp2p/v210.o

vsxx_HDRS = \
	vsxx/vapoursynth/VapourSynth4.h \
	vsxx/vapoursynth/VSConstants4.h \
	vsxx/vapoursynth/VSHelper4.h \
	vsxx/VapourSynth4++.hpp \
	vsxx/vsxx4_pluginmain.h

ifeq ($(X86), 1)
  MY_CPPFLAGS := -DP2P_SIMD $(MY_CPPFLAGS)
  libp2p/simd/p2p_sse41.o: EXTRA_CXXFLAGS := -msse4.1
endif

all: vsrawz.so

vsrawz.so: vsrawz/vsrawz.o vsxx/vsxx4_pluginmain.o $(p2p_OBJS) $(rawz_OBJS)
	$(CXX) -shared $(MY_LDFLAGS) $^ $(MY_LIBS) -o $@

clean:
	rm -f *.a *.o *.so libp2p/*.o libp2p/simd/*.o rawz/*.o vsrawz/*.o vsxx/*.o

%.o: %.cpp $(p2p_HDRS) $(rawz_HDRS) $(vsxx_HDRS)
	$(CXX) -c $(EXTRA_CXXFLAGS) $(MY_CXXFLAGS) $(MY_CPPFLAGS) $< -o $@

.PHONY: clean
