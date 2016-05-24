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
#ifdef __APPLE__
#define ApplePositive 1
#include <mach/semaphore.h>
#include <mach/task.h>
#include <mach/mach_init.h>
#else
#include <semaphore.h>
#endif

#define BUFSIZE 8096
#define OperationMode 2

char * defaultPath;

int ftp(int fd, int hit);

void getFunction(int fd, char * fileName);

void putFunction(int fd, char * fileName);

char * listFilesDir(char * dirName);

void lsFunction(int fd, char * dirName);

void mgetFunction(int fd, char *dirName);

void cdFunction(int fd, char * dirName);

void cdResetFunction(int fd);

#if OperationMode
typedef struct {
	int * fd;
	int hit;
} THREAD_ARGS;

#if OperationMode == 2
typedef struct
{
	int *buff;
	size_t buffSize;
	int in;		/* first empty slot */
	int out;	/* first full slot */
#if ApplePositive
	semaphore_t full;
	semaphore_t empty;
#else
	sem_t full;
	sem_t empty;
#endif
	pthread_mutex_t mutex;
} CONSUMER_STRUCT;

CONSUMER_STRUCT shared;

void initConsumerStruct(CONSUMER_STRUCT *cs, size_t buffSize);

void *Consumer(void *arg);

void onAlarm(int signum);

void printBuffer();
#endif

void *attendFTP(void *);
#endif

/* just checks command line arguments, setup a listening socket and block on accept waiting for clients */

int main(int argc, char **argv) {
	int i, port, pid, listenfd, socketfd, hit, consumers;
	socklen_t length;
	static struct sockaddr_in cli_addr; /* static = initialised to zeros */
	static struct sockaddr_in serv_addr; /* static = initialised to zeros */

#if OperationMode == 2
	if (argc < 5 || argc > 5 || !strcmp(argv[1], "-?")) {
		printf("\n\nhint: ./tftps Port-Number Top-Directory Consumer-Threads Buffer-Size\n\n""\ttftps is a small and very safe mini ftp server\n""\tExample: ./tftps 8181 ./fileDir \n\n");
		exit(0);
	}
	consumers = atoi(argv[3]);

	initConsumerStruct(&shared, (size_t) atoi(argv[4]));
#else
	if (argc < 3 || argc > 3 || !strcmp(argv[1], "-?")) {
		printf("\n\nhint: ./tftps Port-Number Top-Directory\n\n""\ttftps is a small and very safe mini ftp server\n""\tExample: ./tftps 8181 ./fileDir \n\n");
		exit(0);
	}
#endif
	port = atoi(argv[1]);
	defaultPath = argv[2];

	if (chdir(defaultPath) == -1) {
		printf("ERROR: Can't Change to directory %s\n", argv[2]);
		exit(4);
	}

	printf("LOG tftps starting %s - pid %d\n", argv[1], getpid());

	/* setup the network socket */
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		printf("ERROR system call - setup the socket\n");
	if (port < 0 || port > 60000)
		printf("ERROR Invalid port number (try 1->60000)\n");

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(port);

	if (bind(listenfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
		printf("ERROR system call - bind error\n");
	if (listen(listenfd, 64) < 0)
		printf("ERROR system call - listen error\n");

#if OperationMode == 2
	pthread_t idC;
	int index = 0, item;

#if ApplePositive
    semaphore_create(mach_task_self(), &shared.full, SYNC_POLICY_FIFO, 0);
    semaphore_create(mach_task_self(), &shared.empty, SYNC_POLICY_FIFO, shared.buffSize);
#else
	sem_t *semFull = sem_open("/semFull", O_CREAT, 0644, 0);
	sem_t *semEmpty = sem_open("/semEmpty", O_CREAT, 0644, shared.buffSize);

	shared.full = *semFull;
	shared.empty = *semEmpty;
#endif

	pthread_mutex_init(&shared.mutex, NULL);

	/*create a new Consumer*/
	for(index=0; index<consumers; index++)
	{
		pthread_create(&idC, NULL, Consumer, (void*)&index);
	}

	struct sigaction act;
	act.sa_handler = &onAlarm;
	act.sa_flags = SA_RESTART;  // Restart interrupted system calls
	sigaction(SIGALRM, &act, NULL);

	alarm(30);
#endif

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
#if OperationMode == 1
			pthread_t thread_id;

			THREAD_ARGS *args = malloc(sizeof(THREAD_ARGS));

			int * sockAUX = (int *) malloc(sizeof(int *));

			*sockAUX = socketfd;

			args->fd = sockAUX;
			args->hit = hit;

			if (args != NULL) {
				if (pthread_create(&thread_id, NULL, &attendFTP, args)) {
					perror("could not create thread");
					return 1;
				}
			}
#else
			item = socketfd;

#if ApplePositive
            semaphore_wait(shared.empty);
#else
			sem_wait(&shared.empty);
#endif
            pthread_mutex_lock(&shared.mutex);

			shared.buff[shared.in] = item;

			pthread_mutex_unlock(&shared.mutex);

			shared.in = (shared.in + 1) % shared.buffSize;

#if ApplePositive
			semaphore_signal(shared.full);
#else
			sem_post(&shared.full);
#endif
#endif
#else
			pid = fork();
                if(pid==0) {
                    ftp(socketfd, hit);
                    exit(0);
                } else {
                    //Temos de fechar o socketfd para que seja apenas a child a tratar dos pedidos, caso contrário iria ficar aqui pendurado
                    close(socketfd);
                    signal(pid, SIGCHLD);
                }
#endif
		}
	}
}

