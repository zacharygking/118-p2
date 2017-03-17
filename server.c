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
#include <signal.h>
#include <time.h>
#include <sys/time.h>

#define BUFSIZE 1024
#define WINDOWSIZE 5120
#define MAXSEQ 30720


int recievedACK[30];

struct SendArgs {
	int fd;
	short seq;
	int len;
	char buff[1024];
	const struct sockaddr *dest_addr;
	socklen_t serverlen
};


void error(char *msg) {
	perror(msg);
	exit(1);
}


void* sendPacket(void* args) {
	struct SendArgs data = *(struct SendArgs *) args;
	struct timeval sent, check, result;

	if (data.len == 0)
		return;

	char packet[1024];
	packet[0] = (data.seq >> 8) & 0xFF;
	packet[1] = data.seq & 0xFF;
	memcpy(packet + 2, data.buff, data.len);
	printf("sending packet seq=%d, len=%d\n", data.seq, data.len);
	int n = sendto(data.fd, packet, data.len + 2, 0, data.dest_addr, data.serverlen);
	if (n < 0)
		error("Error sending packet.\n");
	gettimeofday(&sent, NULL);

	while (1) {
		// If the ACK has been recieved return
		if (recievedACK[data.seq/1024] == 1) {
			printf("Thread Suicide: %d\n", data.seq);
			return 0;
		}

		// If Timeout is Reached, Resend the Request
		gettimeofday(&check, NULL);
		timersub(&check, &sent, &result);
		if (result.tv_usec / 1000 > 500) {
			printf("resending packet seq=%d, len=%d\n", data.seq, data.len);
			int n = sendto(data.fd, packet, data.len + 2, 0, data.dest_addr, data.serverlen);
			if (n < 0)
				error("Error sending packet.\n");
			gettimeofday(&sent, NULL);
		}

	}
	
}

