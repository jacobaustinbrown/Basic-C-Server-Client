#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<sys/time.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<pthread.h>
#include<errno.h>
#include<time.h>

void error(char* str){
  perror(str);
  //exit(0);				// Maybe not exit, but leave it for now
}

/**
 * Worker function for the thread
 */
struct queue{
	int fd;
	int marker;
	time_t start;
	struct queue* next;
};

struct fdholder{
	int mode;
	int flags;
	char* fpath;
};

struct fdholder fdHolder[1024];

struct node{ // A linked list of structs in order to implement mutual exclusion of opened files
	int readers;
	int writer;
	int transaction;
	int exclusive;
	char* fpath;
	struct queue* next_q;
	struct node* next;
};

struct node* head;
pthread_mutex_t mutex;
int newFileStatus = 0;

void* monitorThread(void* nothing){
	
	struct node* temp;
	struct queue* qtemp;
	double diff = 0;
	time_t end;

	while(1){

		pthread_mutex_lock(&mutex);
		temp=head;
		while(temp!=NULL){ // Search each file's queue, if any process has been running for more than 2 seconds, mark it for the worker thread to deque
			qtemp=temp->next_q;
			while(qtemp!=NULL){
				end = time(&end);
				diff = difftime(end, qtemp->start);
				if(diff>2){
					qtemp->marker=1;
				}
				qtemp=qtemp->next;
			}
			temp=temp->next;
		}
		pthread_mutex_unlock(&mutex);
		sleep(3);
	}

	return 0;
}

int closeFile(char* path, int flags, int mode){
	
	pthread_mutex_lock(&mutex);
	struct node* temp = head;

	if(path==NULL){
		// file not in database, therefore do not search the list
		pthread_mutex_unlock(&mutex);
		return -1;
	}

	do{
		if(strcmp(temp->fpath, path)==0){
			if(mode==4){
				if(flags==0||flags==2){
					temp->readers--;
				}
				if(flags==1||flags==2){
					temp->writer--;
				}
			
				pthread_mutex_unlock(&mutex);
				return 0;
			}else if(mode==5){
				temp->exclusive--;
				if(flags==0||flags==2){
					temp->readers--;
				}
				if(flags==1||flags==2){
					temp->writer--;
				}
				
				pthread_mutex_unlock(&mutex);
				return 0;
			}else if(mode==6){
				temp->transaction=0;
				temp->readers=0;
				temp->writer=0;
				temp->exclusive=0;
				pthread_mutex_unlock(&mutex);
				return 0;
			}
		}

		temp=temp->next;
	}while(temp!=NULL);

	pthread_mutex_unlock(&mutex);
	// No node with file path, should only happen if someone calls with an invalid fd
	return -1;
}

