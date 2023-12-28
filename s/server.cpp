#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <assert.h>
#define PORT 20000
#define BACKLOG 5
#define BUFLEN 512
#define FILENAMELEN 110
#define NUMSERVER 1
#define IPADDRLEN 16
#define MAXDEPTH 15
#define SLEEP_TIME 1
#define USLEEP_TIME 100000
#define SUCCESS 0
pthread_t thread_do[NUMSERVER];
pthread_mutex_t A_LOCK=PTHREAD_MUTEX_INITIALIZER;
void* server(void* sockfd);
void* command(void* none);
void readRequest(int sockfd,char* arg,struct sockaddr_in* saddr);
int checkData(int sockfd,char* arg,struct sockaddr_in* saddr,short number);
void sendAck(int sockfd,char* arg,struct sockaddr_in* saddr);
void writeRequest(int sockfd,char* arg,struct sockaddr_in* saddr);
int checkAck(int sockfd,struct sockaddr_in* saddr,short number);
void sendData(int sockfd,char* arg,struct sockaddr_in* saddr);
size_t dataPacket(int sockfd,short number,char* buf,struct sockaddr_in* saddr,int depth=0);
void ackPacket(int sockfd,short number,struct sockaddr_in* saddr);
void logo();
enum operationCode
{
	RRQ=0x1,
	WRQ,
	DATAPT,
	ACKPT,
	ERRORPT
};

typedef struct {
	short operationCode;
	char name[FILENAMELEN];
	char mode[16]; 
}request;

typedef struct {
	short operationCode;
	short number;
	char detail[BUFLEN]; 
}data;

typedef struct {
	short operationCode;
	short number;
}ack;

typedef struct {
	short operationCode;
	short errorCode;
	char errorMessgae[BUFLEN];
}error;

int main()
{
	logo();
	int sockfd;
	if( (sockfd = socket(PF_INET,SOCK_DGRAM,0) ) == -1)
	{
		fprintf(stderr,"Create socket error: %s\n",strerror(errno) );
		exit(1);
	}
	struct timeval timeout;
	timeout.tv_sec = SLEEP_TIME;
	timeout.tv_usec = 0;
	socklen_t optlen = sizeof(timeout);
	setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, optlen);
	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, optlen);
	optlen=sizeof(optlen);
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optlen, optlen);
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &optlen, optlen);
    
	struct sockaddr_in saddr;
	unsigned int addrlen;
	addrlen = sizeof(struct sockaddr_in);
	memset(&saddr,0,addrlen);
	saddr.sin_family=AF_INET;
	saddr.sin_port=htons(PORT);
	saddr.sin_addr.s_addr=htonl(INADDR_ANY);
	if( bind(sockfd,(struct sockaddr*)&saddr,addrlen) == -1)
	{
		fprintf(stderr,"Bind socket error: %s\n",strerror(errno) );
		exit(1);
	}
	
	printf("Waiting for client connect......\n");
	for(int i=0;i<NUMSERVER;i++)
		pthread_create(&thread_do[i],NULL,server,(void*)&sockfd);
	pthread_t thread;
	pthread_create(&thread,NULL,command,NULL);
	pthread_join(thread,NULL);
	
	close(sockfd);
	return 0;
}

void* server(void* sockfd)
{
	pthread_detach(pthread_self());
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(*(int*)sockfd, &readfds);
	int activity;
	activity = select( (*(int*)sockfd) + 1, &readfds, NULL, NULL, NULL);
	if(activity > 0)
	{
		struct sockaddr_in caddr;
		unsigned int caddrlen=sizeof(caddr);
		request readOrWrite;
		memset(&readOrWrite, 0, sizeof(readOrWrite));
		request* p;
		p=&readOrWrite;
		if(FD_ISSET(*(int*)sockfd, &readfds))
		{
			pthread_mutex_lock(&A_LOCK);	
			recvfrom( *(int*)sockfd, p, sizeof(readOrWrite), 0, (struct sockaddr*)&caddr, (socklen_t*)&caddrlen);
			pthread_mutex_unlock(&A_LOCK);
			perror("Server recvfrom");
			printf("readOrWrite.operationCode:%hd\nreadOrWrite.name:%s\nreadOrWrite.mode:%s\n", ntohs(readOrWrite.operationCode), readOrWrite.name, readOrWrite.mode);
			printf("Got message from %s:%d\n", inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port));
			readOrWrite.operationCode=ntohs(readOrWrite.operationCode);
			switch(readOrWrite.operationCode)
			{
			case RRQ:
				sendData( *(int*)sockfd, readOrWrite.name, &caddr);
				break;
			case WRQ:
				ackPacket( *(int*)sockfd, 0, &caddr);
				sendAck( *(int*)sockfd, readOrWrite.name, &caddr);
				break;
			default:
				break;
			}
		}
		else
			perror("Fd_isset");
	}
	else
		perror("Server recvfrom");

	pthread_exit(0);
}

