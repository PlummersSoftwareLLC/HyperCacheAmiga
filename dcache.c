#include <exec/types.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <exec/exec.h>
#include <exec/ports.h>
#include <exec/devices.h>
#include <libraries/dos.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libraries/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>


/* NOTE: This code is designed to support multi-threaded execution, but needs
**       to contain these global variables so that the detached code segment
**       that replaces the exec beginio() vector can access the device 
**       structure that was originally opened.  In order for the executable
**       to remain pure, it must be linked with cres.o 
**
**       A semaphore system is used to make sure that that the critical 
**       sections are single-threaded on writes.  Since one task can be
**       writing to the device (the cache) and several can be reading,
**       the write task must perform the action as an atomic operation.
*/

int devopen;	
struct MsgPort  			*devport = NULL;	/* Device's message port				*/
struct IOStdReq 			*IO		= NULL;	/* All IO goes through here now. 	*/
struct SignalSemaphore 	*ss		= NULL;	/* To force single threading	 		*/
														/* through the code responsible	 	*/
														/* for updating the cache	 			*/

/*
 * Constants. Adjust for adjusting cache size and shape.
 */

#define ITEM_SIZE 512           /* No change - sector size */
#define LINE_SIZE   4           /* Increase for prefetch */
#define SETS        8           /* N-way set associative */
#define LINES      32           /* N lines of cache      */

ULONG counter;
ULONG allocnum 	= 0;
ULONG reads 		= 0;
ULONG readhits 	= 0;
ULONG writes 		= 0;
ULONG sectorsize;

IMPORT ULONG linesize;			/* from arg.c */
IMPORT ULONG sets;
IMPORT ULONG lines;
IMPORT UBYTE *device;
IMPORT int   unit;

/*
 * Bit fields. Adjust to match above constants.
 */

#define ITEM_BITS  2            /* 2^ITEM_BITS = LINE_SIZE */
#define LINE_BITS  5            /* 2^LINE_BITS = LINES     */

struct bitaddress {
   int key:32-LINE_BITS-ITEM_BITS,
       line:LINE_BITS,
       offset:ITEM_BITS ;
};

union sector {
   int sector ;
   struct bitaddress s ;
};

struct cache_line {
   int key ;                     /* Item key */
   int age ;                     /* AGE for LRU alg */
   int valid ;
   char *buffer ;                /* [LINE_SIZE][ITEM_SIZE] of DATA */
};

struct cache_line *cache = NULL;

/*
 * A buffer for the device to go through.
 */

char __chip globbuffer[LINE_SIZE << 9] ;

#define DEV_BEGINIO (-30)
#define DEV_ABORTIO (-36)

/*
	Prototypes
*/

void (*__asm oldbeginio)(register __a1 struct IOStdReq *,
								 register __a6 struct Device *dev);
void 	Cleanup				(void);
void	InfoServer			(void);
void 	Cleanexit			(char *errormsg);
int 	FindEntry			(union sector *s);
int 	AllocCache			(union sector *s);
int 	ReadCache			(union sector *s);
int 	ReadBufferToCache	(int 	 linestart,	int unread,		char *buffer);
char *FindCache			(union sector *s, int set);
int 	NextEntry			(union sector *s, int set);
void 	ClearEntry			(union sector *s, int set);
int 	CacheUpdate			(union sector *s, int seccount, 	char *buffer);
int 	parse_args			(int argc,        char **argv);
void	usage					(void);
/*
 * Scan for sector, and return the set it resides in.
 */

#define cache_member(set, line) cache[ (set) * lines + (line) ]

int FindEntry(union sector *s) 
{
int set;

   for (set = 0; set < SETS; set++) 
      if (cache_member(set, s->s.line).valid) {
         if (cache_member(set, s->s.line).key == s->s.key) {
            cache_member(set, s->s.line).age = counter ++ ;
            return set ;
            }
         }
      else
         break ;

   return -1 ;
}

/*
 * Pick a set from the associated cache, and return
 * the set number. The cache memory is also allocated
 * before returning. The cache entry is marked VALID. so if you
 * can't fill it, remember to clear it!
 */

