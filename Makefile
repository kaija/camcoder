
CFLAGS= -Inoly
CFLAGS+= -I./include
#CFLAGS+=-Werror
CFLAGS+=-Wall
CFLAGS+=-DDEBUG

LDFLAGS= noly/libnoly.a
LDFLAGS+= libs/libavformat.a
LDFLAGS+= libs/libavfilter.a
LDFLAGS+= libs/libavcodec.a
LDFLAGS+= libs/libavutil.a
LDFLAGS+= libs/libswscale.a
LDFLAGS+= -lm
LDFLAGS+= -lpthread
LDFLAGS+= -lz
.PHONY: noly camcoder test
all: noly camcoder test

noly:
	$(MAKE) -C noly

camcoder: camcoder.o
	$(CC) -o camcoder.exe camcoder.o $(CFLAGS) $(LDFLAGS)

test: test.o
	$(CC) -o test.exe test.o $(CFLAGS) $(LDFLAGS)

clean:
	rm -rf *.o *.exe
