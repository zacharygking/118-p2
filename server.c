/* 
 * udpserver.c - A simple UDP echo server 
 * usage: udpserver <port>
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFSIZE 1024
#define WINDOWSIZE 5120
#define MAXSEQ 30720

int recievedACK[30];

struct SendArgs {
	int fd;
	int seq;
	int len;
	char buff[1024];
};

void error(char *msg) {
	perror(msg);
	exit(1);
}

void* sendPacket(void* args) {
	struct SendArgs* test = (struct SendArgs *) args;
	*(test->buff + 50) = '\0';
	printf("%d, %d, %d, %s\n", test->seq, test->fd, test->len, test->buff);
	
}

int main(int argc, char **argv) {
  int sockfd; /* socket */
  int portno; /* port to listen on */
  int clientlen; /* byte size of client's address */
  struct sockaddr_in serveraddr; /* server's addr */
  struct sockaddr_in clientaddr; /* client addr */
  struct hostent *hostp; /* client host info */
  char buf[BUFSIZE]; /* message buf */
  char fileName[BUFSIZE];
  char *hostaddrp; /* dotted decimal host addr string */
  int optval; /* flag value for setsockopt */
  int n; /* message byte size */

  /* 
   * check command line arguments 
   */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  portno = atoi(argv[1]);

  /* 
   * socket: create the parent socket 
   */
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) 
    error("ERROR opening socket");

  /* setsockopt: Handy debugging trick that lets 
   * us rerun the server immediately after we kill it; 
   * otherwise we have to wait about 20 secs. 
   * Eliminates "ERROR on binding: Address already in use" error. 
   */
  optval = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
	     (const void *)&optval , sizeof(int));

  /*
   * build the server's Internet address
   */
  bzero((char *) &serveraddr, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serveraddr.sin_port = htons((unsigned short)portno);

  /* 
   * bind: associate the parent socket with a port 
   */
  if (bind(sockfd, (struct sockaddr *) &serveraddr, 
	   sizeof(serveraddr)) < 0) 
    error("ERROR on binding");

  /* 
   * main loop: wait for a datagram, then echo it
   */
  clientlen = sizeof(clientaddr);
  while (1) {
	// Init 
	int i;
	for (i = 0; i <= 30; i++) {
		recievedACK[i] = 0;
	}

    // Recieve File Request
    bzero(fileName, BUFSIZE);
    n = recvfrom(sockfd, fileName, BUFSIZE, 0,
		 (struct sockaddr *) &clientaddr, &clientlen);
    if (n < 0)
      error("ERROR in recvfrom");

    // Determine the Origin of the Request
    hostp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr, 
			  sizeof(clientaddr.sin_addr.s_addr), AF_INET);
    if (hostp == NULL)
      error("ERROR on gethostbyaddr");
    hostaddrp = inet_ntoa(clientaddr.sin_addr);
    if (hostaddrp == NULL)
      error("ERROR on inet_ntoa\n");
    printf("server received datagram from %s (%s)\n", 
	   hostp->h_name, hostaddrp);
    printf("server received %d/%d bytes: %s\n", strlen(fileName), n, fileName);
    
	// Open the Requested File and Read the Size
	FILE* file = fopen("data.txt", "rb");
	if (file == NULL) {
		perror("Error opening file");
		exit(5);
	}

	fseek(file, 0, SEEK_END);
	int fileSize = ftell(file);
	rewind(file);

	int windowStart = 0;
	int windowEnd = windowStart + WINDOWSIZE;
	int dataSent = 0;
	int currSeq = 0;

	pthread_t windowThreads[5];
	char windowData[1024 * 5];
	int windowLengths[5];
	int windowSeqs[5];

	struct SendArgs threadInfo[5];

	// Send the First Five Packets if Possible
	for (i = 0; i < 5; i++) {
		if (dataSent == fileSize) {
			// Done Sending the File
			break;
		} else {
			// Launch a thread to send the next segment
			threadInfo[i].fd = sockfd;
			threadInfo[i].len = fileSize - dataSent > 1022 ? 1022 : fileSize - dataSent;
			threadInfo[i].seq = currSeq;
			memset(threadInfo[i].buff, 0, 1024);
			fread(threadInfo[i].buff, sizeof(char), threadInfo[i].len, file);
			pthread_create(&windowThreads[i], NULL, sendPacket, &threadInfo[i]);
			currSeq += threadInfo[i].len + 2;
			dataSent += threadInfo[i].len;
		}
	}
	int seq = 0;
	while(dataSent < fileSize) {
		memset(buf, 0, 1024);
		n = recvfrom(sockfd, buf, BUFSIZE, 0, (struct sockaddr *) &clientaddr, &clientlen);
		printf("server received %d/%d bytes: %s", strlen(buf), n, buf);
		 
		if (seq == windowStart) {	
			windowStart += 1024;
			windowEnd += 1024;
			for(i = 0; i < 5; i++) {
				if (threadInfo[i].seq == seq) {
					threadInfo[i].seq = windowEnd;
					threadInfo[i].len = fileSize - dataSent > 1022 ? 1022 : fileSize - dataSent;
					memset(threadInfo[i].buff, 0, 1024);
					fread(threadInfo[i].buff, sizeof(char), threadInfo[i].len, file);
					pthread_create(&windowThreads[i], NULL, sendPacket, &threadInfo[i]);
					currSeq += threadInfo[i].len + 2;
					dataSent += threadInfo[i].len;
				}
			}
			seq += 1024;
		} else {
			recievedACK[seq/1024] = 1;
		}
	}
	printf("finished sending file\n");
  }
}