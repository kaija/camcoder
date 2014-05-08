
CFLAGS= -Inoly
CFLAGS+= -I./include
#CFLAGS+=-Werror
CFLAGS+=-Wall

LDFLAGS= noly/libnoly.a
LDFLAGS+= libs/libavformat.a
LDFLAGS+= libs/libavfilter.a
LDFLAGS+= libs/libavcodec.a
LDFLAGS+= libs/libswscale.a
LDFLAGS+= libs/libavutil.a
LDFLAGS+= -lm
LDFLAGS+= -lpthread
LDFLAGS+= -lz

all: noly camcoder

noly:
	$(MAKE) -C noly

camcoder: camcoder.o
	$(CC) -o camcoder.exe camcoder.o $(CFLAGS) $(LDFLAGS)

clean:
	rm -rf *.o *.exe
