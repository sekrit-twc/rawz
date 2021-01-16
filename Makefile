MY_CFLAGS := -O2 -fPIC $(CFLAGS)
MY_CXXFLAGS := -std=c++14 -O2 -fPIC $(CXXFLAGS)
MY_CPPFLAGS := -Ilibp2p -Irawz -Ivsxx $(CPPFLAGS)
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
	libp2p/p2p.h

vsxx_HDRS = \
	vsxx/VapourSynth.h \
	vsxx/VapourSynth++.hpp \
	vsxx/VSScript.h \
	vsxx/VSHelper.h \
	vsxx/vsxx_pluginmain.h

all: vsrawz.so

vsrawz.so: vsrawz/vsrawz.o vsxx/vsxx_pluginmain.o $(rawz_OBJS)
	$(CXX) -shared $(MY_LDFLAGS) $^ $(MY_LIBS) -o $@

clean:
	rm -f *.a *.o *.so rawz/*.o vsrawz/*.o vsxx/*.o

%.o: %.cpp $(p2p_HDRS) $(rawz_HDRS) $(vsxx_HDRS)
	$(CXX) -c $(EXTRA_CXXFLAGS) $(MY_CXXFLAGS) $(MY_CPPFLAGS) $< -o $@

.PHONY: clean
