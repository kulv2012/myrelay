CC = gcc
CFLAGS = -I ../include/ -O2 -fPIC
OBJECT = common.o conf.o dict.o genpool.o handler.o hash.o iprange.o log.o md5.o sock.o timer.o

libop.so	:	$(OBJECT)
	gcc -o libop.so -shared -fPIC $(OBJECT)

common.o	:	common.c ../include/common.h
	gcc -c common.c $(CFLAGS)

conf.o	:	conf.c ../include/conf.h ../include/dict.h ../include/log.h ../include/common.h
	gcc -c conf.c $(CFLAGS)

dict.o	:	dict.c ../include/dict.h ../include/list.h ../include/hash.h
	gcc -c dict.c $(CFLAGS)

genpool.o	:	genpool.c ../include/genpool.h ../include/list.h ../include/log.h
	gcc -c genpool.c $(CFLAGS)

handler.o	:	handler.c ../include/log.h ../include/handler.h
	gcc -c handler.c $(CFLAGS)

hash.o	:	hash.c ../include/hash.h
	gcc -c hash.c $(CFLAGS)

iprange.o	:	iprange.c ../include/iprange.h ../include/common.h ../include/log.h
	gcc -c iprange.c $(CFLAGS)

log.o	:	log.c ../include/log.h
	gcc -c log.c $(CFLAGS)

md5.o	:	md5.c ../include/md5.h
	gcc -c md5.c $(CFLAGS)

sock.o	:	sock.c ../include/sock.h ../include/log.h ../include/common.h
	gcc -c sock.c $(CFLAGS)

timer.o	:	timer.c ../include/timer.h ../include/log.h
	gcc -c timer.c $(CFLAGS)

install	: libop.so
	cp libop.so ../lib/

clean 	:
	-rm -f $(OBJECT)

.PHONY	: install clean
