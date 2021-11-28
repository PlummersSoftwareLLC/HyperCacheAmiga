#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <exec/types.h>
#include <libraries/dos.h>
#include <proto/dos.h>
#include "cache.h"

IMPORT ULONG _Backstdout;
ULONG			 _BackGroundIO = TRUE;
BOOL			  cli;


void bputs(char *outstring)
{
	if (cli)
		Write(_Backstdout, outstring, strlen(outstring));
}

void bprintf (char *format,...)
{
char tempbuf[255];
va_list ptr;

	va_start(ptr,format);
	vsprintf(tempbuf, format, ptr);
	bputs(tempbuf);
	va_end  (ptr);	
}