int main(int argc, char **argv) {
	int sockfd; 
	int portno; 
	int clientlen; 
	struct sockaddr_in serveraddr;
	struct sockaddr_in clientaddr; 
	struct hostent *hostp; 
	char buf[BUFSIZE]; 
	char fileName[BUFSIZE];
	char *hostaddrp; 
	int optval; 
	int n; 

	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}
	portno = atoi(argv[1]);

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) 
	error("ERROR opening socket");

	optval = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
			(const void *)&optval , sizeof(int));

	//build the server's Internet address
	bzero((char *) &serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons((unsigned short)portno);

	
	//bind: associate the parent socket with a port 
	if (bind(sockfd, (struct sockaddr *) &serveraddr, 
		sizeof(serveraddr)) < 0) 
	error("ERROR on binding");

	 
	// Continuously Service Files
	clientlen = sizeof(clientaddr);
	while (1) {

		// Init  
		int i;
		for (i = 0; i < 30; i++)
			recievedACK[i] = 1;
		short windowStart = 0;
		short windowEnd = windowStart + WINDOWSIZE;
		long dataSent = 0;
		short currSeq = 0;
		int activeThreads, err;

		pthread_t windowThreads[5];

		struct SendArgs threadInfo[5];


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

		// Open the Requested File and Read the Size
		printf("server received %d/%d bytes: %s\n", strlen(fileName), n, fileName);
		if (fileName[strlen(fileName) - 1] == '\n')
			fileName[strlen(fileName) - 1] = '\0';

		FILE* file = fopen(fileName, "rb");
		if (file == NULL) {
			error("Error opening file");
		}

		fseek(file, 0, SEEK_END);
		long fileSize = ftell(file);
		rewind(file);

		// Send the First Five Packets if Possible
		for (i = 0; i < 5; i++) {
			if (dataSent == fileSize) { 
				break; // Done sending the file
			} else {
				// Launch a thread to send the next segment
				threadInfo[i].fd = sockfd;
				threadInfo[i].len = fileSize - dataSent > 1022 ? 1022 : fileSize - dataSent;
				threadInfo[i].seq = currSeq;
				threadInfo[i].dest_addr = &clientaddr;
				threadInfo[i].serverlen = clientlen;
				memset(threadInfo[i].buff, 0, 1024);
				fread(threadInfo[i].buff, sizeof(char), threadInfo[i].len, file);
											
				recievedACK[threadInfo[i].seq/1024] = 0;
				err = pthread_create(&windowThreads[i], NULL, sendPacket, &threadInfo[i]);
				if (err != 0)
					error("Error creating thread\n");

				currSeq += threadInfo[i].len + 2;
				dataSent += threadInfo[i].len;
			}
		}	

		int seq = 0;
		// Send the Rest of the File While Shifting Window
		while(dataSent < fileSize) {

			memset(buf, 0, 1024);
			n = recvfrom(sockfd, buf, BUFSIZE, 0, (struct sockaddr *) &clientaddr, &clientlen);

			if (buf[strlen(buf) - 1] == '\n')
				buf[strlen(buf) - 1] = '\0';
			short ack = (((short)buf[0]) << 8) | buf[1];
			printf("recieved ack: %d\n", ack);
			int ackNum = (((short)buf[0]) << 8) | buf[1];
			if (ackNum < 0 || ackNum > 30720)
				error("Invalid ACK number");
			recievedACK[ackNum/1024] = 1;

			if (ackNum == windowStart) {
				// Launch the New Segment(s) and Advance the Window
				while (recievedACK[windowStart/1024]) {
					for(i = 0; i < 5; i++) {
						if (threadInfo[i].seq == windowStart) {
							threadInfo[i].seq = windowEnd;
							threadInfo[i].len = fileSize - dataSent > 1022 ? 1022 : fileSize - dataSent;
							threadInfo[i].dest_addr = &clientaddr;
							threadInfo[i].serverlen = clientlen;
							memset(threadInfo[i].buff, 0, 1024);
							fread(threadInfo[i].buff, sizeof(char), threadInfo[i].len, file);

							recievedACK[threadInfo[i].seq/1024] = 0;
							pthread_kill(&windowThreads[i], SIGTERM);
							pthread_create(&windowThreads[i], NULL, sendPacket, &threadInfo[i]);
							currSeq += threadInfo[i].len + 2;
							dataSent += threadInfo[i].len;
						} 
					}
					windowStart = (windowStart == 30720 ? 0 : windowStart + 1024);
					windowEnd = (windowEnd == 30720 ? 0 : windowEnd + 1024);
				}
			} 
		}
		// See if there are more ACKS we are waiting for
		int sum = 0;
		for (i = 0; i < 30; i++) {
			sum += recievedACK[i];
		}
		printf("Waiting for %d more ACKS.\n", 30 - sum);
		// Wait for the rest of the ACKS
		while (sum != 30) {

			sum = 0;
			memset(buf, 0, 1024);
			n = recvfrom(sockfd, buf, BUFSIZE, 0, (struct sockaddr *) &clientaddr, &clientlen);

			if (buf[strlen(buf) - 1] == '\n')
				buf[strlen(buf) - 1] = '\0';
			
			short ack = (((short)buf[0]) << 8) | buf[1];
			printf("WAITING server received ACK %d\n", ack);
			recievedACK[ack/1024] = 1;
			for (i = 0; i < 5; i++) {
				if (threadInfo[i].seq == ack) {
					pthread_kill(&windowThreads[i], SIGKILL);
				}
			}

			for (i = 0; i < 30; i++) {
				sum += recievedACK[i];
			}
		}
		printf("sending FIN\n");
		// All ACKs Recieved, Send FIN
		char packet[6];
		memset(packet, 0, 5);
		short seqNum = currSeq;
		packet[0] = (currSeq >> 8) & 0xFF;
		packet[1] = currSeq & 0xFF;
		packet[2] = 'F';
		packet[3] = 'I';
		packet[4] = 'N';

		int n = sendto(sockfd, packet, strlen(packet), 0, &clientaddr, clientlen);
		if (n < 0)
			error("Error sending packet.\n");

		// Wait for FIN ACK
		
		printf("Finished Sending File\n");
	}
}