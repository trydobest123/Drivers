#include <stdio.h> 
#include <unistd.h>  
#include <stdlib.h>  
#include <fcntl.h>  
#include <linux/input.h>   

int main(void)  
{  
	int value ;  
    int type ;  
    struct input_event key_dev ;  
    int fd = open("/dev/event1",O_RDWR);  
	
    if(-1 == fd)
    {
        printf("open key_device event fair!\n");  
        return -1 ;  
    }   
	
    while(1)
    {  
        read(fd ,&key_dev ,sizeof(key_dev));  
		
        switch(key_dev.type)
	{  
            case EV_SYN:  
                 printf("sync!\n");  
			break ;  
			
            case EV_REL:  
            if(key_dev.code == REL_X)
	     {   
                 printf("event_mouse.code_X:%d\n",key_dev.code);      
                 printf("event_mouse.value_X:%d\n",key_dev.value);    
            }  
            if(key_dev.code == REL_Y){  
                 printf("event_mouse.code_Y:%d\n",key_dev.code);      
                 printf("event_mouse.value_Y:%d\n",key_dev.value);    
            }  
		break;
			
		case EV_KEY:
		{
			switch(key_dev.code)
			{
				case KEY_L:
					printf("event_key.code:%d\n",key_dev.code);      
					printf("event_key.value:%d\n",key_dev.value);    
				break;
				
				case KEY_S:
					printf("event_key.code:%d\n",key_dev.code);
					printf("event_key.value:%d\n",key_dev.value);    
				break;
				
				case KEY_ENTER:
					printf("event_key.code:%d\n",key_dev.code);     
					printf("event_key.value:%d\n",key_dev.value);    
				break;
				
				case KEY_DELETE:
					printf("event_key.code:%d\n",key_dev.code);
					printf("event_key.value:%d\n",key_dev.value);    
				break;
				
				default:
				break;
			}

			break;
		}
		
		default:
		break;
		}   
	}
	
    return 0 ;  
}  