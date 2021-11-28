/*
	File		: cache.c
	For		: HyperCache
	Author	: David Plummer
	Created	: April 20, 1992

	Main cache control and startup code.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <exec/exec.h>
#include <exec/ports.h>
#include <exec/devices.h>
#include <devices/trackdisk.h>
#include <libraries/dos.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libraries/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include "cache.h"


void MyCopyMemQuick( ULONG *source, ULONG *dest, int size)
{
	size = size >> 6;

	while (size--) {
		*dest++ = *source++;
		*dest++ = *source++;
		*dest++ = *source++;
		*dest++ = *source++;
		*dest++ = *source++;
		*dest++ = *source++;
		*dest++ = *source++;
		*dest++ = *source++;
		*dest++ = *source++;
		*dest++ = *source++;
		*dest++ = *source++;
		*dest++ = *source++;
		*dest++ = *source++;
		*dest++ = *source++;
		*dest++ = *source++;
		*dest++ = *source++;

	}
}

BOOL 							 devopen = FALSE;	/* Flag: has the device been opened */
struct MsgPort  			*devport = NULL;	/* Device's message port				*/
struct MsgPort 			*infoport= NULL;	/* The info server port					*/
struct IOStdReq 			*IO		= NULL;	/* All IO goes through here now. 	*/
struct SignalSemaphore 	*ss		= NULL;	/* To force single threading	 		*/
														/* through the code responsible	 	*/
														/* for updating the cache	 			*/

														/* Pointer to the old vector			*/  
void (*__asm oldbeginio)(register __a1 struct IOStdReq *,
								 register __a6 struct Device *dev);


ULONG counter;
ULONG allocnum 	= 0;
ULONG reads 		= 0;
ULONG readhits 	= 0;
ULONG writes 		= 0;

IMPORT ULONG sectorsize;
IMPORT ULONG linesize;			/* from arg.c */
IMPORT ULONG sets;
IMPORT ULONG lines;
IMPORT UBYTE *device;
IMPORT int   unit;
IMPORT BOOL  killcache;
IMPORT BOOL  cacheinfo;
IMPORT ULONG itembits;
IMPORT ULONG linebits;

IMPORT ULONG sectormask;
IMPORT ULONG linemask;

IMPORT BOOL  cli;
IMPORT ULONG _Backstdout;
IMPORT BOOL  DiskStatus;

struct cache_line *cache = NULL;

/*
 * A buffer for scsidisk.device to go through.  
 */
char *globbuffer = NULL;


/* This function checks to see if there is a disk currently in the drive.
** It returns 0 if there is, 1 if there is not.
*/

int DiskInDrive(void)
{
struct MsgPort     *port;
int status;

	ObtainSemaphore(ss);

	port = CreatePort(NULL, NULL) ;
	if (!port) {
		ReleaseSemaphore(ss) ;
		return DISK_OUT ;				/* Can't check... assume still in */
	}

   IO->io_Message.mn_ReplyPort = port ;
   IO->io_Command = TD_CHANGESTATE;
   oldbeginio(IO,IO->io_Device) ;
	WaitIO(IO) ;
	status = IO->io_Actual;
	DeletePort(port) ;
	ReleaseSemaphore(ss);

	return status;
}

/* 
 * This functions checks the cache. If the sector is there, it returns
 * the buffer, otherwise it returns NULL.
 * Go ahead, expand this by hand, I dare you...
 */
char *FindCache(ULONG *s,int set) 
{
   return & (cache[set * lines + LINE(*s)].buffer[OFFSET(*s) * sectorsize ] ) ;
}

/*
 * Scan for sector, and return the set it resides in.
 */
int FindEntry(ULONG *s) 
{
int set;
struct cache_line *thecache = cache + LINE(*s);

	for (set = 0; set < sets; set++, thecache += lines)		
      if ((*thecache).valid) {
         if ((*thecache).key == KEY(*s)) {
            (*thecache).age = counter ++ ;
            return set ;
            }
         }
      else
         break ;


/*   for (set = 0; set < sets; set++) 
      if (cache[set * lines + LINE(*s)].valid) {
         if (cache[set * lines + LINE(*s)].key == KEY(*s)) {
            cache[set * lines + LINE(*s)].age = counter ++ ;
            return set ;
            }
         }
      else
         break ;
*/
   return -1 ;
}

