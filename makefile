all:	clean client tftps

client:	client.c
	gcc client.c -o client -pthread -pedantic
tftps:	tftps.c
	gcc tftps.c -o tftps -pthread -pedantic
clean:
	find . -maxdepth 1 -type f -not -name 'README.md' -not -name 'LICENSE' -not -name 'tftps.c' -not -name 'client.c' -not -name '.git*' -not -name 'CMakeLists.txt' -not -name 'makefile' -exec rm -rf {} \;
