#include <stdio.h>

extern char **environ;

int main(int argc,char **argv)
{
	char **var;
	
	printf("-----------------------\n");
	printf("argv[1] = %s\n",argv[1]);
	
	for(var= environ; *var!=NULL;++var)
	{
		printf("env = %s\n",*var);
	}
	
	printf("-----------------------\n");
	
	return 0;
}