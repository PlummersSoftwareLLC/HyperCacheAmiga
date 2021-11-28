/*
	File		: arg.c
	For		: HyperCache
	Author	: David Plummer
	Created	: April 20, 1992

	This code takes the command line arguments given, parses them, and sets
	all of the options required.  It also figures out what device and unit
	number belong to a volume name, or lets the user specify the device and
	unit explicitly.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <libraries/dos.h>
#include <libraries/dosextens.h>
#include <libraries/filehandler.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include "cache.h"


#define BTOCSTR(bstr)	((UBYTE *)((UBYTE *)(BADDR(bstr)) + 1))
char conv_name[40];				/* Used for BCPL to C name conversion		*/

 
UBYTE *ver = "\0$VER: HyperCache version 1.0";

ULONG bit_table[16] = { 1, 		2, 		4, 		8,	 	
							   16, 		32,   	64,  		128,  	
							   512,		1024,		2048, 	4096,   
							   8192,    16384,   32768,	65536 
							 }; 


ULONG sectorsize	= DEFLT_ITEM_SIZE;
ULONG linesize		= DEFLT_LINE_SIZE;
ULONG sets			= DEFLT_SETS;	
ULONG lines			= DEFLT_LINES;
UBYTE *volume 		= NULL;
UBYTE *device 		= NULL;
ULONG	unit     	= 0;
BOOL	killcache	= FALSE;
BOOL  cacheinfo   = FALSE;

ULONG sectormask;				/* Used to hold bit values and masks	*/
ULONG linemask;
ULONG itembits;
ULONG linebits;

void usage(void)
{
	bprintf("\n%s%s%s%s%s%s%s%s%s\n",
		BOLD, 
		"HyperCache v1.0     (c)1992 David Plummer\n", 
		NORMAL,
		"Usage   : HyperCache -v <volume> | -d <device> -u <unit> [options]\n",
		"Options : -p <prefetch_sectors>\n",
		"          -s <#_of_cache_sets>\n",
		"          -l <#_of_cache_lines>\n",
		"          -q {To remove cache}\n",
		"          -i {To get cache info}\n"
	);
}

