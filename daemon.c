#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <libraries/dos.h>
#include <stdio.h>
#include <proto/exec.h>

struct INFOMessage {
	struct Message INFO_Msg;	
	ULONG				INFO_Reads;			/* Number of reads from device				*/
	ULONG				INFO_Readhits;		/* Number of cache hits							*/
	ULONG				INFO_Writes;		/* Number of writes to device					*/
	ULONG				INFO_Sectorsize;	/* Size in bytes of device sectors			*/
	ULONG				INFO_Linesize;		/* Size of each cache line						*/
	ULONG				INFO_Sets;			/* Number of sets									*/
	ULONG				INFO_Lines;			/* Number of lines								*/
};

void main(void)
{
struct INFOMessage msg;
char command[80];

	puts("**********************");
	puts("Infoserver test daemon");
	puts("**********************");
	
	printf("> ");
	gets(command);
	while(!strcmp(command, "quit")) {
	   if(!(SafePutToPort((struct Message *)msg, "INFO_SCSI_UNIT_1")))
			break;
		else
			

BOOL SafePutToPort(message, portname)
struct Message *message;
STRPTR          portname;
{
struct MsgPort *port;

	Forbid();
	port = FindPort(portname);
	if (port)
		PutMsg(port,message);
   Permit();
   return((BOOL)port); /* If zero, the port has gone away */
}
