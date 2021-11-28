#include <stdio.h>

#define LINE_SIZE   		4           	/* Increase for prefetch 	*/
#define LINES      		32	    	     	/* N lines of cache      	*/

#define LINE_BITS 5
#define ITEM_BITS 2
#define KEY_BITS LINE_BITS-ITEM_BITS

unsigned int bit_table[16] = { 1, 		2, 		4, 		8,	 	
										 16, 		32,   	64,  		128,  	
										 512,		1024,		2048, 	4096,   
										 8192,   16384,   32768,	65536 
									  }; 


struct bitaddress {
   unsigned int key:32-5-2,
       line:5,
       offset:2 ;
};

union sector {
   unsigned int sector ;
   struct bitaddress s ; 
};

#define KEY(sector) 		(sector >> (itembits + linebits))
#define LINE(sector) 	((sector & linemask) >> itembits)
#define OFFSET(sector)	(sector & sectormask)

void main(void)
{
union sector t;
unsigned int a, linemask, sectormask;
unsigned int linebits, itembits;

	/* Calculate the number of bits needed to represent the range of the
	** lines and the line size.  Eg: If lines = 32 then linebits = 5
	*/

	linebits = 0;
	for (a=0; a<16; a++)
		if (bit_table[a] == LINES)
			linebits = a;
	if (linebits == 0) {
		printf("Number of lines must be an even power of 2.\n");
		exit(0);
	}

	itembits = 0;
	for (a=0; a<16; a++)
		if (bit_table[a] == LINE_SIZE)
			itembits = a;
	if (itembits == 0) {
		printf("The line size must be an even power of 2.\n");
		exit(0);
	}

	/* Build the bitmasks needed to access the bits within the sector
	** number that represent the line and offset.  These are used by
	** the macros to extract these pieces of information.  For example,
	** if linebits = 5 and itembits = 2, then linemask = 00...01111100
	** and sectormask = 00...0000011
	*/

	linemask = 1;
	for (a=0; a<linebits-1; a++) 
		linemask = (linemask << 1) + 1;		
	linemask <<= itembits;

	sectormask = 1;
	for (a=0; a<itembits-1; a++)
		sectormask = (sectormask << 1) | 1;

	t.sector = 0xFFFFFFFF;

	printf("Linebits     : %d\n", linebits);
	printf("Itembits     : %d\n", itembits);
	printf("Key Value    : %X %X\n", t.s.key,    KEY(t.sector));
	printf("Line Value   : %X %X\n", t.s.line,   LINE(t.sector));
	printf("Offset Value : %X %X\n", t.s.offset, OFFSET(t.sector));
}