int parse_args(int argc, char **argv)
{
register char *cp;
ULONG a;

	while (--argc) {				/* step through each argument					*/
		cp = *++argv;				/* set a pointer to current arguement		*/
		if (*cp == '-') {			/* if optional argument parse that			*/
			while ( *++cp) {		/* allow multiple arguments together		*/
				switch (*cp) {		/* test each flag letter of optionals		*/
					case 'h':		/* if help is requested, give it				*/
					case '?':
						usage();
					case 'v':		/* set volume name (we figure out device)	*/
						--argc;		
						volume = (*++argv);
						break;
					case 'd':		/* set device name								*/
						--argc;
						device = (*++argv);
						break;
					case 'u':		/* set unit number of device					*/
						--argc;
						unit   = atoi(*++argv);
						break;
					case 'p':		/* set number of prefetch sectors			*/
						--argc;
						linesize = atoi(*++argv);
						break;		
					case 's':		/* set number of cache sets					*/
						--argc;
						sets = atoi(*++argv);
						break;
					case 'l':		/* set new line size								*/
						--argc;
						lines = atoi(*++argv);
						break;
					case 'q':		/* flag to remove the cache					*/
						killcache = 1;
						break;
					case 'i':		/* flag to get cache info						*/
						cacheinfo = 1;
						break;
					default:			/* if unknown argument, show usage			*/
						bprintf("Unknown option: -%c\n", *cp);
						usage();
						Cleanexit(NULL);
				}
			}
		}
	}

	if (linesize > MAX_LINE_SIZE  ||  linesize < MIN_LINE_SIZE) {
		bprintf("Prefetch must be in the range %d to %d.\n", 
			MIN_LINE_SIZE, MAX_LINE_SIZE);
		Cleanexit(NULL);
	}

	if (sets > MAX_SETS  || sets < MIN_SETS) {
		bprintf("The number of sets must be from %d to %d.\n",
			MIN_SETS, MAX_SETS);
		Cleanexit(NULL);
	}

	if (lines > MAX_LINES || lines < MIN_LINES) {
		bprintf("The number of lines must be within %d to %d.\n",
			MIN_LINES, MAX_LINES);
		Cleanexit(NULL);
	}

	/* Calculate the number of bits needed to represent the range of the
	** lines and the line size.  Eg: If lines = 32 then linebits = 5
	*/

	linebits = 0;
	for (a=0; a<16; a++)
		if (bit_table[a] == lines)
			linebits = a;
	if (linebits == 0) {
		bprintf("Number of lines must be an even power of 2.\n");
		Cleanexit(0);
	}

	itembits = 0;
	for (a=0; a<16; a++)
		if (bit_table[a] == linesize)
			itembits = a;
	if (itembits == 0) {
		bprintf("The line size must be an even power of 2.\n");
		Cleanexit(0);
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


	if (!volume && !device) {
		usage();
		bprintf("You must specify a device or volume.\n");
		Cleanexit(NULL);
	}

	if (volume) 	/* Remove the ':' from volume name if given */
		if (volume[strlen(volume) - 1] == ':')
			volume[strlen(volume) - 1] = '\0';


	if ((volume && device) || (volume && unit)) 
		bprintf("%s%s",
			"You may not specify both a volume name and a device/unit combination\n",
			"at the same time: they are mutually exclusive.\n");

	/*	The following takes the AmigaDOS volume name, without the ':', and finds
	** the device name and unit number associated with that volume.
	*/

	if (volume) {
		struct FileSysStartupMsg *fssm_msg; 
		struct DeviceNode 		 *devlist;
		int l;

		l = strlen(volume);
    	Forbid();
    	devlist = (struct DeviceNode *)
					BADDR(((struct DosInfo *)
					BADDR(((struct RootNode *)
					(DOSBase->dl_Root))->rn_Info))->di_DevInfo);
    	for (;devlist; devlist = (struct DeviceNode *)BADDR(devlist->dn_Next))
      	if (devlist->dn_Type == DLT_DEVICE) {
				char *name2 = (char *)BADDR(devlist->dn_Name);
				int l2 = *name2;
				if (l == l2 && strnicmp(volume, name2 + 1, l) == 0) break;
			}
   	Permit();

		/* If the volume couldn't be found in the device list, report error and
		** abort.
		*/

		if (!devlist) {
			Cleanexit("No device could be associated with your volume name.");
		} 

		fssm_msg = (struct FileSysStartupMsg *)BADDR(devlist->dn_Startup);
		device 	= strcpy(conv_name, BTOCSTR(fssm_msg->fssm_Device));
		unit 		= fssm_msg->fssm_Unit;
	}

	bprintf("%sHyperCache v1.0     (c)1992 David Plummer%s\n\n", BOLD, NORMAL);
	if (!killcache && !cacheinfo) {
		bprintf("[Establishing Cache]\n");
		bprintf("Device   : %s\n", device);
		bprintf("Unit     : %d\n", unit);
		bprintf("Prefetch : %d\n", linesize);
		bprintf("Sets     : %d\n", sets);
		bprintf("Lines    : %d\n", lines);
		bprintf("Size     : %dK\n", (sectorsize * linesize * sets * lines)/1024); 

		if (!strcmp(device, "trackdisk.device")) {
			bprintf("Warning: Diskchange code is disabled in this version.  Although\n");
			bprintf("         it will likely function anyway, I suggest restricting\n");
			bprintf("         yourself to non-removable media, or at least to not\n");
			bprintf("         swapping disks while HyperCache is running. Sorry...\n");
		} 

	} else if (cacheinfo) {
		bprintf("[Signalling cache to provide statistics...]\n");
	} else {
		bprintf("[Removing Cache]\n");
		bprintf("Device   : %s\n", device);
		bprintf("Unit     : %d\n", unit);
	}

	return NULL;
}
