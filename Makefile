CC = gcc
CFLAGS = -g -I ./oplib/include/
OBJECT = cli_pool.o conn_pool.o main.o my_buf.o my_ops.o my_pool.o work.o my_protocol.o sqldump.o passwd.o sha1.o my_conf.o

all : $(OBJECT)
	make -C ./oplib/src/
	gcc -o myrelay $(OBJECT) ./oplib/src/libop.a

main.o	:	main.c cli_pool.h my_pool.h conn_pool.h my_conf.h
	gcc -c main.c $(CFLAGS)

cli_pool.o	:	cli_pool.c cli_pool.h my_buf.h conn_pool.h
	gcc -c cli_pool.c $(CFLAGS)

conn_pool.o	:	conn_pool.c conn_pool.h my_pool.h my_conf.h
	gcc -c conn_pool.c $(CFLAGS)

my_buf.o	:	my_buf.c my_buf.h
	gcc -c my_buf.c $(CFLAGS)

my_ops.o	:	my_ops.c my_ops.h my_buf.h mysql_com.h conn_pool.h my_pool.h cli_pool.h
	gcc -c my_ops.c $(CFLAGS)

my_protocol.o	:	my_protocol.c my_buf.h mysql_com.h
	gcc -c my_protocol.c $(CFLAGS)

my_pool.o	:	my_pool.c my_pool.h my_buf.h my_conf.h def.h
	gcc -c my_pool.c $(CFLAGS)

work.o	:	work.c my_ops.h conn_pool.h my_pool.h
	gcc -c work.c $(CFLAGS)

sqldump.o	:	sqldump.c sqldump.h conn_pool.h
	gcc -c sqldump.c $(CFLAGS)

passwd.o	:	passwd.c passwd.h sha1.h mysql_com.h
	gcc -c passwd.c $(CFLAGS)

sha1.o	:	sha1.c sha1.h
	gcc -c sha1.c $(CFLAGS)

my_conf.o	:	my_conf.c
	gcc -c my_conf.c $(CFLAGS)

install	: $(OBJECT)
	gcc -o myrelay $(OBJECT) -L ./oplib/src/ -lop

clean 	:
	-rm -f $(OBJECT)

.PHONY	: install clean all
