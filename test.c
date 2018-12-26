#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include<string.h>

int main(int argc, char *argv[]) {
	char str[9192];
	int i=0;
	char c='a';
	for(;i<9191;i++)
	{
		
		str[i]=c;
		if((i+1)%400==0)
		c=c+1;
		
	}
	str[i]='\0';
	int fp =0;
	int rw=atoi(argv[2]);
	if(rw==1)
	fp = open(argv[1], O_WRONLY);
	else
	fp=open(argv[1],O_RDONLY);
	
	printf("FP=%d\n", fp);
	if(fp<=0) {
		perror("Error opening file");
		return(-1);
	}
	int off = (int)lseek(fp, 0, SEEK_SET);
	int len=0;
	if(rw==0)
	{
	len = read(fp, str, sizeof(str));
	str[len]=0;
	}
	else
	{
	if(argv[3])
	strcpy(str,argv[3]);
	len=write(fp,str,sizeof(str));
	}
	printf("%d, %d=%s\n", len, (int)(off), str);

	close(fp);
}
