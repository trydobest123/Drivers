#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

struct pollfd fd;

int main(int argc, char **argv)
{
	int iFd;
	int Buf;
	int iRet;
	int iBack = 0;
	
	printf("I am two.\n");
	iFd = open("/dev/keydev",O_RDWR);
	if(iFd < 0)
	{
		printf("can't open device.\n");
		return -1;
	}
	
	fd.fd  = iFd;
	fd.events = POLLIN;
	
	while(1)
	{	
		iRet = poll(&fd,1,5000);
		if(iRet < 0)
			printf("error.\n");
			
		if(iRet == 0)
			printf("no data within 5s.\n");
		if(iRet > 0)
		{
			if(fd.revents & POLLIN)
			{
				read(iFd,&Buf,1);
				printf("KeyNum = 0x%x\n",Buf);
			}
		}
	}
	
	close(iFd);
	return 0;
}