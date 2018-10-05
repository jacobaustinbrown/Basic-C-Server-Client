#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/types.h>
#define port "10214"
#include "libnetfiles.h"
#include <netdb.h>


int fakeBool = 0;
int fileMode = 0;
struct addrinfo connector;
struct addrinfo * remote;

int netserverinit(char * hostname, int filemode){

	if(!(filemode==4||filemode==5||filemode==6)){
		printf("Please call netopen with a valid filemode: O_UNRESTRICTED, O_EXCLUSIVE, or O_TRANSACTIVE\nReturning, netclose failure\n");
		return -1;
	}
	memset(&connector, 0, sizeof(connector)); 
	connector.ai_family = AF_UNSPEC; // iLabs should be ipv4, but just in case
	connector.ai_socktype = SOCK_STREAM;
	int addrStatus = getaddrinfo(hostname, port, &connector, &remote);
	if(addrStatus!=0){	
		fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(addrStatus));
		printf("The above error offers more information, however, error: HOST_NOT_FOUND\n");
		return -1;
	}
	printf("Information successfully saved from hostname: %s\n", hostname);
	fakeBool=1;
	fileMode = filemode;
	return 0;

}

int netopen(const char* pathname, int flags){
	
	int sck = 0;

	if(fakeBool==0){
		printf("Error: cannot call netopen without first succesfully calling netserverinit\n");
		return -1;
	}
	int i = 0;
	while(pathname[i]!='\0'){ // calculating the length of pathname
		i++;
	}

	// printf("string size %d\n", i);
	char* buffer = malloc(i+10);
	memset(buffer, '\0', i+10);
	
	char mode;
	if(fileMode==4){ 
		mode='u';	// Unresticted
	}else if(fileMode==5){
		mode='e';	// Exclusive
	}else if(fileMode==6){
		mode='t';	// Transaction
	}
	buffer[0] = mode;

	if(flags==0){
		strcpy(buffer+1, "NETOPEN0");
	}else if(flags==1){
		strcpy(buffer+1, "NETOPEN1");
	}else if(flags==2){
		strcpy(buffer+1, "NETOPEN2");
	}else{
		printf("Not a valid flag, please use O_RDONLY, O_WRONLY, or O_RDWR\n");
		return -1;
	}
	
	strcpy(buffer+9, pathname);

	// open socket/connect
	// printf("Netopen Message: %s", buffer);
	sck = socket(AF_INET, SOCK_STREAM, 0);
	if(sck==-1){
		fprintf(stderr, "socket failed: %s\n", strerror(errno));
		return -1;
		//error
	}
	int con = connect(sck, remote->ai_addr, remote->ai_addrlen);	
	if(con==-1){
		fprintf(stderr, "connect failed: %s\n", strerror(errno));
		close(sck);
		return -1;
		//error, return -1
	}

	int buflen = strlen(buffer);
	send(sck, buffer, buflen, 0);
	char* netfd = malloc(31);  // Grab status of new file descriptor/its value
	memset(netfd, '\0', 31);
	recv(sck, netfd, 31, 0);
	// printf("from server: %s\n", netfd);

	i = 0;
	while(netfd[i]!='#'){
		i++;
	}

	char * number_written_string = malloc(31);
	memset(number_written_string, '\0', 31);
	memcpy(number_written_string, netfd, i);
	int num_written = atoi(number_written_string);

	if(num_written==1){ // Error checking, general undefined error
		printf("Netwrite has failed\n");
		return -1;
	}
	if(num_written==2){
		errno=13;
		printf("Netopen has failed, permission denied\n");
		return -1;
	}
	if(num_written==3){
		errno=4;
		printf("Netopen has failed, interrupted system call\n");
		return -1;
	}
	if(num_written==4){
		errno=21;
		printf("Netopen has failed, path points to a directory\n");
		return -1;
	}
	if(num_written==5){
		errno=2;
		printf("Netopen has failed, No such file or directory\n");
		return -1;
	}
	if(num_written==6){
		errno=30;
		printf("Netopen has failed, read only file system\n");
		return -1;
	}
	if(num_written==7){ // When a file cannot be opened because of monitor thread deactivating the request
		//set an appropriate errno
		errno=11; // Would block/ eagain
		h_errno = 111; //timeout
		printf("Netopen could not open the file because of conflicting open modes\nThe process timed out while waiting to be a valid file descriptor\n");
		return -1;
	}

	int holder = i;
	i++;
	while(netfd[i]!='#'){
		i++;
	}

	char* file_Descriptor = malloc(i); // Plenty of data to store fd
	memset(file_Descriptor, '\0', i);
	memcpy(file_Descriptor, netfd+holder+1, i-holder-1);
	int int_File_Descriptor = atoi(file_Descriptor);
	// printf("code: %d, file descriptor: %d\n", num_written, int_File_Descriptor);

	// printf("newstringsize, %d, string: %s\n", strlen(buffer), buffer);

	close(sck);
	return int_File_Descriptor;
}



