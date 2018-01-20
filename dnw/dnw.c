#include <stdio.h>
#include <string.h>
#include <stdlib.h> 
 #include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
       
int main(int argc,char **argv)
{
	unsigned char *data_buf= NULL;	
	unsigned char file_name[64];
	unsigned int default_address = 0x32000000;
	unsigned int remain_size,cur_size,blocks;
	unsigned int total_write;
	int dev_fd = 0,file_fd  = 0;
	struct stat file_stat;
	
	if(argc < 2)
	{
		printf("Usage: dnw [downloadaddress] <filename>\n");
		return -1;	
	}
	
	if(argc > 2)
	{
		    default_address = strtol(argv[1],NULL,16);	
		    strcpy(file_name,argv[2]);
	}
	else
	{
			default_address = 0x32000000;
			strcpy(file_name,argv[1]);
	}
	
	dev_fd = open("/dev/secbulk0",O_RDWR);
	if(dev_fd < 0)
	{
		printf("open device fail.\n");
			goto error;	
	}
	
	file_fd = open(file_name,O_RDONLY);
	if(file_fd <0)
	{
		printf("open file fail.\n");	
			goto error;	
	}
	
	if(fstat(file_fd,&file_stat) == -1)
	{
		printf("get file size fail.\n");
		return -1;	
	}
	
	data_buf = (unsigned char *)malloc(file_stat.st_size + 10);
	if(data_buf == NULL)
	{
		printf("malloc space fail.\n");
			goto error;	
	}
	
	*((unsigned int *)data_buf)       = default_address;
	*((unsigned int *)data_buf + 1) = file_stat.st_size;
	
	if(file_stat.st_size != read(file_fd,data_buf+8,file_stat.st_size))
	{
			printf("load file fail.\n");	
				goto error;	
	}
	
	printf("File Name:%s\n",file_name); 
	printf("File Size:%d bytes\n",file_stat.st_size);
	printf("Download Address: 0x%08x\n",default_address);
	
	printf("Write Data......\n");
	
	remain_size = file_stat.st_size + 10;
	blocks = remain_size / 10;
	total_write = 0;
	do{
			cur_size = (remain_size > blocks) ? blocks:remain_size;
			if(cur_size != write(dev_fd,data_buf + total_write,cur_size))
			{
					printf("write error.\n");
					goto error;		
			}
			
			remain_size -= cur_size;
			total_write += cur_size;
			printf("\r%d%% %d bytes     ", total_write*100/(file_stat.st_size+10), total_write);

			fflush(stdout);
		}while(remain_size > 0);
		
		printf("\n");
		printf("OK.\n");
	
error:		
		if(file_fd < 0)
			close(file_fd);
		if(dev_fd < 0)
			close(dev_fd);
		if(data_buf < 0)
			free(data_buf);
		return 0;	
}