int AllocCache(union sector *s) 
{
int set ;
int oldest ;
int oldset ;
int found ;
int age ;

   oldset = 0 ;
   oldest = 0 ;
   found  = 0 ;

   for (set = 0; set < SETS; set++) 
      if (cache_member(set, s->s.line).valid) {
         if (cache_member(set, s->s.line).key != s->s.key) {
            /*
             * This 'age' calculation is complicated since normally, 
             * AGE = COUNTER - CACHE.AGE, however, if counter has
             * wrapped to zero, such evaluation evaluates ages of < 0 and
             * these entries will never be reselected for reuse.
             *
             * If counter wraps to zero, then CACHE.AGE is generally larger
             * than COUNTER, so we evaluate age as the total of MAXINT-AGE 
             * plus the current value of the counter. IE, how much it took to
             * wrap and reach the current position.
             *
             * Hence, 
             * AGE = ~0 - CACHE.AGE + COUNTER
             */
            age = cache_member(set, s->s.line).age ;
            if (age > counter)
               age = ((ULONG) ~0 - age) + counter ;
            else
               age = counter - age ;

            if (age > oldest)
               oldest = age, oldset = set ;
            }
         else {
            found = 1 ;
            break ; /* key = s.key. Line already in cache */
            }
         }
      else
         break ; /* !valid. Found a free line */

   if (found) {
      return -1 ;
      }

   if (set == SETS)
      set = oldset ;

   cache_member(set, s->s.line).age = counter ++ ;
   cache_member(set, s->s.line).key = s->s.key ;

   /*
    * If no buffer, allocate one.
    */
   if (! cache_member(set, s->s.line).buffer )
      cache_member(set, s->s.line).buffer = AllocMem(LINE_SIZE << 9, MEMF_PUBLIC) ;

   /*
    * If STILL no buffer, return failure. Otherwise set the VALID flag.
    */

   if (cache_member(set, s->s.line).buffer ) {
      cache_member(set, s->s.line).valid = 1 ;
      allocnum ++ ;
      }
   else {
      cache_member(set, s->s.line).valid = 0 ;         /* Allocation failed */
      return -1 ;
      }

   return set ;
   }

/*
 * Allocate a line of cache and read it from disk.
 */

int ReadCache(union sector *s) 
{
struct MsgPort *port;
char *dest;
int  set;

   ObtainSemaphore(ss);

   port = CreatePort(NULL, NULL) ;

   if (!port) {
      ReleaseSemaphore(ss) ;
      return -1 ;
      }

   if (s->s.offset)
      s->s.offset = 0 ;

   IO->io_Message.mn_ReplyPort = port ;

   IO->io_Command = CMD_READ;
   IO->io_Offset = s->sector << 9 ;
   IO->io_Length = LINE_SIZE << 9 ;
   IO->io_Data = (APTR) globbuffer ;

   oldbeginio(IO,IO->io_Device) ;
   WaitIO(IO) ;

   DeletePort(port) ;

   if (IO->io_Error) {
      ReleaseSemaphore(ss) ;
      return -1 ;
      }

   set = AllocCache(s) ;

   if (set < 0) {
      ReleaseSemaphore(ss) ;
      return -1 ;
      }

   dest = cache_member(set, s->s.line).buffer ;

   CopyMemQuick(globbuffer, dest, LINE_SIZE << 9) ;

   ReleaseSemaphore(ss) ;
   return 0 ;
   }

int ReadBufferToCache(int linestart,int unread,char *buffer) 
{
union sector s ;
struct MsgPort *port ;
int set ;

   ObtainSemaphore(ss);
   s.sector = linestart ;

   port = CreatePort(NULL, NULL) ;

   if (!port) {
      ReleaseSemaphore(ss) ;
      return -1 ;
      }

   IO->io_Message.mn_ReplyPort = port ;

   /*
    * Read enough to fill the buffer.
    */

   IO->io_Command = CMD_READ;
   IO->io_Offset = linestart << 9 ;
   IO->io_Length = unread << 9 ;
   IO->io_Data = (APTR) buffer ;

   oldbeginio(IO,IO->io_Device) ;
   WaitIO(IO) ;

   DeletePort(port) ;

   if (IO->io_Error) {
      ReleaseSemaphore(ss) ;
      return -1 ;
      }

   while (unread) {
      set = AllocCache(&s) ;
      if (set < 0) {
         ReleaseSemaphore(ss) ;
         return -1 ;
         }
      else {
         CopyMemQuick(buffer,cache_member(set, s.s.line).buffer, LINE_SIZE << 9) ;
         }

      s.sector += LINE_SIZE ;
      unread -= LINE_SIZE ;
      buffer += (LINE_SIZE << 9) ;
      }

   ReleaseSemaphore(ss) ;
   return 0 ;
}

