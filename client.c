/* 
 * udpclient.c - A simple UDP client
 * usage: udpclient <host> <port>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <inttypes.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BUFSIZE 1024
#define MAX_DATA_SIZE 1022
/* 
 * error - wrapper for perror
 */
void error(char *msg) {
    perror(msg);
    exit(0);
}

int main(int argc, char **argv) {
    int sockfd, portno, n;
    int serverlen;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    char *hostname;
    char *file_temp;
    int frame[31];
    int WND_SIZE = 5;
    int seq_min = 0;
    int seq_max = 0 + WND_SIZE;
    char file_name[BUFSIZE + 1];
    short seq_num , real_seq_num;
    int datafile_fd;
    char buf[1022];
    char data[31][MAX_DATA_SIZE + 1];
      
    /* check command line arguments */
    if (argc != 4) {
       fprintf(stderr,"usage: %s <hostname> <port> <data file>\n", argv[0]);
       exit(0);
    }

    hostname = argv[1];
    portno = atoi(argv[2]);
    file_temp = argv[3];

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* gethostbyname: get the server's DNS entry */
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", hostname);
        exit(0);
    }

    /* open data file */
    datafile_fd = open("receive.jpg", O_TRUNC | O_CREAT | O_RDWR, 0644);
    if (datafile_fd == -1)
      error("Error opening receive.data");
    
    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
	  (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(portno);
	printf("Sending packet SYN\n");
    strncpy(file_name, file_temp, strlen(file_temp));
    file_name[strlen(file_name)] = '\0';

      
    serverlen = sizeof(serveraddr);

    n = sendto(sockfd, file_name, strlen(file_name), 0, &serveraddr, serverlen); /* send file name to server */
	for (i = 0; i < 31; i++) {
		frame[i] = 0;
	}
    while(1){ /* wait for server packets */
	
    	char buf[BUFSIZE];
		memset(buf, 0, 1024);
		int nRecieved = recvfrom(sockfd, &buf, BUFSIZE, 0, (struct sockaddr *)&serveraddr, &serverlen);
		if (nRecieved < 0) 
			error("ERROR in recvfrom\n");
	
		if (buf[2] == 'F' && buf[3] == 'I' && buf[4] == 'N') {
			printf("Sending packet FIN\n");
			close(datafile_fd);
			exit(1);
      	}
		char seq_temp[2];
    	strncpy(seq_temp, buf, 2); 
      
      	real_seq_num = (((short)seq_temp[0]) << 8) | seq_temp[1];
      	printf("Recieved Packet seq=%d\n", real_seq_num);
      
      	seq_num = real_seq_num/1024;
	  	printf("Sending ACK seq=%d\n", real_seq_num);
      	int n_s = sendto(sockfd, &seq_temp, 2, 0, (struct sockaddr *)&serveraddr, serverlen);
      	if (n_s < 0)
			error("ERROR in sendto\n");

      	if( ((seq_min < seq_max && seq_num < seq_max && seq_num >= seq_min) || (seq_min > seq_max && (seq_num >= seq_min || seq_num < seq_max)))
	 		&& !frame[seq_num]) {
		
			memset(data[seq_num], 0, MAX_DATA_SIZE);
			memcpy(data[seq_num], buf + 2, nRecieved - 2);

			if(seq_num != seq_min){
				frame[seq_num] = 1;
				continue;
			}
	
			int i;
			int num;
			int offset;
			if(seq_num == seq_min && seq_min > seq_max)
				offset = 31;
			else
				offset = 0;
			n = write(datafile_fd, &data[seq_num], nRecieved - 2);
			if (n < 0)
	  			perror("Error writing to receive.data\n");
			frame[seq_num] = 0;
			for(i = seq_min + 1; i < seq_max + offset; i++){
	  			num = i%31;
	 			if(frame[num] == 1){
	    			n = write(datafile_fd, &data[num], nRecieved - 2);
	    			frame[num] = 0;
	  			} else {
	    			break;
	  			} 
			}

			seq_min = i % 31;
			seq_max = (seq_min + 5) % 31;
			bzero(buf, 1022);
	 	}
    } 
}