ssize_t netwrite(int fildes, const void *buf, size_t nbyte){
	
	if(fildes==-1){
		printf("You just passed a fd that is -1, this is the failure response from netopen.\nFailure must have occured!\n");
		return -1;
	}
	if(fakeBool==0){
		printf("Error: cannot call netread without first succesfully calling netserverinit\n");
		return -1;
	}

	char sizeToRead[32];
	char fd[32];
	char* info = malloc(nbyte+1);
	memcpy(info, buf, nbyte);
	memset(info+nbyte, '\0', 1); 
	sprintf(fd, "%d", fildes);
	sprintf(sizeToRead, "%d", nbyte);
	int size = 13+strlen(info)+strlen(fd)+nbyte;
	char* request = malloc(size); // 8 for header, 2 for # seperators to hold size, 2 for the ',', one for null character seperator strlen to fit the string representation of nybytes
	memset(request, '\0', size);  // For example, a sample request may look like: "NET_READ#-6,90#" where -6 is the netfd and 90 is bytes to be read
	strcpy(request, "NET_WRIT");
	strcpy(request+8, "#");
	strcpy(request+9, fd);
	strcpy(request+9+strlen(fd), ",");
	strcpy(request+10+strlen(fd), info);
	strcpy(request+10+strlen(info)+strlen(fd), "#"); 
	// printf("String: %s\n", request);

	int sck = socket(AF_INET, SOCK_STREAM, 0);

	if(sck==-1){
		fprintf(stderr, "socket failed: %s\n", strerror(errno));
		return -1;
	}

	int con = connect(sck, remote->ai_addr, remote->ai_addrlen);	

	if(con==-1){
		fprintf(stderr, "connect failed: %s\n", strerror(errno));
		close(sck);
		return -1;
	}

	ssize_t send = write(sck, request, size);
	if(send==-1){
		fprintf(stderr, "send failed: %s\n", strerror(errno));
		close(sck);
		return -1;
	}

	char* response = malloc(30);
	memset(response, '\0', 30);
	int rec = recv(sck, response, 30, 0);
	// printf("netwrite response: %s\n", response);
	if(rec==-1){
		fprintf(stderr, "recv failed: %s\n", strerror(errno));
		close(sck);
		return -1;
		//error, return -1
	}

	int i = 0;
	while(response[i]!='#'){
		i++;
	}

	char * number_written_string = malloc(30);
	memset(number_written_string, '\0', 30);
	memcpy(number_written_string, response, i);
	int num_written = atoi(number_written_string);

	if(num_written==-2){ // Error checking
		errno=110;
		printf("Netwrite has failed, connection has timeout out: ETIMEDOUT\n");
		return -1;
	}
	if(num_written==-3){
		errno=9;
		printf("Netwrite has failed, a bad file number was passed\n");
		return -1;
	}
	if(num_written==-4){
		errno=104;
		printf("Netwrite has failed, the connection was reset\n");
		return -1;
	}
	
	close(sck);
	return num_written;
}
 
