OBJ =  stdlib.o unistd.o time.o string.o read.o write.o open.o exit.o close.o fork.o errno.o exit.o unlink.o malloc.o stdio.o lseek.o wait.o signals.o setjmp.o dirent.o fcntl.o stat.o abort.o env.o ioctl.o ctype.o times.o strdup.o termios.o getopt.o  pwd.o asctime.o net.o socket.o inet.o netdb.o locale.o fnmatch.o  crti.o crtn.o mntent.o grp.o langinfo.o uname.o clock.o system.o
all: $(OBJ) math.o crt.a crt0.o crt1.o libc.a libm.a
	
	
%.o: %.c
	$(CC) -o $@ $<
	
crt1.o: crt0.c
	$(CC) -DSKIP_INIT_CALL -o crt1.o  crt0.c

crt.a: $(OBJ)  ../internal/heap.o ../internal/streams.o ../internal/setjmp.o ../internal/dirstreams.o ../internal/resolv.o ../internal/pwddb.o ../internal/math.o ../internal/exp.o ../internal/tan.o
	rm -f crt.a
	ar rc crt.a $(OBJ)  ../internal/heap.o ../internal/streams.o ../internal/setjmp.o ../internal/dirstreams.o ../internal/resolv.o ../internal/pwddb.o ../internal/math.o ../internal/exp.o ../internal/tan.o
	
crti.o: crti.S
	$(AS) -o crti.o crti.S
	
crtn.o: crtn.S
	$(AS) -o crtn.o crtn.S

	
libc.a: $(OBJ)
	rm -f libc.a
	ar rc libc.a $(OBJ) ../internal/heap.o ../internal/streams.o ../internal/setjmp.o ../internal/dirstreams.o ../internal/resolv.o ../internal/pwddb.o ../os/*.o ../internal/math.o ../internal/exp.o ../internal/tan.o

libm.a: math.o ../internal/math.o ../internal/exp.o ../internal/tan.o
	rm -f libm.a
	ar rc libm.a math.o ../internal/math.o  ../internal/exp.o ../internal/tan.o
		
clean:
	rm -f $(OBJ) crt0.o libc.a crt.a libm.a

include Makefile.dep

dep:
	gcc -m32 -M -iquote../../include *.c   > Makefile.dep
	