/* 
 * This functions checks the cache. If the sector is there, it returns
 * the buffer, otherwise it returns NULL.
 */

char *FindCache(union sector *s,int set) 
{
   return & (cache_member(set, s->s.line).buffer[s->s.offset << 9 ] ) ;
}

/*
 * This function takes 'sector' and set, and decides if the next sector
 * is in the cache.
 */

int NextEntry(union sector *s, int set) 
{
int line ;

   line = s->s.line ;
   s->sector ++ ;

   if (line == s->s.line) {
      return set ;
      }
   else
      return FindEntry(s) ;
   }

/*
 * Search for sector, and mark it invalid.
 */

void ClearEntry(union sector *s,int set) 
{
   cache_member(set, s->s.line).valid = 0 ;
}

int CacheUpdate(union sector *s, int seccount, char *buffer) 
{
int set ;

   ObtainSemaphore(ss) ;

   while (seccount) {
      set = FindEntry(s) ;
      if (set >= 0) {
         CopyMemQuick(buffer,FindCache(s,set), ITEM_SIZE) ;
         cache_member(set, s->s.line).age = counter ++ ;
         }

      buffer += ( ITEM_SIZE ) ;
      seccount -- ;
      s->sector ++ ;
      }

   ReleaseSemaphore(ss) ;
   return 0 ;
}

void __saveds __asm mybeginio(register __a1 struct IOStdReq *req,
                              register __a6 struct Device *dev) 
{
union sector s;
int   set;
int   command;
int   secnum;
char  *source;
char  *buffer;

int   unread;
int   linestart;

	if (req->io_Unit->unit_flags == IO->io_Unit->unit_flags) {
	
	   s.sector = req->io_Offset >> 9 ;
	   secnum   = req->io_Length >> 9 ;
	   command  = req->io_Command ;
	   buffer   = (char *) req->io_Data ;
	
	   if (command == CMD_WRITE) {
			writes++;
	      CacheUpdate(&s,secnum,buffer);
		}
	
	   if (command == CMD_READ) {
			reads++;

	      while (secnum) {
	         set = FindEntry(&s) ;
	
	         if (set < 0) {
	            source = NULL ;
	            }
	         else {
	            source = FindCache(&s,set) ;
	            }

				if (source) 
					readhits++;
	
	         /*
	          * Scan copying buffers to the request.
	          */
	
	         while (secnum && source) {
	            CopyMemQuick(source,buffer,ITEM_SIZE) ;
	            buffer += ITEM_SIZE ;
	
	            secnum -- ;
	            if (secnum) {
	               set = NextEntry(&s, set) ;
	               if (set < 0) {
	                  source = NULL ;
	                  }
	               else {
	                  source = FindCache(&s,set) ;
	                  }
	               }
	            }
	
	         if (!secnum) {                                 /* Done ? */
	            break ;
	            }
	
	         /*
	          * If we are in the middle of a line, read it in.
	          */
	         if (s.s.offset) {
	            int original = s.sector ;
	
	            s.s.offset = 0 ;
	            if (ReadCache(&s) < 0) {
	               }
	
	            s.sector = original ;
	            }
	         else {
	            /*
	             * Start scanning at next line.
	             */
	            unread = 0 ;
	
	            linestart = s.sector ; 
	
	            /*
	             * Scan counting sectors that need reading.
	             */
	            while ((secnum>unread) && (set < 0)) {
	               s.sector = linestart + unread ;
	               set = FindEntry(&s) ;
	               /*
	                * For efficiency, if a sector is not found, advance to
	                * the next line instead of the next sector.
	                */
	               if (set < 0) {
	                  unread += LINE_SIZE ;
	                  }
	               }
	
	            if (unread > secnum) {
	               unread -= LINE_SIZE ;
	               }
	
	            if (unread) {
	               /*
	                * Read the cache into the supplied buffer, and copy
	                * it to cache memory.
	                */
	               ReadBufferToCache(linestart,unread,buffer) ;
	               buffer += ( unread << 9 ) ;
	               }
	             else {
	               /*
	                * If there are more sectors, call 'ReadCache()' to get them.
	                */
	               ReadCache( (union sector *)&linestart) ;
	               }
	
	            /*
	             * Pick up where we left off.
	             */
	            s.sector = linestart + unread ;
	            secnum -= unread ;
	            if (secnum < 0)
	               break ;
	            }
	         }
	      /*
	       * Done!!!
	       */
	      }
	
	   if (command != CMD_READ)
	      oldbeginio(req,dev) ;
	   else {
	      req->io_Actual = req->io_Length ;
	      ReplyMsg((struct Message *) req) ;
	      } ;
	} else {
		oldbeginio(req, dev);	
	}
}

