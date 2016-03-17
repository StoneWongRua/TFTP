all:	client tftps

client:	client.c
	gcc client.c -o client 
tftps:	tftps.c
	gcc tftps.c -o tftps 
clean:
	rm -rf client tftps