/*
 * Pick a set from the associated cache, and return
 * the set number. The cache memory is also allocated
 * before returning. The cache entry is marked VALID. so if you
 * can't fill it, remember to clear it!
 */

 int AllocCache(ULONG *s) 
{
int set ;
int oldest ;
int oldset ;
int found ;
int age ;

	int line_s = LINE(*s);
	int key_s  = KEY(*s);

   oldset = 0 ;
   oldest = 0 ;
   found  = 0 ;

   for (set = 0; set < sets; set++) 
      if (cache[set * lines + line_s].valid) {
         if (cache[set * lines + line_s].key != key_s) {
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
            age = cache[set * lines + line_s].age ;
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

   if (set == sets)
      set = oldset ;

   cache[set * lines + line_s].age = counter ++ ;
   cache[set * lines + line_s].key = key_s ;

   /*
    * If no buffer, allocate one.
    */
   if (! cache[set * lines + line_s].buffer )
      if (cache[set * lines + line_s].buffer = AllocMem(linesize * sectorsize, MEMF_PUBLIC))
			allocnum++;

   /*
    * If STILL no buffer, return failure. Otherwise set the VALID flag.
    */

   if (cache[set * lines + line_s].buffer ) 
      cache[set * lines + line_s].valid = 1 ;
   else {
      cache[set * lines + line_s].valid = 0 ;         /* Allocation failed */
      return -1 ;
      }

   return set ;
   }

/*
 * Allocate a line of cache and read it from disk.
 */

int ReadCache(ULONG *s) 
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

   if (OFFSET(*s))
      *s = *s - OFFSET(*s);	/* s->s.offset = 0 */

   IO->io_Message.mn_ReplyPort = port ;

   IO->io_Command = CMD_READ;
   IO->io_Offset 	= (*s) * sectorsize ;
   IO->io_Length 	= linesize * sectorsize ;
   IO->io_Data 	= (APTR)globbuffer ;

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

   dest = cache[set * lines + LINE(*s)].buffer ;

   CopyMemQuick(globbuffer, dest, (int)(linesize * sectorsize)) ;

   ReleaseSemaphore(ss) ;
   return 0 ;
}

int ReadBufferToCache(int linestart,int unread,char *buffer) 
{
ULONG s ;
struct MsgPort *port ;
int set ;

   ObtainSemaphore(ss);
   s = linestart ;

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
   IO->io_Offset 	= linestart * sectorsize ;
   IO->io_Length 	= unread * sectorsize ;
   IO->io_Data 	= (APTR)buffer ;

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
         CopyMemQuick(buffer,
				 cache[set * lines + LINE(s)].buffer, linesize * sectorsize) ;
         }

      s += linesize ;
      unread -= linesize ;
      buffer += (linesize * sectorsize) ;
      }

   ReleaseSemaphore(ss) ;
   return 0 ;
}

/*
 * This function takes 'sector' and set, and decides if the next sector
 * is in the cache.
 */

int NextEntry(ULONG *s, int set) 
{
int line ;

   line = LINE(*s) ;
   (*s)++ ;

   if (line == LINE(*s)) {
      return set ;
      }
   else
      return FindEntry(s) ;
   }

/*
 * Search for sector, and mark it invalid.
 */

void ClearEntry(ULONG *s,int set) 
{
   cache[set * lines + LINE(*s)].valid = 0 ;
}

int CacheUpdate(ULONG *s, int seccount, char *buffer) 
{
int set ;

   ObtainSemaphore(ss) ;

   while (seccount) {
      set = FindEntry(s) ;
      if (set >= 0) {
         CopyMemQuick(buffer,FindCache(s,set), sectorsize) ;
         cache[set * lines + LINE(*s)].age = counter ++ ;
         }

      buffer += ( sectorsize ) ;
      seccount -- ;
      (*s)++ ;
      }

   ReleaseSemaphore(ss) ;
   return 0 ;
}

char outstr[255];

