all: vector.c
	gcc -c -Wall -fPIC vector.c -lyaslapi
	gcc -shared -o libvector.so vector.o -lyaslapi
