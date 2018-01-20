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
	
	fd = open("/dev/at24c02",O_RDWR);
	if(0 > fd){
		printf("can't open device.\n");
		return -1;
	}
	
	while(1)
	{	
		printf("[w]: write\n");
		printf("[r]: read\n");
		
		scanf("%c",&ch);
		
		switch(ch)
		{
			case 'w':
			case 'W':
				printf("please enter addr and dat:");
				scanf("%d %d",&addr,&dat);
				
				buf[0] = addr;
				buf[1] = dat;
				
				if((len = write(fd,buf,2)) == 2)
				{
					printf("write %d byte data sucessful.\n",len);
				}
				break;
			
			case 'r':
			case 'R':
				printf("please enter addr to read:");
				scanf("%d",&addr);
				
				if((len = read(fd,&addr,1) == 1))
				{
					printf("data is %d:\n",addr);
				}
				break;
		}
		
		fflush(NULL);
	}
	
	return 0;
}