int openNewFile(char* path, int flags, int mode, int fd){
	// Modes are 4 = unrestricted, 5 = exclusive, 6 = transaction
	
	pthread_mutex_lock(&mutex);
	int isInQueue = 0;
	int isOnly = 0;
	struct node* temp = head;
	struct node* prev = head;
	do{
		if(strcmp(temp->fpath, path)==0){

			struct queue* qtemp = temp->next_q; 
			struct queue* qprev = temp->next_q;
			if(qtemp==NULL){  // Must be the only process trying to open as the queue is empty
				isOnly=1;
				goto jump;
			}
			while(qtemp->next!=NULL){
				if(qtemp->fd==fd){
					break;
				}
				qprev=qtemp;
				qtemp=qtemp->next;
			}
			
			if(qtemp->fd==fd){ // Must have found the thread's spot in the queue
				isInQueue=1;
				if(qtemp->marker==1){ // Monitor thread has identified this process as too old to stay in que, deque
					if(qtemp->next==NULL){ // If process to be removed is at the end of the queue
						if(temp->next_q->next==NULL){
							free(temp->next_q);
							temp->next_q=NULL;
						}
						pthread_mutex_unlock(&mutex);
						return -2;
					}else{ // Process is not at the end of the queue
						qprev=qtemp;
						qtemp=qtemp->next;
						pthread_mutex_unlock(&mutex);
						return -2;
					}
				}
				if(qtemp->next!=NULL){ // Status is ok, but it is not at the front of the queue, so we should not allow it to try
					pthread_mutex_unlock(&mutex); // to complete a file operation
					return -1;
				}

			}
			jump:
			// If we reached here, that means that the process was not in the queue, or is allowed to try and open the file again by being first in the queue
			if(mode==4){
				if(temp->transaction==0){
					if(flags==0){
						temp->readers++;
					}
					if(flags==1||flags==2){
						if(temp->exclusive==0){
							temp->writer++;
							if(flags==2){
								temp->readers++;
							}
						}else if(temp->exclusive>0&&temp->writer==0){
							temp->writer++;
							if(flags==2){
								temp->readers++;
							}
						}else{
							// Create new queue node if need be

							if(isInQueue==1){ 
								pthread_mutex_unlock(&mutex);
								return -1;
							}else{
								struct queue* qins = malloc(sizeof(struct queue)); //Since this is the first failure, we must insert into the queue
								qins->fd=fd;
								qins->marker=0;
								qins->start = time(&qins->start);
								qins->next=NULL;
								if(isOnly==1){
									temp->next_q=qins;
								}else{	
									qtemp->next=qins;
								}	
								pthread_mutex_unlock(&mutex);
								return -1;
							}
						}
					}
					if(isInQueue==1){ //If the process was in the queue, dequeue it
						qtemp=NULL;
					}
					pthread_mutex_unlock(&mutex);
					return 0;
				}else{
					if(isInQueue==1){
						pthread_mutex_unlock(&mutex);
						return -1;
					}else{
						struct queue* qins = malloc(sizeof(struct queue)); //Since this is the first failure, we must insert into the queue
						qins->fd=fd;
						qins->marker=0;
						qins->start = time(&qins->start);
						qins->next=NULL;
						if(isOnly==1){
							temp->next_q=qins;
						}else{
							qtemp->next=qins;
						}	
						pthread_mutex_unlock(&mutex);
						return -1;
					}
				}
			
			}else if(mode==5){
				if(temp->transaction==0&&temp->writer==0&&(flags==1||flags==2)){
					temp->writer++;
					if(flags==2){
						temp->readers++;
					}
					temp->exclusive++;
					if(isInQueue==1){ //If the process was in the queue, dequeue it
						qtemp=NULL;
					}
					pthread_mutex_unlock(&mutex);
					return 0;
				}else if(temp->transaction==0&&flags==0){
					temp->readers++;
					temp->exclusive++;
					if(isInQueue==1){ //If the process was in the queue, dequeue it
						qtemp=NULL;
					}
					pthread_mutex_unlock(&mutex);
					return 0;
					
				}else{ // Failure
					if(isInQueue==1){
						pthread_mutex_unlock(&mutex);
						return -1;
					}else{
						struct queue* qins = malloc(sizeof(struct queue)); //Since this is the first failure, we must insert into the queue
						qins->fd=fd;
						qins->marker=0;
						qins->start = time(&qins->start);
						qins->next=NULL;
						if(isOnly==1){
							temp->next_q=qins;
						}else{
							qtemp->next=qins;
						}	
						pthread_mutex_unlock(&mutex);
						return -1;
					}
				}
			}else if(mode==6){
				if(temp->transaction==0&&temp->readers==0&&temp->writer==0&&temp->exclusive==0){
					temp->transaction=1;
					if(flags==0||flags==2){
						temp->readers=1;
					}
					if(flags==1||flags==2){
						temp->writer=1;
					}
					if(isInQueue==1){ //If the process was in the queue, dequeue it
						qtemp=NULL;
					}
					pthread_mutex_unlock(&mutex);
					return 0;
				}else{
					struct queue* qins = malloc(sizeof(struct queue)); //Since this is the first failure, we must insert into the queue
					qins->fd=fd;
					qins->marker=0;
					qins->start = time(&qins->start);
					qins->next=NULL;
					if(isOnly==1){
						temp->next_q=qins;
					}else{
						qtemp->next=qins;
					}	
					pthread_mutex_unlock(&mutex);
					return -1;
				}
			}else{
				// invalid mode, should never happen
				pthread_mutex_unlock(&mutex);
				return -3;
			}
		}
		prev=temp;
		temp=temp->next;
	}while(temp!=NULL);
	
	//If reached here, no node in the linked list is associated with the file path, make a node and insert at end
	struct node* ins = malloc(sizeof(struct node));
	ins->fpath = path;
	ins->next = NULL;
	ins->next_q = NULL;
	if(mode==4){
		if(flags==0){
			ins->readers=1;
			ins->writer=0;
			ins->exclusive=0;
			ins->transaction=0;
		}else if(flags==1){
			ins->readers=0;
			ins->writer=1;
			ins->exclusive=0;
			ins->transaction=0;
		}else{
			ins->readers=1;
			ins->writer=1;
			ins->exclusive=0;
			ins->transaction=0;
		}
	}else if(mode==5){
		if(flags==0){
			ins->readers=1;
			ins->writer=0;
			ins->exclusive=1;
			ins->transaction=0;
		}else if(flags==1){
			ins->readers=0;
			ins->writer=1;
			ins->exclusive=1;
			ins->transaction=0;
		}else{
			ins->readers=1;
			ins->writer=1;
			ins->exclusive=1;
			ins->transaction=0;
		}
	}else if(mode==6){
		if(flags==0){
			ins->readers=1;
			ins->writer=0;
			ins->exclusive=0;
			ins->transaction=1;
		}else if(flags==1){
			ins->readers=0;
			ins->writer=1;
			ins->exclusive=0;
			ins->transaction=1;
		}else{
			ins->readers=1;
			ins->writer=1;
			ins->exclusive=0;
			ins->transaction=1;
		}
		
	}else{
		//invalid mode, should never happen
	}
	
	prev->next = ins;
	pthread_mutex_unlock(&mutex);
	return 0;
}

