/* This file was manually recreated because it was missing in the source tree. */
#ifndef NODETYPENAME
#ifdef DEBUG
static const char * const nodenames[] = {
	"NSEMI",
	"NCMD",
	"NPIPE",
	"NREDIR",
	"NBACKGND",
	"NSUBSHELL",
	"NAND",
	"NOR",
	"NIF",
	"NWHILE",
	"NUNTIL",
	"NFOR",
	"NCASE",
	"NCLISTCONT",
	"NCLIST",
	"NDEFUN",
	"NARG",
	"NTO",
	"NCLOBBER",
	"NFROM",
	"NFROMTO",
	"NAPPEND",
	"NTOFD",
	"NFROMFD",
	"NHERE",
	"NXHERE",
	"NNOT",
	"NDNOT",
};
#define NODETYPENAME(type) ((unsigned)(type) < (sizeof(nodenames)/sizeof(nodenames[0])) ? nodenames[(type)] : "UNKNOWN")
#else
#define NODETYPENAME(type) "node"
#endif
#endif
