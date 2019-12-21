CLANG_PREFIX = ${HOME}/src/clang/build

CXX = ${CLANG_PREFIX}/bin/clang++ -std=c++2a -stdlib=libc++ \
			-I${CLANG_PREFIX}/include \
			-fimplicit-modules -fimplicit-module-maps \
			-fmodules-cache-path=build \
			-fprebuilt-module-path=build \
			-Wall -Wextra -pedantic

.PHONY: default opt debug all clean
.PRECIOUS: build/build.o

default: debug

BASE_CXXFLAGS = -g3
DEBUG_CXXFLAGS = -fmodules-cache-path=build/debug  \
								 -fprebuilt-module-path=build/debug
OPT_CXXFLAGS = -fmodules-cache-path=build/opt  \
							 -fprebuilt-module-path=build/opt  \
							 -Ofast -ffunction-sections -fdata-sections -flto -DNDEBUG

BASE_LDFLAGS = -L${CLANG_PREFIX}/lib -Wl,-rpath,${CLANG_PREFIX}/lib
DEBUG_LDFLAGS =
OPT_LDFLAGS = #-Wl,--gc-sections -s

clean:
	rm -rf bin build

MKBMI = ${CXX} -Xclang -emit-module-interface

bin bin/opt bin/debug build build/opt build/debug:
	mkdir -p $@

build/debug/%.pcm: | build/debug
	${MKBMI} ${BASE_CXXFLAGS} ${DEBUG_CXXFLAGS} -c $< -o $@

build/opt/%.pcm: | build/opt
	${MKBMI} ${BASE_CXXFLAGS} ${OPT_CXXFLAGS} -c $< -o $@

build/debug/%.o: | build/debug
	${CXX} ${BASE_CXXFLAGS} ${DEBUG_CXXFLAGS} -c $< -o $@

build/opt/%.o: | build/opt
	${CXX} ${BASE_CXXFLAGS} ${OPT_CXXFLAGS} -c $< -o $@

bin/debug/%: build/debug/%.o | bin/debug
	${CXX} ${BASE_CXXFLAGS} ${DEBUG_CXXFLAGS} $^ ${BASE_LDFLAGS}  \
		     ${DEBUG_LDFLAGS} -o $@

bin/opt/%: build/opt/%.o | bin/opt
	${CXX} ${BASE_CXXFLAGS} ${OPT_CXXFLAGS} $^ ${BASE_LDFLAGS}  \
		     ${OPT_LDFLAGS} -o $@

build/debug/build.o: src/build.cc

-include build/depends.mk

build/depends.mk: bin/debug/build $(shell find src -name '*.cc')
	$< > $@
