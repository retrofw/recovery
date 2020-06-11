BUILDTIME := $(shell date +%s)

CC ?= $(CROSS_COMPILE)gcc
CXX ?= $(CROSS_COMPILE)g++
STRIP ?= $(CROSS_COMPILE)strip

SYSROOT     ?= $(shell $(CC) --print-sysroot)
SDL_CFLAGS  ?= $(shell $(SYSROOT)/usr/bin/sdl-config --cflags)
SDL_LIBS    ?= $(shell $(SYSROOT)/usr/bin/sdl-config --libs)

CFLAGS = -DTARGET_RETROFW -D__BUILDTIME__="$(BUILDTIME)" -DLOG_LEVEL=0 -g0 -Os $(SDL_CFLAGS) -mhard-float -mips32 -mno-mips16 -Isrc/
CFLAGS += -std=c++11 -fdata-sections -ffunction-sections -fno-exceptions -fno-math-errno -fno-threadsafe-statics

LDFLAGS = $(SDL_LIBS) -lfreetype -lSDL_image -lSDL_ttf -lSDL -lpthread -lpng
LDFLAGS +=-Wl,--as-needed -Wl,--gc-sections -s

all:
	echo 'const unsigned char _fatresize[] = {' > src/fatresize.h
	cat src/fatresize.sh | gzip | xxd -i >> src/fatresize.h
	echo '};' >> src/fatresize.h

	echo 'const unsigned char _opkscan[] = {' > src/opkscan.h
	cat src/opkscan.sh | gzip | xxd -i >> src/opkscan.h
	echo '};' >> src/opkscan.h

	$(CXX) $(CFLAGS) $(LDFLAGS) src/recovery.c -o retrofw

pc:
	gcc src/recovery.c -g -o retrofw -ggdb -O0 -DDEBUG -lSDL_image -lSDL -lSDL_ttf -I/usr/include/SDL

clean:
	rm -rf retrofw
