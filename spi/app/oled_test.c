#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
      
#include "font.h"

#define OLED_CMD_INIT 0x10000000
#define OLED_CMD_CLEAR_ALL 0x10000001
#define OLED_CMD_CLEAR_PAGE 0x10000010
#define OLED_CMD_SET_POS 0x10000011

static int fd;
static void OledPutChar(unsigned int page,unsigned int col,unsigned char code)
{
		const unsigned char *dot = oled_asc2_8x16[code - ' '];
		
		/*高八位为页地址，低八位为列地址*/
		unsigned int pos = (page << 8) | col;
		ioctl(fd,OLED_CMD_SET_POS,pos);
		write(fd,&dot[0],8);
		
		pos = ((page + 1) << 8) | col;
		ioctl(fd,OLED_CMD_SET_POS,pos);
		write(fd,&dot[8],8);
}	

static int OledPrint(unsigned int page,unsigned int col,unsigned char *str)
{
	int len = strlen(str);
	int point = 0;
	
	if(page > 7 || col > 127)
	{
		printf("invalid value.\n");
		return -1;
	}
	
	ioctl(fd ,OLED_CMD_CLEAR_PAGE,page);
	ioctl(fd ,OLED_CMD_CLEAR_PAGE,page+1);
	while(point < len)
	{
		OledPutChar(page,col,str[point]);
		
		if(col > 127)
		{
			page += 2;
			col = 0;		
			ioctl(fd ,OLED_CMD_CLEAR_PAGE,page);
			ioctl(fd ,OLED_CMD_CLEAR_PAGE,page+1);
		}

		col += 8;
		point++;
	}

	return 0;
}

/*./oled_test init 
 *./oled_test clear 
 *./oled_test page col <str>
 */
static void print_usage(char *usage)
{
	printf("Usage:\n");
	printf("%s init\n",usage);
	printf("%s clear\n",usage);
	printf("%s <page> <col> <str>\n",usage);
	printf("page:0,1,2,...,7 col:0,1,2,...,127\n");	
				
}

int main(int argc,char **argv)
{
	int col = 0;
	int page = 0;
	
	fd = open("/dev/oled_dev",O_RDWR);
	if(fd < 0)
	{
		printf("no such device.\n");
		exit(-1);
	}
	
	if(argc > 4)
	{
			printf("too many args!\n");
			close(fd);
			exit(-1);	
	}
	else if(1 == argc)
	{
		print_usage(argv[0]);
		close(fd);
		exit(-1);	
	}
	else if(2 == argc && !strcmp(argv[1],"init"))
	{
			ioctl(fd,OLED_CMD_INIT);
	}
	else if(2 == argc && !strcmp(argv[1],"clear"))
	{
			ioctl(fd,OLED_CMD_CLEAR_ALL);
	}
	else if(4 == argc)
	{
		page = strtoul(argv[1],NULL,10);
		col  = strtoul(argv[2],NULL,10);
		
		if(OledPrint(page,col,argv[3]) < 0)
		{
				close(fd);		
				exit(-1);
		}
				
	}
	else
	{
		print_usage(argv[0]);
		close(fd);
		exit(-1);
	}

	return 0;
}