int main(int argc, char *argv[]) 
{
char portname[40];

	if (parse_args(argc, argv))
		return -1;

	/* Build the portname from the device and unit number.
	** eg:        scsi_unit_1_port
	*/

	strcpy(portname, device);
	strtok(portname, ".");
	sprintf((portname + strlen(portname)), "_unit_%d_port", unit);
	puts(portname);
	fflush(stdout);

	if (FindPort(portname)) {
		fprintf(stderr, "HyperCache is already active on this device.\n");
		return -1;
	}

	/* Allocate the two-dimensional cache_line array */

	if (!(cache = (struct cache_line *)
				AllocMem((LINES+1) * (SETS+1) * sizeof(struct cache_line), MEMF_PUBLIC))) {
		fprintf(stderr, "Error allocating cache table.");
		return -1;
	}

	if (!(devport = CreatePort(portname, 0)))
		Cleanexit("An error occurred creating the message port");

	if (!(IO = CreateStdIO(devport)))
		Cleanexit("An error occurred creating the IO request");

	if (OpenDevice(device, unit, (struct IORequest *)IO, 0))
		Cleanexit("An error occurred opening the device");
	else
		devopen = 1;

	if (!(ss = AllocMem(sizeof(struct SignalSemaphore), MEMF_PUBLIC | MEMF_CLEAR)))
		Cleanexit("An error occurred allocating semaphore memory");

   SumLibrary( (struct Library *) IO->io_Device) ; 

   InitSemaphore(ss) ;

   oldbeginio = SetFunction((struct Library *)IO->io_Device,DEV_BEGINIO,(__fptr)mybeginio) ;

	Wait (SIGBREAKF_CTRL_F) ;	/* InfoServer(); */

   ObtainSemaphore(ss) ;
   SetFunction((struct Library *)IO->io_Device,DEV_BEGINIO,(__fptr)oldbeginio) ;
   ReleaseSemaphore(ss) ;

	printf("Stats:  Reads-> %d  Readhits-> %d\n", reads, readhits);

	Cleanup();
   return 0 ;
}

/*
	Display an error message if one is passed, then clean up global allocations
	and exit.
*/

void Cleanexit(char *errormsg)
{
	if (errormsg) 
		fputs(errormsg, stderr);

	Cleanup();
	exit(RETURN_FAIL);
}

/*
	Clean up global allocations 
*/

void Cleanup(void)
{
int line, set;

   for (line = 0; line < LINES; line ++)
      for (set = 0; set < SETS; set ++)
         if (cache_member(set, line).buffer) {
            FreeMem( cache_member(set, line).buffer,LINE_SIZE << 9 ) ;
            allocnum -- ;
         }

	if (cache)
		FreeMem(cache, (lines+1) * (sets+1) * sizeof(struct cache_line));

   if (allocnum)
      printf("Allocation mismatch. %d buffers left lying around\n",allocnum) ;

   if (ss)
      FreeMem(ss,sizeof(struct SignalSemaphore)) ;
   if (devopen) {
      IO->io_Message.mn_ReplyPort = devport ;
      CloseDevice((struct IORequest *)IO) ;
      }
   if (IO)
      DeleteStdIO(IO) ;
   if (devport)
      DeletePort(devport) ;
}
