//
//  tftps.c
//  Adapted by Christophe Soares & Pedro Sobral on 15/16
//  Adapted by Rafael Almeida on 2016
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#define BUFSIZE 8096

int ftp(int fd, int hit);

void getFunction(int fd, char * fileName);

void putFunction(int fd, char * fileName);

void lsFunction(int fd, char * dirName);

void mgetFunction(int fd, char *dirName);

/* just checks command line arguments, setup a listening socket and block on accept waiting for clients */

int main(int argc, char **argv) {
	int i, port, pid, listenfd, socketfd, hit;
	socklen_t length;
	static struct sockaddr_in cli_addr; /* static = initialised to zeros */
	static struct sockaddr_in serv_addr; /* static = initialised to zeros */

	if (argc < 3 || argc > 3 || !strcmp(argv[1], "-?")) {
		printf("\n\nhint: ./tftps Port-Number Top-Directory\n\n""\ttftps is a small and very safe mini ftp server\n""\tExample: ./tftps 8181 ./fileDir \n\n");
		exit(0);
	}
	
	if (chdir(argv[2]) == -1) {
		printf("ERROR: Can't Change to directory %s\n", argv[2]);
		exit(4);
	}

	printf("LOG tftps starting %s - pid %d\n", argv[1], getpid());

	/* setup the network socket */
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		printf("ERROR system call - setup the socket\n");
	port = atoi(argv[1]);
	if (port < 0 || port > 60000)
		printf("ERROR Invalid port number (try 1->60000)\n");
		

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(port);
	
	if (bind(listenfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
		printf("ERROR system call - bind error\n");
	if (listen(listenfd, 64) < 0)
		printf("ERROR system call - listen error\n");


	// Main LOOP
	for (hit = 1 ;; hit++) {
		length = sizeof(cli_addr);
		/* block waiting for clients */
		socketfd = accept(listenfd, (struct sockaddr *) &cli_addr, &length);
		if (socketfd < 0)
			printf("ERROR system call - accept error\n");
		else
		{
			pid = fork();
			if(pid==0)
			{
				ftp(socketfd, hit);
			}
			else
			{
				//Temos de fechar o socketfd para que seja apenas a child a tratar dos pedidos, caso contrário iria ficar aqui pendurado
				close(socketfd);
				kill(pid, SIGCHLD);
			}
		}
	}
}

/* this is the ftp server function */
int ftp(int fd, int hit) {
	int j, file_fd, filedesc;
	long i, ret, len;
	char * fstr;
	static char buffer[BUFSIZE + 1]; /* static so zero filled */

	ret = read(fd, buffer, BUFSIZE); // read FTP request

	if (ret == 0 || ret == -1) { /* read failure stop now */
		close(fd);
		return 1;
	}
	if (ret > 0 && ret < BUFSIZE) /* return code is valid chars */
		buffer[ret] = 0; /* terminate the buffer */
	else
		buffer[0] = 0;

	for (i = 0; i < ret; i++) /* remove CF and LF characters */
		if (buffer[i] == '\r' || buffer[i] == '\n')
			buffer[i] = '*';

	printf("LOG request %s - hit %d\n", buffer, hit);

	/* null terminate after the second space to ignore extra stuff */
	for (i = 4; i < BUFSIZE; i++) {
		if (buffer[i] == ' ') { /* string is "GET URL " +lots of other stuff */
			buffer[i] = 0;
			break;
		}
	}

	if (!strncmp(buffer, "get ", 4)) {
		// GET
		getFunction(fd, &buffer[5]);
	} else if (!strncmp(buffer, "put ", 4)) {
		// PUT
		putFunction(fd,&buffer[5]);
	} else if (!strncmp(buffer, "ls ", 2)) {
		// LS
		lsFunction(fd,&buffer[3]);
	} else if (!strncmp(buffer, "mget ", 4)) {
		// MGET
		mgetFunction(fd, &buffer[5]);
	}

	sleep(1); /* allow socket to drain before signalling the socket is closed */
	close(fd);
	return 0;
}

void getFunction(int fd, char * fileName){
	int file_fd;
	long ret;

	static char buffer[BUFSIZE + 1]; /* static so zero filled */

	if ((file_fd = open(fileName, O_RDONLY)) == -1) { /* open the file for reading */
		printf("ERROR failed to open file %s\n", fileName);
		close(fd);
	}
	
	printf("GET -> LOG SEND %s \n", fileName);
	
	/* send file in 8KB block - last block may be smaller */
	while ((ret = read(file_fd, buffer, BUFSIZE)) > 0) {
		write(fd, buffer, ret);
	}
}

void putFunction(int fd, char * fileName){
	int file_fd;
	long ret;
	
	static char buffer[BUFSIZE + 1]; /* static so zero filled */
    
	printf("PUT -> LOG Header %s \n", fileName);

	file_fd = open(fileName, O_WRONLY | O_CREAT | O_TRUNC, 0777);
	if (file_fd == -1) {
		sprintf(buffer, "ERROR");
		write(fd, buffer, strlen(buffer));
	} else {
		sprintf(buffer, "OK");
		write(fd, buffer, strlen(buffer));

		while ((ret = read(fd, buffer, BUFSIZE)) > 0)
			write(file_fd, buffer, ret);
	}
}

void lsFunction(int fd, char * dirName){
	printf("LS -> LOG Header %s \n", dirName);

	dup2(fd, 1);
	dup2(fd, 2);

	if(strcmp(dirName,"")==0){
		execlp("ls", "ls", ".", NULL);
	} else {
		execlp("ls", "ls", dirName, NULL);
	}
}

void mgetFunction(int fd, char *dirName)
{
	FILE *fp;
    char path[255];
    printf("MGET COUNT -> LOG Header %s \n", dirName);

	if(strcmp(dirName,"")==0){
        strcpy(path,"find . -type f -maxdepth 1 | wc -l");
	} else {
        sprintf(path, "find %s -type f -maxdepth 1 | wc -l", dirName);
	}

	fp = popen(path, "r");

	while (fgets(path, sizeof(path), fp) != NULL) {
		write(fd, path, strlen(path));
	}

	dup2(fd, 1);
	dup2(fd, 2);

	if(strcmp(dirName,"")==0){
		strcpy(path,"find . -type f -maxdepth 1");
	} else {
		sprintf(path, "find %s -type f -maxdepth 1", dirName);
	}

	execlp("/bin/sh" , "sh", "-c", path, NULL);
}
