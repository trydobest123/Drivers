#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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
	
	while(1)
	{	
		iRet = read(iFd,&Buf,1);
		if(iRet == -1)
		{
			printf("read error.\n");
			close(iFd);
		}
		
		if(iBack == Buf)
			printf("No data read.\n");
		else
		{
			printf("KeyNum = 0x%x\n",Buf);
			iBack = Buf;
		}
	}
	
	close(iFd);
	return 0;
}