void* connectionHandler(void* socket_desc){
  int clientfd = *(int*)socket_desc;
  int n=-1;				//utility: file desc for monitering the socket
  char status[256];			//utility: for returning a status message
  char buffer[256];			//data going to and coming from the socket
  char retfd[256];			//utility: holds the fd to be returned
  char* path = malloc(256);		//Path extracted from buffer
  memset(path, '\0', 256);
  
  int newfd=0;				//used when a file desc is passed for a read/write/close request
    
  const char* rd = "NETOPEN0"; 		//Netopen: Read Only String
  const char* wr = "NETOPEN1";		//Netopen: Write Only String
  const char* rdwr = "NETOPEN2";	//Netopen: Read Write String
  const char* netrd = "NET_READ";	//Netread
  const char* netwr = "NET_WRIT";	//Netwrite
  const char* netcl = "NET_CLOS";	//Netclose

  memset(status, '\0', 256);
  memset(buffer, '\0', 256);  
  memset(retfd, '\0', 256);
  
  n = recv(clientfd, buffer, 256, 0);

  //Failure on reading from client's socket
  if(n==0){
    error("ERROR: Client disconnected");
  }else if(n<0){
    error("ERROR: failed to receive message from Client");
  }
  
  /*THESE ARE NETOPEN THINGS (sorry didn't separate them into functions in the end lawls)*/
  int m = 0;	//number of characters sprintf wrote
  if(strncmp(rd, buffer+1, 8)==0){					//READ ONLY MODE
    strncpy(path, buffer+9, 248);					// Strncpy is more safe than strcpy, was causing a segfault, I memset buffer before use	
    //printf("path: %s\n", path);
    n = open(path, O_RDONLY);
    if(n<0){
      error("ERROR: Error opening file");
      if(errno == EACCES){
	status[0] = '2';
	status[1] = '#';
      }else if(errno == EINTR){
	status[0] = '3';
	status[1] = '#';
      }else if(errno == EISDIR){
	status[0] = '4';
	status[1] = '#';
      }else if(errno == ENOENT){
	status[0] = '5';
	status[1] = '#';
      }else if(errno == EROFS){
	status[0] = '6';
	status[1] = '#';
      }else{
	//Unspecified Error
	status[0] = '1';
	status[1] = '#';
      }
    }else{ 
        if(buffer[0]=='u'){ // based on output, add to 
	    newFileStatus = openNewFile(path, 0, 4, n);
	    while(newFileStatus==-1){
		newFileStatus = openNewFile(path, 0, 4, n);
		sleep(1);
	    }
	}else if(buffer[0]=='e'){
	    newFileStatus = openNewFile(path, 0, 5, n);
	    while(newFileStatus==-1){
		newFileStatus = openNewFile(path, 0, 5, n);
		sleep(1);
	    }
	}else if(buffer[0]=='t'){
	    newFileStatus = openNewFile(path, 0, 6, n);
	    while(newFileStatus==-1){
		newFileStatus = openNewFile(path, 0, 4, n);
		sleep(1);
	    }
	}
	if(newFileStatus!=0){
		status[0] = '7'; // Define new error in client
		status[1] = '#';
		close(n); // Can't send the file descriptor because it dosen't fit our rules
	}else{
		status[0] = '0';
		status[1] = '#';
		if(buffer[0]=='u'){ // based on output, add to 
	            fdHolder[n].mode = 4;
		    fdHolder[n].flags = 0;
		    fdHolder[n].fpath = path;
	        }else if(buffer[0]=='e'){
	            fdHolder[n].mode = 5;
		    fdHolder[n].flags = 0;
		    fdHolder[n].fpath = path;
	        }else if(buffer[0]=='t'){
	            fdHolder[n].mode = 6;
		    fdHolder[n].flags = 0;
		    fdHolder[n].fpath = path;
	        }
	}
    }

    n*=-1;
    m = sprintf(retfd, "%d#", n);
    strncpy(&status[2], retfd, m);
    
  }else if(strncmp(wr, buffer+1, 8)==0){					//WRITE ONLY MODE
    strncpy(path, buffer+9, 248);
    //printf("path: %s\n", path);
    n = open(path, O_WRONLY);    
    if(n<0){
      error("ERROR: Error opening file");
      if(errno == EACCES){
	status[0] = '2';
	status[1] = '#';
      }else if(errno == EINTR){
	status[0] = '3';
	status[1] = '#';
      }else if(errno == EISDIR){
	status[0] = '4';
	status[1] = '#';
      }else if(errno == ENOENT){
	status[0] = '5';
	status[1] = '#';
      }else if(errno == EROFS){
	status[0] = '6';
	status[1] = '#';
      }else{
	//Unspecified Error
	status[0] = '1';
	status[1] = '#';
      }
    }else{
      if(buffer[0]=='u'){ // based on output, add to 
	    newFileStatus = openNewFile(path, 1, 4, n);
	    while(newFileStatus==-1){
		newFileStatus = openNewFile(path, 1, 4, n);
		sleep(1);
	    }
	}else if(buffer[0]=='e'){
	    newFileStatus = openNewFile(path, 1, 5, n);
	    while(newFileStatus==-1){
		newFileStatus = openNewFile(path, 1, 5, n);
		sleep(1);
	    }
	}else if(buffer[0]=='t'){
	    newFileStatus = openNewFile(path, 1, 6, n);
	    while(newFileStatus==-1){
		newFileStatus = openNewFile(path, 1, 6, n);
		sleep(1);
	    }
	}
	if(newFileStatus!=0){
		status[0] = '7'; // Define new error in client
		status[1] = '#';
		close(n); // Can't send the file descriptor because it dosen't fit our rules
	}else{
		status[0] = '0';
		status[1] = '#';
		if(buffer[0]=='u'){ // based on output, add to 
	            fdHolder[n].mode = 4;
		    fdHolder[n].flags = 1;
		    fdHolder[n].fpath = path;
	        }else if(buffer[0]=='e'){
	            fdHolder[n].mode = 5;
		    fdHolder[n].flags = 1;
		    fdHolder[n].fpath = path;
	        }else if(buffer[0]=='t'){
	            fdHolder[n].mode = 6;
		    fdHolder[n].flags = 1;
		    fdHolder[n].fpath = path;
	        }
	}
    }
    n*=-1;
    m = sprintf(retfd, "%d#", n);
    strncpy(&status[2], retfd, m);
    
  }else if(strncmp(rdwr, buffer+1, 8)==0){				//READ WRITE MODE
    strncpy(path, buffer+9, 248);
    //printf("path: %s\n", path);
    n = open(path, O_RDWR);    
    if(n<0){
      error("ERROR: Error opening file");
      if(errno == EACCES){
	status[0] = '2';
	status[1] = '#';
      }else if(errno == EINTR){
	status[0] = '3';
	status[1] = '#';
      }else if(errno == EISDIR){
	status[0] = '4';
	status[1] = '#';
      }else if(errno == ENOENT){
	status[0] = '5';
	status[1] = '#';
      }else if(errno == EROFS){
	status[0] = '#';
	status[1] = '#';
      }else{
	//Unspecified Error
	status[0] = '1';
	status[1] = '#';
      }
    }else{
      if(buffer[0]=='u'){ // based on output, add to 
	    newFileStatus = openNewFile(path, 2, 4, n);
	    while(newFileStatus==-1){
		newFileStatus = openNewFile(path, 2, 4, n);
		sleep(1);
	    }
	}else if(buffer[0]=='e'){
	    newFileStatus = openNewFile(path, 2, 5, n);
	    while(newFileStatus==-1){
		newFileStatus = openNewFile(path, 2, 5, n);
		sleep(1);
	    }
	}else if(buffer[0]=='t'){
	    newFileStatus = openNewFile(path, 2, 6, n);
	    while(newFileStatus==-1){
		newFileStatus = openNewFile(path, 2, 6, n);
		sleep(1);
	    }
	}
	if(newFileStatus!=0){
		status[0] = '7'; // Define new error in client
		status[1] = '#';
		close(n); // Can't send the file descriptor because it dosen't fit our rules
	}else{
		status[0] = '0';
		status[1] = '#';
		if(buffer[0]=='u'){ // based on output, add to 
	            fdHolder[n].mode = 4;
		    fdHolder[n].flags = 2;
		    fdHolder[n].fpath = path;
	        }else if(buffer[0]=='e'){
	            fdHolder[n].mode = 5;
		    fdHolder[n].flags = 2;
		    fdHolder[n].fpath = path;
	        }else if(buffer[0]=='t'){
	            fdHolder[n].mode = 6;
		    fdHolder[n].flags = 2;
		    fdHolder[n].fpath = path;
	        }
	}
    }
    n*=-1;
    m = sprintf(retfd, "%d#", n);
    strncpy(&status[2], retfd, m);
    
    
    
    
    
  /*THESE ARE IF YOU SENT IN A FILE DESC*/
  }else if(strncmp(netrd, buffer, 8)==0){				//NETREAD CALL
    char* token;
    char* length;
    int l=0;

    token = strtok(buffer, "#");

    token = strtok(NULL, ",");		//fd

    length = strtok(NULL, "#");

    newfd = atoi(token);
    newfd*=-1;
    l = atoi(length);
    memset(buffer, '#', 256);
    n = read(newfd, buffer, l);
    if(n>=0){
      strcpy(buffer+(n+1), "#");
      int m=0;
      m = sprintf(retfd, "%d#", n);
      strncpy(status, retfd, m);
      strncpy(&status[m], buffer, n+1);
    }else{

      error("ERROR: Error reading from file: ");
      if(errno == ETIMEDOUT){
	status[0] = '-';
	status[1] = '2';
	status[2] = '#';
      }else if(errno == EBADF){
	status[0] = '-';
	status[1] = '3';
	status[2] = '#';
      }else if(errno == ECONNRESET){
	status[0] = '-';
	status[1] = '4';
	status[2] = '#';
      }
    }
    
  }else if(strncmp(netwr, buffer, 8)==0){				//NETWRITE CALL
    char* token;
    char* newbuff;
    // char* length;

    int l=0;
    token = strtok(buffer, "#");
    token = strtok(NULL, ",");		//fd

    newbuff = strtok(NULL, "#");
    l = strlen(newbuff);
    newfd = atoi(token);
    newfd=newfd*-1;
    // newfd*=-1;
    // l = atoi(length);

    n = write(newfd, newbuff, l);
    
    if(n>0){
      int m=0;
      m = sprintf(retfd, "%d#", n);
      strncpy(status, retfd, m);
    }else{
      error("ERROR: Error writing to the file");
      if(errno == ETIMEDOUT){
	status[0] = '-';
	status[1] = '2';
	status[2] = '#';
      }else if(errno == EBADF){
	status[0] = '-';
	status[1] = '3';
	status[2] = '#';
      }else if(errno == ECONNRESET){
	status[0] = '-';
	status[1] = '4';
	status[2] = '#';
      }
    }
    
  }else if(strncmp(netcl, buffer, 8)==0){				//NETCLOSE CALL
    char* token;

    token = strtok(buffer, "#");
    token = strtok(NULL, "#");		//fd
    newfd = atoi(token);
    newfd*=-1;
    n = close(newfd);

    if(n==-1){
      if(errno == EBADF){
	status[0] = '1';
      }else if(errno == EINTR){
	status[0] = '2';
      }
    }else{
      int worked = closeFile(fdHolder[newfd].fpath, fdHolder[newfd].flags, fdHolder[newfd].mode);

      status[0] = '0';
    }

    
      
  /*THIS IS IF NOTHING WAS PROPERLY READ*/
  }else{
   error("ERROR: Incorrect header input"); 
  }
  

  //Write back to the client's socket
  write(clientfd, status, strlen(status));
  close(clientfd);
  pthread_exit(NULL);
  //pthread_exit(status);
}

