/*
	File		: cache.h
	For		: HyperCache
	Author	: David Plummer
	Created	: April 31, 1992

	Constants, structure defs, and macros for HyperCache
*/

#define DEFLT_ITEM_SIZE 512         	/* No change - sector size */
#define DEFLT_LINE_SIZE	4           	/* Increase for prefetch 	*/
#define DEFLT_SETS  		8        	  	/* N-way set associative 	*/
#define DEFLT_LINES 		32	    	     	/* N lines of cache      	*/

#define MAX_LINE_SIZE 	8
#define MIN_LINE_SIZE	2
#define MAX_SETS			32
#define MIN_SETS			2
#define MAX_LINES			512
#define MIN_LINES			4

#define DEV_BEGINIO (-30)
#define DEV_ABORTIO (-36)

#define ITALICS   	"\033[3m"
#define BOLD      	"\033[1m"
#define NORMAL			"\033[0m"

#define DISK_IN			0
#define DISK_OUT			1

/*
 * Macros to allow access to a specific set of bits within the sector value.
 */

#define KEY(sector) 		(sector >> (itembits + linebits))
#define LINE(sector) 	((sector & linemask) >> itembits)
#define OFFSET(sector)	(sector & sectormask)

struct cache_line {
   int  key;                     /* Item key 								*/
   int  age;                     /* AGE for LRU alg 						*/
   int  valid;							/* 1=buffer valid, 0=not				*/
   char *buffer;                	/* [LINE_SIZE][ITEM_SIZE] of DATA 	*/
};

/*
	Infoserver message and commands
*/

struct INFOMessage {
	struct Message INFO_Msg;	
	ULONG				INFO_Reads;			/* Number of reads from device				*/
	ULONG				INFO_Readhits;		/* Number of cache hits							*/
	ULONG				INFO_Writes;		/* Number of writes to device					*/
	ULONG				INFO_Sectorsize;	/* Size in bytes of device sectors			*/
	ULONG				INFO_Linesize;		/* Size of each cache line						*/
	ULONG				INFO_Sets;			/* Number of sets									*/
	ULONG				INFO_Lines;			/* Number of lines								*/
	ULONG				INFO_Command;		/* Command request sent to the server		*/
};

#define INFO_KILL		1					/* Remove caching									*/
#define INFO_RESIZE 	2					/* Change size of cache (not implemented)	*/
#define INFO_STATS	3					/* Get perfomance stats from cache			*/

/*
	Prototypes
*/

void 	Cleanup				(void);
void 	Cleanexit			(char *errormsg);
int 	FindEntry			(ULONG *s);
int 	AllocCache			(ULONG *s);
int 	ReadCache			(ULONG *s);
int 	ReadBufferToCache	(int 	 linestart,	int unread,		char *buffer);
char *FindCache			(ULONG *s, int set);
int 	NextEntry			(ULONG *s, int set);
void 	ClearEntry			(ULONG *s, int set);
int 	CacheUpdate			(ULONG *s, int seccount, 	char *buffer);
void 	bputs					(char *outstring);
void 	bprintf 				(char *format,...);
int 	parse_args			(int argc, char **argv);
void 	InfoServer			(struct MsgPort *infoport);
