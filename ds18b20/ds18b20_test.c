#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

void convert_temp_disp(unsigned char *buf)
{	
	float temp = 0.0;
	unsigned short tmpval = 0;
	unsigned short *tmp_buf = (unsigned short *)buf;
	
	if(buf[1] & 0xf8)//说明是负值
	{
		tmp_buf[1] = ~tmp_buf[1];
		tmp_buf[0] = ~tmp_buf[0];
		
		tmpval = (tmp_buf[0] | (tmp_buf[1] << 8));
		temp   = 0 - tmpval * 0.0625;
	}
	else
	{
		tmpval = (tmp_buf[0] | (tmp_buf[1] << 8));
		temp = tmpval * 0.0625;
	}
	
	printf("Temperature is  [%4.2f `C]\n", temp);
}

int main(int argc, char **argv)
{
	int fd = 0;
	unsigned char buf[2] = {0};
	
	if((fd = open("/dev/ds18b20",O_RDONLY)) < 0)
	{
		printf("no such device.\n");
		return -1;
	}
	
	/* 温度转换并显示*/
	while(1)
	{
		if(read(fd,buf,2) != 2)
		{
			printf("read error.\n");
			close(fd);
			return -1;
		}
		
		convert_temp_disp(buf);
		sleep(2);
	}
	
	close(fd);
	return 0;
}