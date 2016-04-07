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
#include <sys/stat.h>
#include <dirent.h>
#include <pthread.h>

#define BUFSIZE 8096
#define OperationMode 1

#if OperationMode
	typedef struct {
		int fd;
		int hit;
	} THREAD_ARGS;

    void *attendFTP(void *);
#endif

int ftp(int fd, int hit);

void getFunction(int fd, char * fileName);

void putFunction(int fd, char * fileName);

char * listFilesDir(char * dirName);

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
			#if OperationMode            
                pthread_t thread_id;
    
                THREAD_ARGS *args = malloc(sizeof(THREAD_ARGS));

				int sockAUX = (int) malloc(sizeof(int));

				sockAUX = socketfd;

                args->fd = sockAUX;
                args->hit = hit;

                if (args != NULL) {
                    if (pthread_create(&thread_id, NULL, &attendFTP, args)) {
                        perror("could not create thread");
                        return 1;
                    }
                }
                
                //pthread_join(thread_id,NULL);
            #else
                pid = fork();
                if(pid==0) {
                    ftp(socketfd, hit);
                } else {
                    //Temos de fechar o socketfd para que seja apenas a child a tratar dos pedidos, caso contrário iria ficar aqui pendurado
                    close(socketfd);
                    kill(pid, SIGCHLD);
                }
            #endif
		}
	}
}

#if OperationMode
	void *attendFTP(void *argp) {
        THREAD_ARGS *args = argp;
        
        int sock = args->fd;

		printf("FD SOCK: %d\n\n", sock);

        ftp(sock, args->hit);

        free(args);
        //printf("Thread executou\n\n");
        pthread_exit(NULL);
        return NULL;
    }
#endif

/* this is the ftp server function */
int ftp(int fd, int hit) {
	int j, file_fd, filedesc;
	long i, ret, len;
	char * fstr;
	static char buffer[BUFSIZE + 1]; /* static so zero filled */

	printf("FD: %d\n\n", fd);

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
	} else if (!strncmp(buffer, "ls ", 3)) {
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

	printf("FD GET: %d\n\n", fd);

	static char buffer[BUFSIZE + 1]; /* static so zero filled */

	if ((file_fd = open(fileName, O_RDONLY)) == -1) { /* open the file for reading */
		printf("ERROR failed to open file %s\n", fileName);
        printf("Err: %d\n\n",errno);
        sprintf(buffer, "%s", "erro");
        write(fd,buffer,BUFSIZE);
		close(fd);
        return;
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
    
    static char buffer[BUFSIZE + 1];
    
    sprintf(buffer, "%s", listFilesDir(dirName));
    write(fd,buffer,BUFSIZE);
}

void mgetFunction(int fd, char *dirName)
{
	FILE *fp;
    char path[255];
    
    static char buffer[BUFSIZE + 1];
    
    printf("MGET COUNT -> LOG Header %s \n", dirName);
    
    sprintf(buffer, "%s", listFilesDir(dirName));
    write(fd,buffer,BUFSIZE);
}

char * listFilesDir(char * dirName)
{
    DIR *midir;
    struct dirent* info_archivo;
    struct stat fileStat;
    char fullpath[256];
    char *files = malloc (sizeof (char) * BUFSIZE);
    
    if ((midir=opendir(dirName)) == NULL)
    {
        return "\nO directorio pedido não existe.\n";
    }
    
    while ((info_archivo = readdir(midir)) != 0)
    {
        strcpy (fullpath, dirName);
        strcat (fullpath, "/");
        strcat (fullpath, info_archivo->d_name);
        if (!stat(fullpath, &fileStat))
        {
            if(!S_ISDIR(fileStat.st_mode))
            {
                strcat (files, info_archivo->d_name);
                strcat (files, "$$");
            }
        } else {
            return "\nErro ao ler o directório.\n";
        }
    }
    closedir(midir);
    
    return files;
}