int main(int argc, char**argv){
  // Initialize head
  head = malloc(sizeof(struct node));
  head->readers=0;
  head->writer=0;
  head->fpath="bob";
  head->exclusive=0;
  head->transaction=0;
  head->next=NULL;
  pthread_t monitor;
  pthread_create(&monitor, NULL, monitorThread, NULL);
  
  int servfd=-1;			//server socket
  int clientfd=-1;			//client socket
  int portno=10214;			//server port to connect to
  
  struct sockaddr_in serverAddrInfo;	//address info for building server socket
  struct sockaddr_in clientAddrInfo;	//address info about client socket
 
  /*Get the address of the port*/
  servfd=socket(AF_INET,SOCK_STREAM,0);
  if(servfd<0){
    error("ERROR: opening socket");
    return 0;
  }
  
  /*Use port to set up address struct*/
  bzero((char*)&serverAddrInfo, sizeof(serverAddrInfo));	//Zero out struct
  serverAddrInfo.sin_port = htons(portno);			//Set remote port and translate int to 'netword port int'
  serverAddrInfo.sin_family = AF_INET;				//Flag: indicate type of network address [ or do something like inet_addr("74.125.235.20"); ]
  serverAddrInfo.sin_addr.s_addr = INADDR_ANY;			//Flag: indicate type of network address w're willing to accept connections from 
  
  /*Build up server socket*/
  //Bind server socket to the given port (where client will connect)
  if(bind(servfd, (struct sockaddr*)&serverAddrInfo, sizeof(serverAddrInfo))<0){
    error("ERROR: binding server socket");
  }

  //open server socket to listen for client connections (gotta stop listening at some pt tho?)
  listen(servfd, 5);
  //printf("Listening for clients.....\n");			//FLAG: COMMENT THIS OUT LATER
  
  while(1){ 
    /*FROM HERE ON OUT THE CLIENT TRIED TO CONNECT*/
    clientfd = accept(servfd, (struct sockaddr*)&clientAddrInfo, &clientAddrInfo);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_t workerThread;
    int* newSockfd = malloc(1);
    *newSockfd = clientfd;
  
    if(pthread_create(&workerThread, &attr, connectionHandler, (void*)newSockfd)<0){
      error("ERROR: could not create thread");
      return 1;
    }
    pthread_join(workerThread, NULL);
    close(clientfd);
    
    if(newSockfd<0){
      error("ERROR: accept failed");
    }
  }
  //haha we don't close this server socket
  return 0;
}
