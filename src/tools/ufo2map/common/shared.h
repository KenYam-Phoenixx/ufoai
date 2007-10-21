/**
 * @brief
 * @file shared.h
 */

#ifndef _SHARED_H
#define _SHARED_H

#include "../../../common/unzip.h"
#include "../../../shared/shared.h"
#include "../../../shared/ufotypes.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#include <assert.h>
#include <limits.h>

#ifndef errno
extern int errno;
#endif

/*========================================================================
The .pak files are just a linear collapse of a directory tree
========================================================================*/

#define MAX_QPATH 64
#define MAX_OSPATH 128

typedef struct qFILE_s {
	void *z; /**< in case of the file being a zip archive */
	FILE *f; /**< in case the file being part of a pak or the actual file */
	char name[MAX_OSPATH];
	unsigned long filepos;
	unsigned long size;
} qFILE;

typedef struct mapConfig_s {
	float subdivideSize;
	int nice;
	qboolean verbose;
	qboolean noprune;
	qboolean nodetail;
	qboolean fulldetail;
	qboolean onlyents;
	qboolean nomerge;
	qboolean nowater;
	qboolean nofill;
	qboolean nocsg;
	qboolean noweld;
	qboolean noshare;
	qboolean nosubdiv;
	qboolean notjunc;
	qboolean verboseentities;
	qboolean nobackclip;
	qboolean noradiosity;
	int block_xl, block_xh, block_yl, block_yh;
	float microvolume;
	unsigned int numbounce;
	qboolean extrasamples;
	float subdiv;
	qboolean dumppatches;
	float ambient_red;
	float ambient_green;
	float ambient_blue;
	float maxlight;
	float lightscale;
	byte lightquant;
	float direct_scale;
	float entity_scale;
} mapConfig_t;

extern mapConfig_t config;

void U2M_ProgressBar(void (*func) (unsigned int cnt), unsigned int count, qboolean showProgress, const char *id);

#include "../../../common/qfiles.h"

extern qboolean verbose;

#define SYS_VRB 0 /* verbose support (on/off) */
#define SYS_STD 1 /* standard print level */
#define SYS_WRN 2 /* warnings */
#define SYS_ERR 3 /* error */

/* the dec offsetof macro doesnt work very well... */
#define myoffsetof(type,identifier) ((size_t)&((type *)0)->identifier)

extern char qdir[1024];
extern char gamedir[1024];

void Sys_Error(const char *error, ...) __attribute__((noreturn, format(printf, 1, 2)));
void Com_Printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
void Sys_FPrintf(int flag, const char *text, ...) __attribute__((format(printf, 2, 3)));

#endif /* _SHARED_H */