int netclose(int fd){

	if(fd==-1){
		printf("You just passed a fd that is -1, this is the failure response from netopen, please try netopen again\n");
		return -1;
	}
	if(fakeBool==0){
		printf("Error: cannot call netclose without first succesfully calling netserverinit\n");
		return -1;
	}

	char fdString[32];
	sprintf(fdString, "%d", fd);
	int size = 11+strlen(fdString); // 8 for header, two for # one for \0
	char* request = malloc(size);
	memset(request, '\0', size);
	strcpy(request, "NET_CLOS");
	strcpy(request+8, "#");
	strcpy(request+9, fdString);
	strcpy(request+9+strlen(fdString), "#");
	int sck = socket(AF_INET, SOCK_STREAM, 0);

	if(sck==-1){
		fprintf(stderr, "socket failed: %s\n", strerror(errno));
		return -1;
	}

	int con = connect(sck, remote->ai_addr, remote->ai_addrlen);	

	if(con==-1){
		fprintf(stderr, "connect failed: %s\n", strerror(errno));
		close(sck);
		return -1;
	}

	ssize_t send = write(sck, request, size);
	if(send==-1){
		fprintf(stderr, "send failed: %s\n", strerror(errno));
		close(sck);
		return -1;
	}

	char* response = malloc(30);
	memset(response, '\0', 30);
	int rec = recv(sck, response, 30, 0);
	if(rec==-1){
		fprintf(stderr, "recv failed: %s\n", strerror(errno));
		close(sck);
		return -1;
		//error, return -1
	}

	if(response[0]=='1'){
		printf("Error: Bad File Number\n");
		errno=9;
		return -1;
	}


	if(response[0]=='2'){
		printf("Error: Interrupted System Call\n");
		errno=4;
		return -1;
	}

	close(sck); // Close socket
	
	return 0;	
}

ssize_t netread(int fildes, void *buf, size_t nbyte){

	if(fildes==-1){
		printf("You just passed a fd that is -1, this is the failure response from netopen.\nFailure must have occured!\n");
		return -1;
	}
	if(fakeBool==0){
		printf("Error: cannot call netread without first succesfully calling netserverinit\n");
		return -1;
	}

	char sizeToRead[32];
	char fd[32];
	sprintf(fd, "%d", fildes);
	sprintf(sizeToRead, "%d", nbyte);
	int size = 12+strlen(sizeToRead)+strlen(fd);
	char* request = malloc(size); // 8 for header, 2 for # seperators to hold size, 1 for the ',' seperator strlen to fit the string representation of nybytes
	memset(request, '\0', size);  // For example, a sample request may look like: "NET_READ#-6,90#" where -6 is the netfd and 90 is bytes to be read
	strcpy(request, "NET_READ");
	strcpy(request+8, "#");
	strcpy(request+9, fd);
	strcpy(request+9+strlen(fd), ",");
	strcpy(request+10+strlen(fd), sizeToRead);
	strcpy(request+10+strlen(sizeToRead)+strlen(fd), "#"); 
	// printf("String: %s\n", request);
	size_t f = 0;

	int sck = socket(AF_INET, SOCK_STREAM, 0);
	if(sck==-1){
		fprintf(stderr, "socket failed: %s\n", strerror(errno));
		return -1;
		//error
	}
	int con = connect(sck, remote->ai_addr, remote->ai_addrlen);	
	if(con==-1){
		fprintf(stderr, "connect failed: %s\n", strerror(errno));
		close(sck);
		return -1;
		//error, return -1
	}
	ssize_t send = write(sck, request, 12+strlen(fd)+strlen(sizeToRead));
	if(send==-1){
		fprintf(stderr, "send failed: %s\n", strerror(errno));
		close(sck);
		return -1;
	}

	char* response = malloc(nbyte*2);  // Allocate plenty of memory for unknown lengths
	memset(response, '\0', nbyte*2);
	int rec = recv(sck, response, nbyte*2, 0);
	// printf("netread response: %s\n", response);
	if(rec==-1){
		fprintf(stderr, "recv failed: %s\n", strerror(errno));
		close(sck);
		return -1;
		//error, return -1
	}

	int i = 0;
	while(response[i]!='#'){
		i++;
	}

	char * number_written_string = malloc(31);
	memset(number_written_string, '\0', 31);
	memcpy(number_written_string, response, i);
	int num_written = atoi(number_written_string);

	if(num_written==-2){ // Error checking
		errno=110;
		printf("Netwrite has failed, connection has timed out out: ETIMEDOUT\n");
		return -1;
	}
	if(num_written==-3){
		errno=9;
		printf("Netwrite has failed, a bad file number was passed\n");
		return -1;
	}
	if(num_written==-4){
		errno=104;
		printf("Netwrite has failed, the connection was reset\n");
		return -1;
	}

	int holder = i;
	i++;
	while(response[i]!='#'){
		i++;
	}

	void* newChar = malloc(i); // Plenty of data to store fd
	memset(newChar, '\0', i);
	memcpy(newChar, response+holder+1, i-holder-1);
	memcpy(buf, newChar, i-holder-1);

	close(sck);
	return num_written;
			
}

