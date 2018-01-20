#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc,char **argv)
{	
	int fd;
	int len;
	char buf[2];
	char ch;
	int dat,addr;
	int x,y,i;
	
	fd = open("/dev/at24c02",O_RDWR);
	if(0 > fd){
		printf("can't open device.\n");
		return -1;
	}
	
	printf("write...");
	for(i = 1; i < 50; i++)
	{
		buf[0] = i;
		buf[1] = i+10;
				
		if((len = write(fd,buf,2)) != 2)
		{
			printf("write error.\n");
		}		
	}
	
	printf("read...\n");
	sleep(1);
	
	for(i = 1; i < 100; i++)
	{	
		for(x = 0; x < 10; x++)
		{
			for(y = 0; y < 10; y++)
			{	
				addr = i++;
				read(fd,&addr,1);
				printf("%d	",addr);
			}
			
			printf("\n\n");
		}
	}
}
		