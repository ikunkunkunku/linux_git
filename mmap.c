/*************************************************************************
    > File Name: mmap.c
    > Author: 冷倾殇
    > Mail: 1500428751@qq.com 
    > Created Time: 2024年06月26日 星期三 11时36分42秒
 ************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc, char *argv[])
{
	int fd;
	struct stat statres;
	char *str;
	int i, count = 0;
	if(argc < 2)
	{
		fprintf(stderr, "usage...\n");
		exit(1);
	}

	fd = open(argv[1], O_RDONLY);
	if(fd < 0)
	{
		perror("open()");
		exit(1);
	}

	if(fstat(fd, &statres) < 0 )
	{
		perror("fstat()");
		exit(1);
	}

	str = mmap(NULL, statres.st_size, PROT_READ, MAP_SHARED, fd, 0);//映射
	if(str == MAP_FAILED)
	{
		perror("1");
		exit(1);
	}
	close(fd);
	
	for(i = 0; i < statres.st_size;i++)
	{
		if(str[i] == 'a')
			count++;
	}
	printf("%d\n", count);

	munmap(str, statres.st_size);
	
}

