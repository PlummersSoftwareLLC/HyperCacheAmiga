
#include <stdio.h>

struct mystruct {
	int value;
	int *pvalue;
};

#define XSIZE 10
#define YSIZE 10

struct mystruct *array;

#define array_member(x,y) array[x * YSIZE + y]

void main(void)
{
	int x,y,z;

	array = (struct mystruct *)malloc(sizeof(struct mystruct) * (XSIZE) * (YSIZE));

	for (x=0; x<10; x++)
		for (y=0; y<10; y++) {
			array_member(x,y).value = x*y*2;
			array_member(x,y).pvalue = (int *)malloc((sizeof x) * 10);
			for (z=0;z<10;z++)
				array_member(x,y).pvalue[z] = x*y*2;
		}

	for (x=0; x<10; x++) {
		for (y=0; y<10; y++) {
			printf("%4d", *(&(array_member(x,y).pvalue[5])));
			free(array_member(x,y).pvalue);
		}
		puts("");
	}
	free(array);
}