void __saveds __asm mybeginio(register __a1 struct IOStdReq *req,
                              register __a6 struct Device *dev) 
{
ULONG s;
int   set;
int   command;
int   secnum;
char *source;
char *buffer;

int   unread;
ULONG linestart;

	if (req->io_Unit == IO->io_Unit) {
	
	   s			= req->io_Offset / sectorsize ;	/* Starting block					*/
	   secnum   = req->io_Length / sectorsize ;	/* Number of blocks				*/
	   command  = req->io_Command ;
	   buffer   = (char *)req->io_Data ;

	   if (command == CMD_WRITE) {
			writes += secnum;
	      CacheUpdate(&s,secnum,buffer);
		}
	
	   if (command == CMD_READ) {
			reads += secnum;

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
	            CopyMemQuick(source,buffer,sectorsize) ;
	            buffer += sectorsize ;
	
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
	         if (OFFSET(s)) {
	            int original = s ;
	
	            s = s - OFFSET(s);
	            if (ReadCache(&s) < 0) {
	               }
	
	            s = original ;
	            }
	         else {
	            /*
	             * Start scanning at next line.
	             */
	            unread = 0 ;
	
	            linestart = s ; 
	
	            /*
	             * Scan counting sectors that need reading.
	             */
	            while ((secnum>unread) && (set < 0)) {
	               s   = linestart + unread ;
	               set = FindEntry(&s) ;
	               /*
	                * For efficiency, if a sector is not found, advance to
	                * the next line instead of the next sector.
	                */
	               if (set < 0) {
	                  unread += linesize ;
	                  }
	               }
	
	            if (unread > secnum) {
	               unread -= linesize ;
	               }
	
	            if (unread) {
	               /*
	                * Read the cache into the supplied buffer, and copy
	                * it to cache memory.
	                */
	               ReadBufferToCache(linestart,unread,buffer) ;
	               buffer += ( unread * sectorsize ) ;
	               }
	             else {
	               /*
	                * If there are more sectors, call 'ReadCache()' to get them.
	                */
	               ReadCache( &linestart) ;
	               }
	
	            /*
	             * Pick up where we left off.
	             */
	            s = linestart + unread ;
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

/* This is used to grab all of the cache memory right off the start, since a
** failed allocation request is too damned difficult to deal with in midstream.
*/

int GrabCacheMem(void) 
{
int set, line;

   for (line = 0; line < lines; line++) 
		for (set=0; set<sets; set++)
		   if (! cache[set * lines + line].buffer ) 
      		if (cache[set * lines + line].buffer = AllocMem(linesize * sectorsize, MEMF_PUBLIC)) {
					allocnum++;
			      cache[set * lines + line].valid = 0 ;  /* Allocation OK */
				} else {
					return 1;
				}
	return 0;
}

/* The main section parses the arguments given on the command line, which
** must include a volume name or a device and unit.  From the device and
** unit it builds two message port names, one for the device control and
** one for the local info server.  If the user has specified the quit
** flag on the command line, a message with the INFO_KILL command is sent
** to the info server and then the code is exited.  
**
** If this is not the kill request, but rather the actual invocation of the
** cache, the two ports are created.  If the device port already exists,
** there must already be caching in effect on this device, so the user is
** informed and the program is exited.
**
** If all goes well, control is transfered to the infoserver routine which
** wait for commands, such as KILL or STATS, and services those requests.
*/

int main(int argc, char *argv[]) 
{
char portname[40];
char infoportname[40];
char programname[40];

	cli = (argc != 0);

	if (cli) 
		if (parse_args(argc, argv))
			Cleanexit(NULL);

	/* Build the port names from the device and unit number.
	** eg:        scsi_unit_1_port    scsi_unit_1_info
	*/

	strcpy(portname, device);
	strtok(portname, ".");
	strcpy(infoportname, portname);
	sprintf((programname + strlen(programname)), "_unit_%d_cache", unit);
	sprintf((portname + strlen(portname)), "_unit_%d_port", unit);
	sprintf((infoportname + strlen(infoportname)), "_unit_%d_info", unit);
	
	if (killcache)	{							/* Remove the cache and exit	*/
		struct MsgPort *port_to_kill;	
		if (port_to_kill = FindPort(infoportname)) {
			struct MsgPort *replyport;

			if (!(replyport = CreatePort(NULL, NULL)))
				Cleanexit("Error creating temporary port");
			else {
				struct INFOMessage msg;
				msg.INFO_Msg.mn_Node.ln_Type 	= NT_MESSAGE;
				msg.INFO_Msg.mn_ReplyPort  	= replyport;
				msg.INFO_Msg.mn_Length     	= sizeof(struct INFOMessage);
				msg.INFO_Command 					= INFO_KILL;

				PutMsg(port_to_kill, (struct Message *)&msg);
				WaitPort(replyport);			/* Wait for the reply...		*/
				DeletePort(replyport);		/* Remove the reply port		*/
				Cleanexit(NULL);				/* Exit this thread				*/
			}
		} else 
			Cleanexit("Problem: Can't find that cache's info port!");
	}

	if (cacheinfo)	{							/* Remove the cache and exit	*/
		struct MsgPort *port_to_send;	
		if (port_to_send = FindPort(infoportname)) {
			struct MsgPort *replyport;

			if (!(replyport = CreatePort(NULL, NULL)))
				Cleanexit("Error creating temporary port");
			else {
				struct INFOMessage msg;
				msg.INFO_Msg.mn_Node.ln_Type 	= NT_MESSAGE;
				msg.INFO_Msg.mn_ReplyPort  	= replyport;
				msg.INFO_Msg.mn_Length     	= sizeof(struct INFOMessage);
				msg.INFO_Command 					= INFO_STATS;

				PutMsg(port_to_send, (struct Message *)&msg);
				WaitPort(replyport);			/* Wait for the reply...		*/
				DeletePort(replyport);		/* Remove the reply port		*/

				bprintf("Reads       : %d\n", msg.INFO_Reads);
				bprintf("ReadHits    : %d\n", msg.INFO_Readhits);
				bprintf("Writes      : %d\n", msg.INFO_Writes);
				bprintf("SectorSize  : %d\n", msg.INFO_Sectorsize);
				bprintf("Prefetch    : %d\n", msg.INFO_Linesize);
				bprintf("Lines       : %d\n", msg.INFO_Lines);
				bprintf("Sets        : %d\n", msg.INFO_Sets);
				bprintf("Cache Size  : %dK\n", (msg.INFO_Sectorsize * 
														  msg.INFO_Linesize * 
														  msg.INFO_Sets *
														  msg.INFO_Lines / 1024));

				Cleanexit(NULL);				/* Exit this thread				*/
			}
		} else 
			Cleanexit("Problem: Can't find that cache's info port!");
	}

	if (!(cache = AllocMem(sizeof(struct cache_line) * sets * lines, MEMF_PUBLIC | MEMF_CLEAR)))
		Cleanexit("Error allocating memory for cache table.");

	if (GrabCacheMem())
		Cleanexit("\nNot enough memory for requested cache.  Reduce size using parameters.");

	if (!(globbuffer = AllocMem(linesize * sectorsize, MEMF_CHIP | MEMF_CLEAR)))
		Cleanexit("Error allocating memory for global buffer.");

	if (FindPort(portname)) 
		Cleanexit("HyperCache is already active on this device.");

	if (!(devport = CreatePort(portname, 0)))
		Cleanexit("An error occurred creating the message port");

	if (!(infoport = CreatePort(infoportname, 0)))
		Cleanexit("An error occurred creating the info port");

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

	Disable();
   oldbeginio = SetFunction((struct Library *)IO->io_Device,DEV_BEGINIO,(__fptr)mybeginio) ;
	Enable();

	bprintf("[Cache installed successfully]\n");
	InfoServer(infoport); 

   ObtainSemaphore(ss) ;
	Disable();
   SetFunction((struct Library *)IO->io_Device,DEV_BEGINIO,(__fptr)oldbeginio) ;
	Enable();
   ReleaseSemaphore(ss) ;

	Cleanup();
	bprintf("\n[Cache removed successfully]\n");

   return 0 ;
}

/*
	Display an error message if one is passed, then clean up global allocations
	and exit.
*/

void Cleanexit(char *errormsg)
{
	if (errormsg) 
		bprintf("HCACHE ERROR: %s\n", errormsg);

	Cleanup();

	if (errormsg)
		exit(RETURN_FAIL);
	else
		exit(RETURN_OK);
}

/*
	Clean up global allocations 
*/

void Cleanup(void)
{
int line, set;

	if (cache) {
	   for (line = 0; line < lines; line ++)
	      for (set = 0; set < sets; set ++)
	         if (cache[set * lines + line].buffer) {
	            FreeMem( cache[set * lines + line].buffer,linesize * sectorsize ) ;
	            allocnum -- ;
	         }
	}

   if (allocnum)
      bprintf("Note: %d buffers could not be freed.",allocnum) ;

   if (ss)
      FreeMem(ss,sizeof(struct SignalSemaphore)) ;
   if (devopen) {
      IO->io_Message.mn_ReplyPort = devport ;
      CloseDevice((struct IORequest *)IO) ;
      }
   if (IO)
      DeleteStdIO(IO);
   if (devport)
      DeletePort(devport);
	if (infoport)
		DeletePort(infoport);
	if (globbuffer)
		FreeMem(globbuffer, linesize * sectorsize);
	if (cache)
		FreeMem(cache, sizeof(struct cache_line) * sets * lines);
   if (_Backstdout) 
		Close(_Backstdout);
}