void* command(void* none)
{
	char cmd[10];
	while( strcmp(cmd,"exit") )
		scanf("%s",cmd);
	for(int i=0;i<NUMSERVER;i++)
		pthread_cancel(thread_do[i]);
	pthread_exit(0);
}

void readRequest(int sockfd,char* arg,struct sockaddr_in* saddr)
{
	request readFile;
	readFile.operationCode=htons(RRQ);
	strcpy(readFile.name, (const char*)arg);
	if(strstr(readFile.name,"txt"))
		strcpy(readFile.mode, "octet");
	else
		strcpy(readFile.mode, "netascii");
	int addrlen=sizeof(struct sockaddr_in);
	int sendlen;
	pthread_mutex_lock(&A_LOCK);
	sendlen = sendto(sockfd, (char*)&readFile, sizeof(readFile), 0, (struct sockaddr*)saddr, (socklen_t)addrlen);
	pthread_mutex_unlock(&A_LOCK);
	if(sendlen == -1)
		perror("sendto");
}

int checkData(int sockfd,char* arg,struct sockaddr_in* saddr,short number)
{
	if( arg == NULL )
		return 1; 
	data dataFile;
	memset(&dataFile,-1,sizeof(dataFile));
	data* p;
	p=&dataFile;
	int rcvlen=0;
    int addrlen=sizeof(struct sockaddr_in);
	pthread_mutex_lock(&A_LOCK);
	rcvlen=recvfrom(sockfd, p, sizeof(dataFile), 0, (struct sockaddr*)saddr, (socklen_t*)&addrlen);
	pthread_mutex_unlock(&A_LOCK);
	if(rcvlen == -1)
		perror("rcvlen");
	dataFile.operationCode=ntohs(dataFile.operationCode);
	dataFile.number=ntohs(dataFile.number);
	if(dataFile.operationCode == DATAPT && dataFile.number == number)
	{
		pthread_mutex_lock(&A_LOCK);
		int newfd;
		newfd=open( arg, O_RDWR | O_CREAT | O_APPEND );
		if( newfd < 0)
			fprintf(stderr,"Open file error: %s\n",strerror(errno) );
		int count;
		count=write(newfd,dataFile.detail,(size_t)strlen(dataFile.detail) );
		pthread_mutex_unlock(&A_LOCK);
		if( count < 0)
			fprintf(stderr,"Write file error: %s\n",strerror(errno) );
		return count;
	}
	else
		printf("Check data incorrect\n");
	return 0;
}

void sendAck(int sockfd,char* arg,struct sockaddr_in* saddr)
{
	short number=1;
	int count=0;
	while( count=checkData(sockfd,arg,saddr,number) )
	{
		if(errno == EINPROGRESS)
			for(int i=0;i < MAXDEPTH;i++)
			{
				ackPacket(sockfd,number,saddr);
				if(errno == SUCCESS)
					break;
			}
		ackPacket(sockfd,number,saddr);
		if(count < BUFLEN-1)
			break;
		number++;
	}
}

void writeRequest(int sockfd,char* arg,struct sockaddr_in* saddr)
{
	request writeFile;
	writeFile.operationCode=htons(WRQ);
	strcpy(writeFile.name, (const char*)arg);
	if(strstr(writeFile.name,"txt"))
		strcpy(writeFile.mode, "octet");
	else
		strcpy(writeFile.mode, "netascii");
	int addrlen=sizeof(struct sockaddr_in);
	int sendlen;
	pthread_mutex_lock(&A_LOCK);
	sendlen=sendto(sockfd, (char*)&writeFile, sizeof(writeFile), 0, (struct sockaddr*)saddr, (socklen_t)addrlen);
	pthread_mutex_unlock(&A_LOCK);
	if(sendlen == -1)
		perror("sendto");
}

