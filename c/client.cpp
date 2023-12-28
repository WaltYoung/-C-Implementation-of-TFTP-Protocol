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
#define NUMSERVER 10
#define IPADDRLEN 16
#define MAXDEPTH 15
#define SLEEP_TIME 1
#define USLEEP_TIME 100000
#define SUCCESS 0
pthread_t thread_do[NUMSERVER];
pthread_mutex_t A_LOCK=PTHREAD_MUTEX_INITIALIZER;
const char* cmdlist[]={"DOWNLOAD","UPLOAD","HELP","EXIT",NULL};
void command(char* cmdline,int* code,char* arg);
void readRequest(int sockfd,char* arg,struct sockaddr_in* saddr);
int checkData(int sockfd,char* arg,struct sockaddr_in* saddr,short number);
void sendAck(int sockfd,char* arg,struct sockaddr_in* saddr);
void writeRequest(int sockfd,char* arg,struct sockaddr_in* saddr);
int checkAck(int sockfd,struct sockaddr_in* saddr,short number);
void sendData(int sockfd,char* arg,struct sockaddr_in* saddr);
size_t dataPacket(int sockfd,short number,char* buf,struct sockaddr_in* saddr,int depth=0);
void ackPacket(int sockfd,short number,struct sockaddr_in* saddr);
void help();
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
    
	printf("Please input server IP:");
	char ip[16];
	scanf("%s",ip);
	getchar();
	struct sockaddr_in saddr;
	int addrlen;
	addrlen = sizeof(saddr);
	memset(&saddr,0,addrlen);
	saddr.sin_family=AF_INET;
	saddr.sin_port=htons(PORT);
	saddr.sin_addr.s_addr=inet_addr(ip); 
	
	char* cmdline;
	size_t cmdlen=0;
	int len=0;
	char arg[FILENAMELEN]; 
	int code=2;
	while(1)
	{
		while( ( len=getline(&cmdline,&cmdlen,stdin) ) < 0)
			perror("Getline error");
		cmdline[len-1]='\0';
		memset(arg,0,sizeof(arg));
		command(cmdline,&code,arg);
		free(cmdline);
		switch(code)
		{
		case 0:
			readRequest(sockfd,arg,&saddr);
			sendAck(sockfd,arg,&saddr);
			break;
		case 1:
			writeRequest(sockfd,arg,&saddr);
			if(checkAck(sockfd,&saddr,0))
				sendData(sockfd,arg,&saddr);
			break;
		case 2:
			help();
			break;
		case 3:
			close(sockfd);
			break;
		default:
			break;
		}
	}
	return 0;
} 

void command(char* cmdline,int* code,char* arg)
{
	char cmd[10];
	memset(cmd,0,sizeof(cmd));
	char* p;
	int i;
	for(p=cmdline,i=0;*p != ' ' && *p != '\0' && i < sizeof(cmdline);p++,i++)
		if(*p >= 'a' && *p <= 'z')
			cmd[i]=*p-'a'+'A';
		else
			cmd[i]=*p;
	while(*p == ' ')
	{
		p++;
		i++;
	}
	int len=0;
	if(*p)
	{
		len=strlen(cmdline)-i;
		memcpy(arg,p,len);
	} 
	for(int j=0;cmdlist[j];j++)
		if( !strcmp(cmd,cmdlist[j]) )
		{
			*code=j;
			break;
		}
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
	sendlen = sendto(sockfd, (char*)&readFile, sizeof(readFile), 0, (struct sockaddr*)saddr, (socklen_t)addrlen);
	if(sendlen == -1)
		perror("sendto");
	printf("readFile.operationCode:%hd\nreadFile.name:%s\nreadFile.mode:%s\n", ntohs(readFile.operationCode), readFile.name, readFile.mode);
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
	rcvlen=recvfrom(sockfd, p, sizeof(dataFile), 0, (struct sockaddr*)saddr, (socklen_t*)&addrlen);
	if(rcvlen == -1)
		perror("rcvlen");
	dataFile.operationCode=ntohs(dataFile.operationCode);
	dataFile.number=ntohs(dataFile.number);
	printf("dataFile.operationCode = %hd\n",dataFile.operationCode);
	printf("dataFile.number = %hd\n",dataFile.number);
	printf("number = %hd\n",number);
	if(dataFile.operationCode == DATAPT && dataFile.number == number)
	{

		int newfd;
		newfd=open( arg, O_RDWR | O_CREAT | O_APPEND );
		if( newfd < 0)
			fprintf(stderr,"Open file error: %s\n",strerror(errno) );
		int count;
  		count=write(newfd,dataFile.detail,(size_t)strlen(dataFile.detail) );
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
	sendlen=sendto(sockfd, (char*)&writeFile, sizeof(writeFile), 0, (struct sockaddr*)saddr, (socklen_t)addrlen);
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
	rcvlen=recvfrom(sockfd, p, sizeof(ackFile), 0, (struct sockaddr*)saddr, (socklen_t*)&addrlen);
	if(rcvlen == -1)
		perror("rcvlen");
	ackFile.operationCode=ntohs(ackFile.operationCode);
	ackFile.number=ntohs(ackFile.number);
	printf("ackFile.operationCode = %hd\n",ackFile.operationCode);
	printf("ackFile.number = %hd\n",ackFile.number);
	printf("number = %hd\n",number);
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
	while( (bytesRead = fread(buf, sizeof(char), sizeof(buf) - 1, fileUpload)) >= 0 ) 
	{
		if( bytesRead == 0 )
		{
			sendlen=dataPacket(sockfd,number,NULL,saddr);
			break;
		}
		sendlen=dataPacket(sockfd,number,buf,saddr);
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
	sendlen=sendto(sockfd, (char*)&dataFile, sizeof(dataFile), 0, (struct sockaddr*)saddr, (socklen_t)addrlen);
	return sendlen;
}

void ackPacket(int sockfd,short number,struct sockaddr_in* saddr)
{
	ack ackFile;
	ackFile.operationCode=htons(ACKPT);
	ackFile.number=htons(number);
	int addrlen=sizeof(struct sockaddr_in);
	sendto(sockfd, (char*)&ackFile, sizeof(ackFile), 0, (struct sockaddr*)saddr, (socklen_t)addrlen);
}

void help()
{
    printf("DOWNLOAD filename\n");
    printf("UPLOAD filename\n");
    printf("HELP\n");
    printf("EXIT\n");
}

void logo()
{
	printf("  ______________________ \n");
    printf(" /_  __/ ____/_  __/ __ \\\n");
    printf("  / / / /_    / / / /_/ /\n");
    printf(" / / / __/   / / / ____/ \n");
	printf("/_/ /_/     /_/ /_/      \n");
}