#if OperationMode == 1
void *attendFTP(void *argp) {
	THREAD_ARGS *args = argp;

	int sock = *args->fd;

	ftp(sock, args->hit);

	free(args->fd);

	free(args);

	pthread_exit(NULL);

	return NULL;
}
#endif

#if OperationMode == 2

void initConsumerStruct(CONSUMER_STRUCT *cs, size_t buffSize) {
	cs->buff = (int *) calloc(buffSize, sizeof(int));
	cs->buffSize = buffSize;
}

void *Consumer(void *arg)
{
	int fd, hit=0;

	for (;;) {
#if ApplePositive
		semaphore_wait(shared.full);
#else
		sem_wait(&shared.full);
#endif
		pthread_mutex_lock(&shared.mutex);
		fd = shared.buff[shared.out];
		shared.buff[shared.out] = 0;
		shared.out = (shared.out+1)%shared.buffSize;
		/* Release the buffer */
		pthread_mutex_unlock(&shared.mutex);
		ftp(fd, hit);
		/* Increment the number of full slots */
#if ApplePositive
		semaphore_signal(shared.empty);
#else
		sem_post(&shared.empty);
#endif
		hit++;
	}
	return NULL;
}

void onAlarm(int signum) {
	printBuffer();
	alarm(30);
}

void printBuffer() {
	int i = 0;
	printf("\n\n\n\nEstado do buffer:\n");
	pthread_mutex_lock(&shared.mutex);
	for (i = 0; i < (int) shared.buffSize; i++) {
		printf("%d ", shared.buff[i]);
	}
	pthread_mutex_unlock(&shared.mutex);
	printf("\n\n\n");
}
#endif

/* this is the ftp server function */
int ftp(int fd, int hit) {
	int j, file_fd, filedesc;
	long i, ret, len;
	char * fstr;
	char buffer[BUFSIZE + 1] = {0};

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
	/*for (i = 4; i < BUFSIZE; i++) {
		if (buffer[i] == ' ') { // string is "GET URL " +lots of other stuff
			buffer[i] = 0;
			break;
		}
	}*/

	if (!strncmp(buffer, "get ", 4)) {
		// GET
		if((buffer[5] == '.' && buffer[6] == '.') || buffer[5] == '|') {
			sprintf(buffer, "%s", "negado");
			write(fd,buffer,BUFSIZE);
			close(fd);
		} else {
			getFunction(fd, &buffer[4]);
		}
	} else if (!strncmp(buffer, "put ", 4)) {
		// PUT
		putFunction(fd,&buffer[5]);
	} else if (!strncmp(buffer, "ls ", 3)) {
		// LS
		lsFunction(fd,&buffer[3]);
	} else if (!strncmp(buffer, "mget ", 4)) {
		// MGET
		mgetFunction(fd, &buffer[5]);
	} else if (!strncmp(buffer, "cd ", 3)) {
		// CD
		cdFunction(fd,&buffer[3]);
	} else if (!strncmp(buffer, "reset ", 5)) {
		// RESET CD
		cdResetFunction(fd);
	}

	sleep(1); /* allow socket to drain before signalling the socket is closed */
	close(fd);
	return 0;
}

void getFunction(int fd, char * fileName){
	int file_fd;
	long ret;

	/*static*/ char buffer[BUFSIZE + 1] = {0}; /* static so zero filled */

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

	/*static*/ char buffer[BUFSIZE + 1] = {0}; /* static so zero filled */

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

void cdFunction(int fd, char * dirName){

	printf("CD -> LOG Header %s \n", dirName);

	static char buffer[BUFSIZE + 1];

	if (chdir(dirName) == -1) {
		sprintf(buffer, "ERROR: Can't Change to directory %s\n", dirName);
		write(fd,buffer,BUFSIZE);
	} else {
		sprintf(buffer, "Remote dir is now: %s\n", dirName);
		write(fd,buffer,BUFSIZE);
	}
}

void cdResetFunction(int fd) {

	printf("CD -> LOG Header %s \n", defaultPath);

	static char buffer[BUFSIZE + 1];

	if (chdir(defaultPath) == -1) {
		sprintf(buffer, "ERROR: Can't Change to base directory\n");
		write(fd,buffer,BUFSIZE);
	} else {
		sprintf(buffer, "Remote dir is now reseted\n");
		write(fd,buffer,BUFSIZE);
	}
}