int checkAck(int sockfd,struct sockaddr_in* saddr,short number)
{
	ack ackFile;
	memset(&ackFile,-1,sizeof(ackFile));
	ack* p;
	p=&ackFile;
	int rcvlen=0;
    int addrlen=sizeof(struct sockaddr_in);
	pthread_mutex_lock(&A_LOCK);
	rcvlen=recvfrom(sockfd, p, sizeof(ackFile), 0, (struct sockaddr*)saddr, (socklen_t*)&addrlen);
	pthread_mutex_unlock(&A_LOCK);
	if(rcvlen == -1)
		perror("rcvlen");
	ackFile.operationCode=ntohs(ackFile.operationCode);
	ackFile.number=ntohs(ackFile.number);
	if(ackFile.operationCode == ACKPT && ackFile.number == number)
		return 1;
	else
		return 0;
}

void sendData(int sockfd,char* arg,struct sockaddr_in* saddr)
{
	FILE *fileUpload;
	fileUpload = fopen( (const char*)arg, "rb");
	char buf[BUFLEN];
	memset(buf, 0, sizeof(buf) );
	size_t bytesRead;
	size_t sendlen=0;
	short number=1;
	while( (bytesRead = fread(buf, sizeof(char), sizeof(buf) - 1, fileUpload)) >= 0 )//必须减一且">0"修改为">=0" 
	{
		printf("bytesRead = %ld\n",bytesRead);
		if( bytesRead == 0 ) 
		{
			sendlen=dataPacket(sockfd,number,NULL,saddr);
			break;
		}
		sendlen=dataPacket(sockfd,number,buf,saddr);
		printf("sendlen = %ld\n",sendlen);
		if( sendlen < 0 )
			perror("DataPacket state");
		for(int i=0;i < MAXDEPTH;i++)
		{
			if( checkAck(sockfd,saddr,number) == 0)
			{
				if(errno == EINPROGRESS)
					dataPacket(sockfd,number,buf,saddr);
			}
			else
				break; 
		}
		memset(buf, 0, sizeof(buf) );
		if( bytesRead < BUFLEN-1 ) 
			break;
		number++;
 	}
 	if( sendlen == sizeof(short) + sizeof(number) )
		if( checkAck(sockfd,saddr,number) )
			return;
}

size_t dataPacket(int sockfd,short number,char* buf,struct sockaddr_in* saddr,int depth) 
{
	data dataFile;
	dataFile.operationCode=htons(DATAPT);
	dataFile.number=htons(number);
	strcpy(dataFile.detail, (const char*)buf);
	int addrlen=sizeof(struct sockaddr_in);
    size_t sendlen=0;
	pthread_mutex_lock(&A_LOCK);
	sendlen=sendto(sockfd, (char*)&dataFile, sizeof(dataFile), 0, (struct sockaddr*)saddr, (socklen_t)addrlen); // 发送消息到服务器
	pthread_mutex_unlock(&A_LOCK);
	return sendlen;
}

void ackPacket(int sockfd,short number,struct sockaddr_in* saddr)
{
	ack ackFile;
	ackFile.operationCode=htons(ACKPT);
	ackFile.number=htons(number);
	int addrlen=sizeof(struct sockaddr_in);
	pthread_mutex_lock(&A_LOCK);
	sendto(sockfd, (char*)&ackFile, sizeof(ackFile), 0, (struct sockaddr*)saddr, (socklen_t)addrlen); // 发送消息到服务器
	pthread_mutex_unlock(&A_LOCK);
}

void logo()
{
	printf("  ______________________ \n");
    printf(" /_  __/ ____/_  __/ __ \\\n");
    printf("  / / / /_    / / / /_/ /\n");
    printf(" / / / __/   / / / ____/ \n");
	printf("/_/ /_/     /_/ /_/      \n");
}