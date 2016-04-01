//
//  client.c
//  Adapted by Christophe Soares & Pedro Sobral on 15/16
//  Adapted by Rafael Almeida on 2016
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE 8096
#define OperationMode 0

#if OperationMode
	typedef struct {
		int fd;
		int hit;
	} THREAD_ARGS;
#endif

typedef struct {
    char **data;
    size_t used;
    size_t size;
} FILE_ARRAY;

int pexit(char * msg);

void getFunction(char * buffer, int sockfd, char * fileName);

void initFileArray(FILE_ARRAY *a, size_t initialSize);

void insertFileArray(FILE_ARRAY *a, char * element);

void freeFileArray(FILE_ARRAY *a);

int *downloadFlag = 0;

int main(int argc, char *argv[]) {
	int i, sockfd, filedesc;
	long long ret = 0, offset = 0;
	char buffer[BUFSIZE];
	static struct sockaddr_in serv_addr;

	struct stat stat_buf; // argument to fstat

	char fileName[50];

	if (argc != 5) {
		printf("Usage: ./client <SERVER IP ADDRESS> <LISTENING PORT> <COMMAND> <FILE/DIR>\n");
		printf("Example: ./client 127.0.0.1 8141 get remoteFileName\n./client 127.0.0.1 8141 [put/get/ls] [localFileName/dir]\n\n");
		exit(1);
	}

	printf("client trying to connect to IP = %s PORT = %s method= %s for FILE/DIR= %s\n", argv[1], argv[2], argv[3], argv[4]);

	strcpy(fileName, argv[4]);

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		pexit("socket() failed");

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
	serv_addr.sin_port = htons(atoi(argv[2]));

	// Connect tot he socket offered by the web server
	if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
		pexit("connect() failed");

	if (!strcmp(argv[3], "get")) {
        
        printf("\n\n-------------------------\n| A começar download... |\n-------------------------\n\n");

		getFunction(buffer, sockfd, fileName);
        
        printf("\n\n-----------------------------------\n| Download terminado com sucesso. |\n-----------------------------------\n\n");

	} else if (!strcmp(argv[3], "put")) {

		sprintf(buffer, "put /%s", fileName);
		printf("-> put /%s\n", fileName);

		write(sockfd, buffer, strlen(buffer));

		ret = read(sockfd, buffer, BUFSIZE); 	// read Web request in one go 
		buffer[ret] = 0; 						// put a null at the end

		if (ret > 0) {
			printf("<- %s\n", buffer);
			if (!strcmp(buffer, "OK")) { // check if it is OK on the ftp server side

				// open the file to be sent
				filedesc = open(fileName, O_RDWR);

				// get the size of the file to be sent
				fstat(filedesc, &stat_buf);

				// Read data from file and send it
				ret = 0;
				while (1) {
					unsigned char buff[BUFSIZE] = { 0 };
					int nread = read(filedesc, buff, BUFSIZE);
					ret += nread;
					printf("\nBytes read %d \n", nread);

					// if read was success, send data.
					if (nread > 0) {
						printf("Sending \n");
						write(sockfd, buff, nread);
					}
                    
					// either there was error, or we reached end of file.	
					if (nread < BUFSIZE) {
						if (ret == stat_buf.st_size)
							printf("End of file\n");
						else
							printf("Error reading\n");
						break;
					}
				}

				if (ret == -1) {
					fprintf(stderr, "error sending the file\n");
					exit(1);
				}
				if (ret != stat_buf.st_size) {
					fprintf(stderr,
							"incomplete transfer when sending: %lld of %d bytes\n",
							ret, (int) stat_buf.st_size);
					exit(1);
				}
			} else {
				printf("ERROR on the server");
			}

			// close descriptor for file that was sent
			close(filedesc);

			// close socket descriptor
			close(sockfd);
		}
    } else if (!strcmp(argv[3], "ls")) {
        char *token;
		
        sprintf(buffer, "ls %s", fileName);

		// Now the sockfd can be used to communicate to the server the LS request
		write(sockfd, buffer, strlen(buffer));
        
		while ((i = read(sockfd, buffer, BUFSIZE)) > 0)
        {
            token = strtok(buffer, "$$");
            
            while( token != NULL ) 
            {
                printf("%s\n", token);
                
                token = strtok(NULL, "$$");
            }
        }
    } else if (!strcmp(argv[3], "mget")) {
        char *token;
        FILE_ARRAY fileArray;
        int i, j, pid;
        struct timeval beginTime, endTime;
        unsigned long long int elapsedTime;
        
        gettimeofday(&beginTime, NULL);
        
        initFileArray(&fileArray,1);
		
		sprintf(buffer, "mget %s", fileName);

		// Now the sockfd can be used to communicate to the server the LS request
		write(sockfd, buffer, strlen(buffer));
        
		while ((i = read(sockfd, buffer, BUFSIZE)) > 0)
        {
            token = strtok(buffer, "$$");
            
            while( token != NULL ) 
            {
                //printf("%s\n", token);
                insertFileArray(&fileArray, token);
                
                token = strtok(NULL, "$$");
            }
        }
        
        int *pids = malloc(fileArray.used * sizeof(*pids));
        
        printf("\n\n--------------------------------------------------------\n| Foram encontrados %d ficheiros. A começar download... |\n--------------------------------------------------------\n\n", (int) fileArray.used);
        
        #if OperationMode
        
        
        #else
            for(j = 0;j < (int) fileArray.used; j++)
            {
                if ((pid = fork()) == -1) {
                    perror(argv[0]); exit(1);
                }
                
                if (pid == 0) {
                    int sockfdAux;
                    struct timeval beginTimeAux, endTimeAux;
                    unsigned long long int elapsedTimeAux;
                    
                    gettimeofday(&beginTimeAux, NULL);
                    
                    if ((sockfdAux = socket(AF_INET, SOCK_STREAM, 0)) < 0)
                        pexit("socket() failed");

                    // Connect tot he socket offered by the web server
                    if (connect(sockfdAux, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
                        pexit("connect() failed");
                    
                    getFunction(buffer, sockfdAux, fileArray.data[j]);
                    
                    close(sockfdAux);
                    
                    gettimeofday(&endTimeAux, NULL);
                    
                    elapsedTimeAux = (endTimeAux.tv_sec-beginTimeAux.tv_sec)*1000000 + endTimeAux.tv_usec-beginTimeAux.tv_usec;
                    
                    printf("O ficheiro pedido: %s foi recebido! Demorei %llu microsegundos.\n\n",fileArray.data[j], elapsedTimeAux);
                    
                    exit(j);
                } else {
                    pids[j] = pid;
                }
            }
            
            for(j = 0;j < (int) fileArray.used; j++)
            {
                int result;
                waitpid(pids[j], &result, 0);
            }
            
            freeFileArray(&fileArray);
        #endif
        
        printf("\n\n-----------------------------------\n| Download terminado com sucesso. |\n-----------------------------------\n\n");
                
        gettimeofday(&endTime, NULL);
        
        elapsedTime = (endTime.tv_sec-beginTime.tv_sec)*1000000 + endTime.tv_usec-beginTime.tv_usec;
                
        printf("Demorei %llu microsegundos.\n\n",elapsedTime);
	} else {
		// implement new methods
		printf("unsuported method\n");
	}
	return 1;
}

void getFunction(char * buffer, int sockfd, char * fileName)
{
	int i, filedesc;

	sprintf(buffer, "get /%s", fileName);

	// Now the sockfd can be used to communicate to the server the GET request
	write(sockfd, buffer, strlen(buffer));

	filedesc = open(fileName, O_WRONLY | O_CREAT | O_TRUNC, 0777);

	while ((i = read(sockfd, buffer, BUFSIZE)) > 0)
    {
        if(strcmp(buffer,"erro") == 0)
        {
            printf("Ocorreu um erro ao tentar abrir o ficheiro\n\n");
        } else {
            write(filedesc, buffer, i);
        }
    }
}

void initFileArray(FILE_ARRAY *a, size_t initialSize) {
    a->data = malloc(initialSize * sizeof(char *));
    if (a->data == NULL) {
        printf("ERROR: Memory allocation failure!\n");
        exit(1);
    }
    a->used = 0;
    a->size = initialSize;
}

void insertFileArray(FILE_ARRAY *a, char * element) {
    if(a->used == a->size) {
        void *pointer;

        a->size *= 2;
        pointer  = realloc(a->data, a->size * sizeof(char *));
        if (a->data == NULL) {
            freeFileArray(a);

            printf("ERROR: Memory allocation failure!\n");
            exit(1);
        }
        a->data = pointer;
    }
    /* if the string passed is not NULL, copy it */
    if (element != NULL) {
        size_t length;

        length           = strlen(element);
        a->data[a->used] = malloc(1 + length);
        if (a->data[a->used] != NULL)
            strcpy(a->data[a->used++], element);
    }
    else
        a->data[a->used++] = NULL;
}

void freeFileArray(FILE_ARRAY *a) {
    size_t i;
    /* Free all the copies of the strings */
    for (i = 0 ; i < a->used ; i++)
        free(a->data[i]);
    free(a->data);
    a = NULL;
    free(a);
}

int pexit(char * msg) {
	perror(msg);
	exit(1);
}
