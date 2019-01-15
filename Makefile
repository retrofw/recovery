CHAINPREFIX=/opt/mipsel-linux-uclibc
CROSS_COMPILE=$(CHAINPREFIX)/usr/bin/mipsel-linux-

BUILDTIME=$(shell date +'\"%Y-%m-%d %H:%M\"')

CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
STRIP = $(CROSS_COMPILE)strip

SYSROOT     := $(CHAINPREFIX)/usr/mipsel-buildroot-linux-uclibc/sysroot
SDL_CFLAGS  := $(shell $(SYSROOT)/usr/bin/sdl-config --cflags)
SDL_LIBS    := $(shell $(SYSROOT)/usr/bin/sdl-config --libs)

CFLAGS = -DTARGET_RS97 -DTARGET=$(TARGET) -D__BUILDTIME__="$(BUILDTIME)" -DLOG_LEVEL=0 -g0 -Os $(SDL_CFLAGS) -I$(CHAINPREFIX)/usr/include/ -I$(SYSROOT)/usr/include/  -I$(SYSROOT)/usr/include/SDL/ -mhard-float -mips32 -mno-mips16
CFLAGS += -std=c++11 -fdata-sections -ffunction-sections -fno-exceptions -fno-math-errno -fno-threadsafe-statics

LDFLAGS = $(SDL_LIBS) -lfreetype -lSDL_image -lSDL_ttf -lSDL -lpthread
LDFLAGS +=-Wl,--as-needed -Wl,--gc-sections -s

#gcc main.c -g -o S80recovery -DDEBUG -lSDL_image -lSDL -lSDL_ttf -I/usr/include/SDL

all:
	$(CXX) $(CFLAGS) $(LDFLAGS) main.c -o S80recovery
clean:
	rm -rf S80recovery
