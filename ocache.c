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
#include <libraries/dos.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libraries/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include "cache.h"

int 							 devopen = NULL;	/* Flag: has the device been opened */
struct MsgPort  			*devport = NULL;	/* Device's message port				*/
struct MsgPort 			*infoport= NULL;	/* The info server port					*/
struct IOStdReq 			*IO		= NULL;	/* All IO goes through here now. 	*/
struct SignalSemaphore 	*ss		= NULL;	/* To force single threading	 		*/
														/* through the code responsible	 	*/
														/* for updating the cache	 			*/

														/* Pointer to the old vector			*/  
void (*__asm oldbeginio)(register __a1 struct IOStdReq *,
								 register __a6 struct Device *dev);


void CopyMemQuick(char *, char *, long);

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
IMPORT BOOL  killcache;

IMPORT ULONG sectormask;
IMPORT ULONG linemask;

IMPORT BOOL  cli;
IMPORT ULONG _Backstdout;

struct cache_line *cache = NULL;

/*
 * A buffer for scsidisk.device to go through.
 */
char __chip *globbuffer = NULL;

/* 
 * This functions checks the cache. If the sector is there, it returns
 * the buffer, otherwise it returns NULL.
 */
char *FindCache(union sector *s,int set) 
{
   return & (cache[set * lines + s->s.line].buffer[s->s.offset * ITEM_SIZE ] ) ;
}

/*
 * Scan for sector, and return the set it resides in.
 */
int FindEntry(union sector *s) 
{
int set;

   for (set = 0; set < sets; set++) 
      if (cache[set * lines + s->s.line].valid) {
         if (cache[set * lines + s->s.line].key == s->s.key) {
            cache[set * lines + s->s.line].age = counter ++ ;
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

   for (set = 0; set < sets; set++) 
      if (cache[set * lines + s->s.line].valid) {
         if (cache[set * lines + s->s.line].key != s->s.key) {
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
            age = cache[set * lines + s->s.line].age ;
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

   cache[set * lines + s->s.line].age = counter ++ ;
   cache[set * lines + s->s.line].key = s->s.key ;

   /*
    * If no buffer, allocate one.
    */
   if (! cache[set * lines + s->s.line].buffer )
      if (cache[set * lines + s->s.line].buffer = AllocMem(linesize * ITEM_SIZE, MEMF_PUBLIC))
			allocnum++;

   /*
    * If STILL no buffer, return failure. Otherwise set the VALID flag.
    */

   if (cache[set * lines + s->s.line].buffer ) {
      cache[set * lines + s->s.line].valid = 1 ;
/*      allocnum ++ ; */
      }
   else {
      cache[set * lines + s->s.line].valid = 0 ;         /* Allocation failed */
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
   IO->io_Offset = s->sector * ITEM_SIZE ;
   IO->io_Length = linesize * ITEM_SIZE ;
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

   dest = cache[set * lines + s->s.line].buffer ;

   CopyMemQuick(globbuffer, dest, linesize * ITEM_SIZE) ;

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
   IO->io_Offset = linestart * ITEM_SIZE ;
   IO->io_Length = unread * ITEM_SIZE ;
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
         CopyMemQuick(buffer,cache[set * lines + s.s.line].buffer, linesize * ITEM_SIZE) ;
         }

      s.sector += linesize ;
      unread -= linesize ;
      buffer += (linesize * ITEM_SIZE) ;
      }

   ReleaseSemaphore(ss) ;
   return 0 ;
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
   cache[set * lines + s->s.line].valid = 0 ;
}

int CacheUpdate(union sector *s, int seccount, char *buffer) 
{
int set ;

   ObtainSemaphore(ss) ;

   while (seccount) {
      set = FindEntry(s) ;
      if (set >= 0) {
         CopyMemQuick(buffer,FindCache(s,set), ITEM_SIZE) ;
         cache[set * lines + s->s.line].age = counter ++ ;
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

	if (req->io_Unit == IO->io_Unit) {
	
	   s.sector = req->io_Offset / ITEM_SIZE ;	/* Starting block				*/
	   secnum   = req->io_Length / ITEM_SIZE ;	/* Number of blocks			*/
	   command  = req->io_Command ;
	   buffer   = (char *) req->io_Data ;
	
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
	               buffer += ( unread * ITEM_SIZE ) ;
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

	if (!(cache = AllocMem(sizeof(struct cache_line) * sets * lines, MEMF_PUBLIC | MEMF_CLEAR)))
		Cleanexit("Error allocating memory for cache table.");

	if (!(globbuffer = AllocMem(linesize * ITEM_SIZE, MEMF_PUBLIC | MEMF_CLEAR)))
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

   oldbeginio = SetFunction((struct Library *)IO->io_Device,DEV_BEGINIO,(__fptr)mybeginio) ;

	bprintf("[Cache installed successfully]\n");
	InfoServer(infoport); 

   ObtainSemaphore(ss) ;
   SetFunction((struct Library *)IO->io_Device,DEV_BEGINIO,(__fptr)oldbeginio) ;
   ReleaseSemaphore(ss) ;

	Cleanup();
	bprintf("[Cache removed successfully]\n");

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
	            FreeMem( cache[set * lines + line].buffer,linesize * ITEM_SIZE ) ;
	            allocnum -- ;
	         }
	}

   if (allocnum)
      bprintf("Allocation mismatch. %d buffers left lying around\n",allocnum) ;

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
		FreeMem(globbuffer, linesize * ITEM_SIZE);
	if (cache)
		FreeMem(cache, sizeof(struct cache_line) * sets * lines);
   if (_Backstdout) 
		Close(_Backstdout);
}
