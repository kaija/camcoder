
CFLAGS= -Inoly
CFLAGS+= -I./include
#CFLAGS+=-Werror
CFLAGS+=-Wall
CFLAGS+=-DDEBUG
CFLAGS+=-DCOLOR_LOG

LDFLAGS= noly/libnoly.a
LDFLAGS+= libs/libavformat.a
LDFLAGS+= libs/libavfilter.a
LDFLAGS+= libs/libavcodec.a
LDFLAGS+= libs/libavutil.a
LDFLAGS+= libs/libswscale.a
LDFLAGS+= libs/libswresample.a
#LDFLAGS+= -lavformat
#LDFLAGS+= -lavfilter
#LDFLAGS+= -lavcodec
#LDFLAGS+= -lavutil
#LDFLAGS+= -lswscale
LDFLAGS+= -lm
LDFLAGS+= -lpthread
LDFLAGS+= -lz
LDFLAGS+= -lrt
.PHONY: noly camcoder test
all: noly camcoder test

noly:
	$(MAKE) -C noly

camcoder: log.o camcoder.o
	$(CC) -o camcoder.exe log.o camcoder.o $(CFLAGS) $(LDFLAGS)

test: test.o
	$(CC) -o test.exe test.o $(CFLAGS) $(LDFLAGS)

clean:
	rm -rf *.o *.exe
