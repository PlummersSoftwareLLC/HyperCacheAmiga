#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <libraries/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include "cache.h"

IMPORT reads;
IMPORT readhits;
IMPORT writes;
IMPORT sectorsize;
IMPORT linesize;
IMPORT sets;
IMPORT lines;

int DiskInDrive(void);

#define PORTSIG (1 << infoport->mp_SigBit)
#define TIMESIG (1 << timeport->mp_SigBit)
#define USERSIG (SIGBREAKF_CTRL_F)

int timepending;
extern struct cache_line *cache;

void InfoServer(struct MsgPort *infoport)
{
BOOL 				    ABORT = NULL;		/* Flag: time to terminate						*/
ULONG				    signal;
struct INFOMessage *msg;
struct timerequest *TimerIO;
int	 DiskStatus  = 0;
struct MsgPort     *timeport;

	if (!(timeport = CreatePort(0,0)))
		Cleanexit("Couldn't open timer port");

	if (!(TimerIO = (struct timerequest *) CreateExtIO(timeport, sizeof(struct timerequest))))
		Cleanexit("Couln't set up timer request");

	if (OpenDevice( TIMERNAME, UNIT_VBLANK, (struct IORequest *) TimerIO, 0L))
		Cleanexit("Couldn't open timer device");

	timepending = 0;
	TimerIO->tr_node.io_Command = TR_ADDREQUEST;
	TimerIO->tr_time.tv_secs    = 1;
	TimerIO->tr_time.tv_micro   = 0;
	SendIO((struct IORequest *)TimerIO);
	timepending++;

	for(;;) {
		signal = Wait(PORTSIG | USERSIG | TIMESIG);     /* sleep 'till someone signals 	*/

		if (signal & TIMESIG) {
			GetMsg((struct MsgPort *)timeport);	/* Pull the msg out of queue */
			timepending--;
			if (DiskInDrive()==0) {	/* See if disk is missing from drive */
				if (DiskStatus==1) {
					int line;
					int set;
					bprintf("\nClearing cache...\n");	
				   for (line = 0; line < lines; line++) 
						for (set=0; set<sets; set++)
					      cache[set * lines + line].valid = 0 ;  /* Invalidate Cache */
				}
				DiskStatus=0;
			} else
				DiskStatus=1;

			TimerIO->tr_time.tv_secs  = 3;
			TimerIO->tr_time.tv_micro = 0;
			SendIO((struct IORequest *)TimerIO);
			timepending++;
		}

		if (signal & PORTSIG) {               /* got a signal at the msgport 	*/
			while (msg = (struct INFOMessage *)GetMsg((struct MsgPort *)infoport)) {

				/* Handle command sent to info port */

				if (msg->INFO_Command == INFO_KILL) {	/* Remove cache				*/
					ABORT = TRUE;
				}

				msg->INFO_Reads 		= reads;
				msg->INFO_Readhits 	= readhits;
				msg->INFO_Writes		= writes;
				msg->INFO_Sectorsize = sectorsize;
				msg->INFO_Linesize	= linesize;
				msg->INFO_Sets			= sets;
				msg->INFO_Lines		= lines;

				ReplyMsg((struct Message *)msg);
			}
		}

		if (signal & USERSIG) {	
			while (msg = (struct INFOMessage *)GetMsg((struct MsgPort *)infoport))
				; 	/* Clean up any other pending messages */
         ABORT = TRUE;
		}
  
		if (ABORT) {
			while(timepending--) { 	/* Wait for outstanding timer messages */
				WaitPort(timeport);
				GetMsg((struct MsgPort *)timeport);	
			}

			CloseDevice((struct IORequest *) TimerIO);
			DeleteExtIO((struct IORequest *) TimerIO);
		   DeletePort(timeport);
			return;
		}
	}
}

