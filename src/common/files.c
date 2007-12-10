/**
 * @file files.c
 * @brief All of Quake's data access is through a hierchal file system, but the contents of the file system can be transparently merged from several sources.
 * The "base directory" is the path to the directory holding the quake.exe and all game directories.
 * The sys_* files pass this to host_init in quakeparms_t->basedir.
 * This can be overridden with the "-basedir" command line parm to allow code debugging in a different directory.
 * The base directory is only used during filesystem initialization.
 * The "game directory" is the first tree on the search path and directory that all generated files (savegames, screenshots, config files) will be saved to.
 * The game directory can never be changed while quake is executing.
 * This is a precacution against having a malicious server instruct clients to write files over areas they shouldn't.
 */

/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "common.h"

/* in memory */

static cvar_t *fs_basedir;
cvar_t *fs_gamedir;

typedef struct filelink_s {
	struct filelink_s *next;
	char *from;
	int fromlength;
	char *to;
} filelink_t;

filelink_t *fs_links;

searchpath_t *fs_searchpaths;
searchpath_t *fs_base_searchpaths;	/* without gamedirs */

/**
 * @brief Called to find where to write a file (savegames, etc)
 * @note We will use the searchpath that isn't a pack and has highest priority
 */
const char *FS_Gamedir (void)
{
	searchpath_t *search;

	for (search = fs_searchpaths; search; search = search->next) {
		if (search->pack == NULL)
			return search->filename;
	}

	return NULL;
}

/**
 * @brief Convert operating systems path seperators to ufo virtuell filesystem
 * seperators (/)
 * @sa Sys_NormPath
 * @sa Sys_OSPath
 */
void FS_NormPath (char *path)
{
	Sys_NormPath(path);
}

/**
 * @param[in] qpath may have either forward or backwards slashes
 */
static char *FS_BuildOSPath (const char *base, const char *qpath, char *buffer, size_t size)
{
	Com_sprintf(buffer, size, "%s/%s", base, qpath);
	Sys_OSPath(buffer);

	return buffer;
}

int FS_FileLength (qFILE * f)
{
	int pos;
	int end;

	/* FIXME: Implement for zips */
	if (!f->f)
		return 0;

	pos = ftell(f->f);
	fseek(f->f, 0, SEEK_END);
	end = ftell(f->f);
	fseek(f->f, pos, SEEK_SET);

	return end;
}

/**
 * @brief Creates any directories needed to store the given filename
 * @sa Sys_Mkdir
 * @note Paths should already be normalized
 * @sa FS_NormPath
 */
void FS_CreatePath (const char *path)
{
	char pathCopy[MAX_OSPATH];
	char *ofs;

	Q_strncpyz(pathCopy, path, sizeof(pathCopy));

	for (ofs = pathCopy + 1; *ofs; ofs++) {
		/* create the directory */
		if (*ofs == '/') {
			*ofs = 0;
			Sys_Mkdir(pathCopy);
			*ofs = '/';
		}
	}
}

/**
 * @brief For some reason, other dll's can't just call fclose()
 * on files returned by FS_FOpenFile...
 * @sa FS_FOpenFileWrite
 */
void FS_FCloseFile (qFILE * f)
{
	if (f->f)
		fclose(f->f);
	else if (f->z)
		unzCloseCurrentFile(f->z);

	f->f = f->z = NULL;
}

/**
 * @brief Finds the file in the search path.
 * @return filesize and an open qFILE *
 * @note Used for streaming data out of either a pak file or a seperate file.
 * @sa FS_FOpenFile
 */
static int FS_FOpenFileSingle (const char *filename, qFILE * file)
{
	searchpath_t *search;
	char netpath[MAX_OSPATH];
	pack_t *pak;
	int i;
	filelink_t *link;

	file->z = file->f = NULL;

	Q_strncpyz(file->name, filename, sizeof(file->name));

	/* check for links first */
	for (link = fs_links; link; link = link->next)
		if (!Q_strncmp(filename, link->from, link->fromlength)) {
			Com_sprintf(netpath, sizeof(netpath), "%s%s", link->to, filename + link->fromlength);
			file->f = fopen(netpath, "rb");
			if (file->f) {
				Com_DPrintf(DEBUG_ENGINE, "link file: %s\n", netpath);
				return FS_FileLength(file);
			}
			return -1;
		}

	/* search through the path, one element at a time */
	for (search = fs_searchpaths; search; search = search->next) {
		/* is the element a pak file? */
		if (search->pack) {
			/* look through all the pak file elements */
			pak = search->pack;
			for (i = 0; i < pak->numfiles; i++)
				/* found it! */
				if (!Q_strcasecmp(pak->files[i].name, filename)) {
					Com_DPrintf(DEBUG_ENGINE, "PackFile: %s : %s\n", pak->filename, filename);
					/* open a new file on the pakfile */
					if (unzLocateFile(pak->handle.z, filename, 2) == UNZ_OK) {	/* found it! */
						if (unzOpenCurrentFile(pak->handle.z) == UNZ_OK) {
							unz_file_info info;
							Com_DPrintf(DEBUG_ENGINE, "PackFile: %s : %s\n", pak->filename, filename);
							if (unzGetCurrentFileInfo(pak->handle.z, &info, NULL, 0, NULL, 0, NULL, 0) != UNZ_OK)
								Com_Error(ERR_FATAL, "Couldn't get size of %s in %s", filename, pak->filename);
							unzGetCurrentFileInfoPosition(pak->handle.z, &file->filepos);
							file->z = pak->handle.z;
							return info.uncompressed_size;
						}
					}
					/*fseek(*file, pak->files[i].filepos->pos_in_zip_directory, SEEK_SET);*/
					return pak->files[i].filelen;
				}
		} else {
			/* check a file in the directory tree */
			Com_sprintf(netpath, sizeof(netpath), "%s/%s", search->filename, filename);

			file->f = fopen(netpath, "rb");
			if (!file->f)
				continue;

			/*Com_DPrintf(DEBUG_ENGINE, "FindFile: %s\n", netpath);*/
			return FS_FileLength(file);
		}
	}

	file->f = NULL;
	return -1;
}

#define PK3_SEEK_BUFFER_SIZE 65536
int FS_Seek (qFILE * f, long offset, int origin)
{
	int _origin;

	if (f->z) {
		byte	buffer[PK3_SEEK_BUFFER_SIZE];
		int		remainder = offset;

		if (offset < 0 || origin == FS_SEEK_END) {
			Com_Error(ERR_FATAL, "Negative offsets and FS_SEEK_END not implemented "
					"for FS_Seek on pk3 file contents\n");
			return -1;
		}

		switch (origin) {
		case FS_SEEK_SET:
			unzSetCurrentFileInfoPosition(f->z, (unsigned long)f->filepos);
			unzOpenCurrentFile(f->z);
			/* fallthrough */
		case FS_SEEK_CUR:
			while (remainder > PK3_SEEK_BUFFER_SIZE) {
				FS_Read(buffer, PK3_SEEK_BUFFER_SIZE, f);
				remainder -= PK3_SEEK_BUFFER_SIZE;
			}
			FS_Read(buffer, remainder, f);
			return offset;
			break;

		default:
			Com_Error(ERR_FATAL, "Bad origin in FS_Seek");
			return -1;
			break;
		}
	} else if (f->f) {
		switch (origin) {
		case FS_SEEK_CUR:
			_origin = SEEK_CUR;
			break;
		case FS_SEEK_END:
			_origin = SEEK_END;
			break;
		case FS_SEEK_SET:
			_origin = SEEK_SET;
			break;
		default:
			_origin = SEEK_CUR;
			Sys_Error("Bad origin in FS_Seek\n");
			break;
		}
		return fseek(f->f, offset, _origin);
	} else
		Sys_Error("FS_Seek: no file opened\n");
}

/**
 * @returns filesize
 * @sa FS_FOpenFileSingle
 * @sa FS_LoadFile
 */
int FS_FOpenFile (const char *filename, qFILE * file)
{
	int result;

	/* open file */
	result = FS_FOpenFileSingle(filename, file);

	/* nothing corresponding found */
	if (result == -1)
		Com_DPrintf(DEBUG_ENGINE, "FS_FOpenFile: can't find %s\n", filename);

	return result;
}

void FS_FOpenFileWrite (const char *filename, qFILE * f)
{
	if (f->z)
		return;

	f->f = fopen(filename, "wb");
	if (!f->f)
		Com_DPrintf(DEBUG_ENGINE, "Could not open %s for writing\n", filename);
}


/**
 * @brief Just returns the filelength and -1 if the file wasn't found
 * @note Won't print any errors
 */
int FS_CheckFile (const char *filename)
{
	int result;
	qFILE file;

	result = FS_FOpenFileSingle(filename, &file);
	if (result != -1)
		FS_FCloseFile(&file);

	return result;
}

#define	MAX_READ	0x10000		/* read in blocks of 64k */
/**
 * @brief Read a file into a given buffer in memory.
 * @param[out] buffer Pointer to memory where file contents are written to.
 * @param[in] len The length of the supplied memory area.
 * @param[in] f The file which is to be read into the memory area.
 * @return The length of the file contents successfully read and written to
 * memory.
 * @note @c buffer is not null-terminated at the end of file reading
 * @note This function properly handles partial reads so check that the
 * returned length matches @c len.
 * @note Reads in blocks of 64k.
 * @sa FS_LoadFile
 * @sa FS_FOpenFile
 */
#ifdef DEBUG
int FS_ReadDebug (void *buffer, int len, qFILE * f, const char* file, int line)
#else
int FS_Read (void *buffer, int len, qFILE * f)
#endif
{
	int block, remaining;
	int read;
	byte *buf;
	int tries;

	buf = (byte *) buffer;

	if (f->z) {
		read = unzReadCurrentFile(f->z, buf, len);
		if (read == -1) {
#ifdef DEBUG
			Com_Printf("FS_Read: %s:%i (%s)\n", file, line, f->name);
#endif
			Com_Error(ERR_FATAL, "FS_Read (zipfile): -1 bytes read");
		}
		return read;
	}

	remaining = len;
	tries = 0;
	while (remaining) {
		block = remaining;
		if (block > MAX_READ)
			block = MAX_READ;
		read = fread(buf, 1, block, f->f);

		/* end of file reached */
		if (read != block && feof(f->f))
			return (len - remaining + read);

		if (read == 0) {
			/* we might have been trying to read from a CD */
			if (!tries) {
				tries = 1;
			} else {
#ifdef DEBUG
				Com_DPrintf(DEBUG_ENGINE, "FS_Read: %s:%i (%s)\n", file, line, f->name);
#endif
				Com_Error(ERR_FATAL, "FS_Read: 0 bytes read");
			}
		}

		if (read == -1) {
#ifdef DEBUG
			Com_DPrintf(DEBUG_ENGINE, "FS_Read: %s:%i (%s)\n", file, line, f->name);
#endif
			Com_Error(ERR_FATAL, "FS_Read: -1 bytes read");
		}

		/* do some progress bar thing here... */
		remaining -= read;
		buf += read;
	}
	return len;
}

/**
 * @brief Filename are reletive to the quake search path
 * @param[in] buffer a null buffer will just return the file length without loading
 * @param[in] path
 * @return a -1 length means that the file is not present
 * @sa FS_Read
 * @sa FS_FOpenFile
 */
int FS_LoadFile (const char *path, byte **buffer)
{
	qFILE h;
	byte *buf;
	int len;

	/* look for it in the filesystem or pack files */
	len = FS_FOpenFile(path, &h);
	if (!h.f && !h.z) {
		Com_DPrintf(DEBUG_ENGINE, "FS_LoadFile: Could not open %s\n", path);
		if (buffer)
			*buffer = NULL;
		return -1;
	}

	if (!buffer) {
		FS_FCloseFile(&h);
		return len;
	}

	assert(com_fileSysPool);
	buf = (byte*)_Mem_Alloc(len + 1, qtrue, com_fileSysPool, 0, path, 0);
	if (!buf)
		return -1;
	*buffer = buf;

	FS_Read(buf, len, &h);
	buf[len] = 0;

	FS_FCloseFile(&h);

	return len;
}

void FS_FreeFile (void *buffer)
{
	_Mem_Free(buffer, "FS_FreeFile", 0);
}

/**
 * @brief Takes an explicit (not game tree related) path to a pak file.
 * Adding the files at the beginning of the list so they override previous pack files.
 */
static pack_t *FS_LoadPackFile (const char *packfile)
{
	unsigned int i, len, err;
	packfile_t *newfiles;
	pack_t *pack;
	unz_file_info file_info;
	unzFile uf;
	unz_global_info gi;
	char filename_inzip[MAX_QPATH];

	len = strlen(packfile);

	if (!Q_strncmp(packfile + len - 4, ".pk3", 4) || !Q_strncmp(packfile + len - 4, ".zip", 4)) {
		uf = unzOpen(packfile);
		err = unzGetGlobalInfo(uf, &gi);

		if (err != UNZ_OK) {
			Com_Printf("Could not load '%s'\n", packfile);
			return NULL;
		}

		len = 0;
		unzGoToFirstFile(uf);
		for (i = 0; i < gi.number_entry; i++) {
			err = unzGetCurrentFileInfo(uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0);
			if (err != UNZ_OK) {
				break;
			}
			len += strlen(filename_inzip) + 1;
			unzGoToNextFile(uf);
		}

		pack = Mem_PoolAlloc(sizeof(pack_t), com_fileSysPool, 0);
		Q_strncpyz(pack->filename, packfile, sizeof(pack->filename));
		pack->handle.z = uf;
		pack->handle.f = NULL;
		pack->numfiles = gi.number_entry;
		unzGoToFirstFile(uf);

		/* Allocate space for array of packfile structures (filename, offset, length) */
		newfiles = Mem_PoolAlloc(i * sizeof(packfile_t), com_fileSysPool, 0);

		for (i = 0; i < gi.number_entry; i++) {
			err = unzGetCurrentFileInfo(uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0);
			if (err != UNZ_OK)
				break;
			Q_strlwr(filename_inzip);

			unzGetCurrentFileInfoPosition(uf, &newfiles[i].filepos);
			Q_strncpyz(newfiles[i].name, filename_inzip, sizeof(newfiles[i].name));
			newfiles[i].filelen = file_info.compressed_size;
			unzGoToNextFile(uf);
		}
		pack->files = newfiles;

		/* Sort our list alphabetically - also rearrange the unsigned long values */
		qsort((void *)pack->files, i, sizeof(packfile_t), Q_StringSort);

		Com_Printf("Added packfile %s (%li files)\n", packfile, gi.number_entry);
		return pack;
	} else {
		/* Unrecognized file type! */
		Com_Printf("Pack file type %s unrecognized\n", packfile + len - 4);
		return NULL;
	}
}


#define MAX_PACKFILES 1024
/**
 * @brief Adds the directory to the head of the search path
 * @note No ending slash here
 */
static void FS_AddGameDirectory (const char *dir)
{
	searchpath_t *search;
	pack_t *pak;
	char **dirnames = NULL;
	int ndirs = 0, i;
	char pakfile_list[MAX_PACKFILES][MAX_OSPATH];
	int pakfile_count = 0;
	char pattern[MAX_OSPATH];

	Com_Printf("Adding game dir: %s\n", dir);

	Com_sprintf(pattern, sizeof(pattern), "%s/*.zip", dir);
	if ((dirnames = FS_ListFiles(pattern, &ndirs, 0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM)) != 0) {
		for (i = 0; i < ndirs - 1; i++) {
			if (strrchr(dirnames[i], '/')) {
				Q_strncpyz(pakfile_list[pakfile_count], dirnames[i], sizeof(pakfile_list[pakfile_count]));
				pakfile_count++;
				if (pakfile_count >= MAX_PACKFILES) {
					Com_Printf("Warning: Max allowed pakfiles reached (%i) - skipping the rest\n", MAX_PACKFILES);
					break;
				}
			}
			Mem_Free(dirnames[i]);
		}
		Mem_Free(dirnames);
	}

	Com_sprintf(pattern, sizeof(pattern), "%s/*.pk3", dir);
	if ((dirnames = FS_ListFiles(pattern, &ndirs, 0, 0)) != 0) {
		for (i = 0; i < ndirs - 1; i++) {
			if (strrchr(dirnames[i], '/')) {
				Q_strncpyz(pakfile_list[pakfile_count], dirnames[i], sizeof(pakfile_list[pakfile_count]));
				pakfile_count++;
				if (pakfile_count >= MAX_PACKFILES) {
					Com_Printf("Warning: Max allowed pakfiles reached (%i) - skipping the rest\n", MAX_PACKFILES);
					break;
				}
			}
			Mem_Free(dirnames[i]);
		}
		Mem_Free(dirnames);
	}

	/* Sort our list alphabetically */
	qsort((void *)pakfile_list, pakfile_count, MAX_OSPATH, Q_StringSort);

	for (i = 0; i < pakfile_count; i++) {
		pak = FS_LoadPackFile(pakfile_list[i]);
		if (!pak)
			continue;

		search = Mem_PoolAlloc(sizeof(searchpath_t), com_fileSysPool, 0);
		search->pack = pak;
		search->next = fs_searchpaths;
		fs_searchpaths = search;
	}

	/* add the directory to the search path */
	search = Mem_PoolAlloc(sizeof(searchpath_t), com_fileSysPool, 0);
	Q_strncpyz(search->filename, dir, sizeof(search->filename));
	search->next = fs_searchpaths;
	fs_searchpaths = search;
}

/**
 * @note e.g. *nix: Use ~/.ufoai/dir as gamedir
 * @sa Sys_GetHomeDirectory
 */
static void FS_AddHomeAsGameDirectory (const char *dir)
{
	char gdir[MAX_OSPATH];
	char *homedir = Sys_GetHomeDirectory();

	if (homedir) {
#ifdef _WIN32
		Com_sprintf(gdir, sizeof(gdir), "%s/"UFO_VERSION"/%s", homedir, dir);
#else
		Com_sprintf(gdir, sizeof(gdir), "%s/.ufoai/"UFO_VERSION"/%s", homedir, dir);
#endif
		Com_Printf("using %s for writing\n", gdir);
		FS_CreatePath(va("%s/", gdir));

		FS_AddGameDirectory(gdir);
	} else {
		Com_Printf("could not find the home directory\n");
	}
}

void FS_ExecAutoexec (void)
{
	const char *dir;
	char name[MAX_QPATH];

	dir = Cvar_VariableString("fs_gamedir");
	if (*dir)
		Com_sprintf(name, sizeof(name), "%s/%s/autoexec.cfg", fs_basedir->string, dir);
	else
		Com_sprintf(name, sizeof(name), "%s/%s/autoexec.cfg", fs_basedir->string, BASEDIRNAME);
	if (Sys_FindFirst(name, 0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM))
		Cbuf_AddText("exec autoexec.cfg\n");
	Sys_FindClose();
}


/**
 * @brief Sets the gamedir and path to a different directory.
 */
void FS_SetGamedir (const char *dir)
{
	searchpath_t *next;

	if (strstr(dir, "..") || strstr(dir, "/")
		|| strstr(dir, "\\") || strstr(dir, ":")) {
		Com_Printf("Gamedir should be a single filename, not a path\n");
		return;
	}

	/* free up any current game dir info */
	while (fs_searchpaths != fs_base_searchpaths) {
		if (fs_searchpaths->pack) {
			FS_FCloseFile(&fs_searchpaths->pack->handle);

			Mem_Free(fs_searchpaths->pack->files);
			Mem_Free(fs_searchpaths->pack);
		}
		next = fs_searchpaths->next;
		Mem_Free(fs_searchpaths);
		fs_searchpaths = next;
	}

	/* flush all data, so it will be forced to reload */
	if (sv_dedicated && !sv_dedicated->integer)
		Cbuf_AddText("vid_restart\nsnd_restart\n");

	if (!Q_strncmp(dir, BASEDIRNAME, strlen(BASEDIRNAME)) || (*dir == 0)) {
		Cvar_FullSet("fs_gamedir", "", CVAR_LATCH | CVAR_SERVERINFO);
	} else {
		FS_AddGameDirectory(va("%s/%s", fs_basedir->string, dir));
		FS_AddHomeAsGameDirectory(dir);
	}
}


/**
 * @brief Creates a filelink_t
 */
static void FS_Link_f (void)
{
	filelink_t *l, **prev;

	if (Cmd_Argc() != 3) {
		Com_Printf("usage: link <from> <to>\n");
		return;
	}

	/* see if the link already exists */
	prev = &fs_links;
	for (l = fs_links; l; l = l->next) {
		if (!Q_strcmp(l->from, Cmd_Argv(1))) {
			Mem_Free(l->to);
			if (!strlen(Cmd_Argv(2))) {	/* delete it */
				*prev = l->next;
				Mem_Free(l->from);
				Mem_Free(l);
				return;
			}
			l->to = Mem_PoolStrDup(Cmd_Argv(2), com_fileSysPool, 0);
			return;
		}
		prev = &l->next;
	}

	/* create a new link */
	l = Mem_PoolAlloc(sizeof(*l), com_fileSysPool, 0);
	l->next = fs_links;
	fs_links = l;
	l->from = Mem_PoolStrDup(Cmd_Argv(1), com_fileSysPool, 0);
	l->fromlength = strlen(l->from);
	l->to = Mem_PoolStrDup(Cmd_Argv(2), com_fileSysPool, 0);
}


/**
 * @brief Builds a qsorted filelist
 * @sa Sys_FindFirst
 * @sa Sys_FindNext
 * @sa Sys_FindClose
 * @note Don't forget to free the filelist array and the file itself
 */
char **FS_ListFiles (const char *findname, int *numfiles, unsigned musthave, unsigned canthave)
{
	char *s;
	int nfiles = 0, i;
	char **list = NULL;
	char tempList[MAX_FILES][MAX_OSPATH];

	*numfiles = 0;

	s = Sys_FindFirst(findname, musthave, canthave);
	while (s) {
		if (s[strlen(s) - 1] != '.')
			nfiles++;
		s = Sys_FindNext(musthave, canthave);
	}
	Sys_FindClose();

	if (!nfiles)
		return NULL;

	nfiles++; /* add space for a guard */
	*numfiles = nfiles;

	list = Mem_PoolAlloc(sizeof(char*) * nfiles, com_fileSysPool, 0);
	memset(tempList, 0, sizeof(tempList));

	s = Sys_FindFirst(findname, musthave, canthave);
	nfiles = 0;
	while (s) {
		if (s[strlen(s) - 1] != '.') {
			Q_strncpyz(tempList[nfiles], s, sizeof(tempList[nfiles]));
#ifdef _WIN32
			Q_strlwr(tempList[nfiles]);
#endif
			nfiles++;
		}
		s = Sys_FindNext(musthave, canthave);
	}
	Sys_FindClose();

	qsort(tempList, nfiles, MAX_OSPATH, Q_StringSort);
	for (i = 0; i < nfiles; i++) {
		list[i] = Mem_PoolStrDup(tempList[i], com_fileSysPool, 0);
	}

	return list;
}

/**
 * @brief Show the filesystem contents - also supports wildcarding
 */
static void FS_Dir_f (void)
{
	const char *path = NULL;
	char findname[1024];
	char wildcard[1024] = "*.*";
	char **dirnames;
	int ndirs;

	if (Cmd_Argc() != 1)
		Q_strncpyz(wildcard, Cmd_Argv(1), sizeof(wildcard));

	while ((path = FS_NextPath(path)) != NULL) {
		Com_sprintf(findname, sizeof(findname), "%s/%s", path, wildcard);
		FS_NormPath(findname);

		Com_Printf("Directory of %s\n", findname);
		Com_Printf("----\n");

		if ((dirnames = FS_ListFiles(findname, &ndirs, 0, SFF_HIDDEN | SFF_SYSTEM)) != 0) {
			int i;

			for (i = 0; i < ndirs - 1; i++) {
				if (strrchr(dirnames[i], '/'))
					Com_Printf("%s\n", strrchr(dirnames[i], '/') + 1);
				else
					Com_Printf("%s\n", dirnames[i]);

				Mem_Free(dirnames[i]);
			}
			Mem_Free(dirnames);
		}
		Com_Printf("\n");
	}
}

/**
 * @brief Prints search paths and file links
 */
static void FS_Path_f (void)
{
	searchpath_t *s;
	filelink_t *l;

	Com_Printf("Current search path:\n");
	for (s = fs_searchpaths; s; s = s->next) {
		if (s == fs_base_searchpaths)
			Com_Printf("----------\n");
		if (s->pack)
			Com_Printf("%s (%i files)\n", s->pack->filename, s->pack->numfiles);
		else
			Com_Printf("%s\n", s->filename);
	}

	Com_Printf("\nLinks:\n");
	for (l = fs_links; l; l = l->next)
		Com_Printf("%s : %s\n", l->from, l->to);
}

/**
 * @brief Allows enumerating all of the directories in the search path
 * @note ignore pk3 here
 */
const char *FS_NextPath (const char *prevpath)
{
	searchpath_t *s;
	char *prev;

	if (!prevpath)
		return FS_Gamedir();

	prev = NULL;
	for (s = fs_searchpaths; s; s = s->next) {
		if (s->pack)
			continue;
		if (prev && !Q_strcmp(prevpath, prev))
			return s->filename;
		prev = s->filename;
	}

	return NULL;
}

/**
 * @brief Print all searchpaths
 */
static void FS_Info_f (void)
{
	searchpath_t *search;

	Com_Printf("Filesystem information\n");
	Com_Printf("...write dir: '%s'\n", FS_Gamedir());

	for (search = fs_searchpaths; search; search = search->next) {
		if (search->pack == NULL)
			Com_Printf("...path: '%s'\n", search->filename);
		else
			Com_Printf("...pakfile: '%s'\n", search->pack->filename);
	}
}

/**
 * @brief filesystem console commands
 * @sa FS_InitFilesystem
 * @sa FS_RestartFilesystem
 */
static const cmdList_t fs_commands[] = {
	{"fs_restart", FS_RestartFilesystem, "Reloads the file subsystem"},
	{"path", FS_Path_f, "Print search paths and file links"},
	{"link", FS_Link_f, "Create file links"},
	{"dir", FS_Dir_f, "Show the filesystem contents - also supports wildcarding"},
	{"fs_info", FS_Info_f, "Show information about the virtuell filesystem"},

	{NULL, NULL, NULL}
};

/**
 * @sa FS_Shutdown
 * @sa FS_RestartFilesystem
 */
void FS_InitFilesystem (void)
{
	cvar_t* fs_usehomedir;
	const cmdList_t *commands;

	Com_Printf("\n---- filesystem initialization -----\n");

	for (commands = fs_commands; commands->name; commands++)
		Cmd_AddCommand(commands->name, commands->function, commands->description);
	fs_usehomedir = Cvar_Get("fs_usehomedir", "1", CVAR_ARCHIVE, "Use the homedir to store files like savegames and screenshots");

	/* basedir <path> */
	/* allows the game to run from outside the data tree */
	fs_basedir = Cvar_Get("fs_basedir", ".", CVAR_NOSET, "Allows the game to run from outside the data tree");

	/* start up with base by default */
	FS_AddGameDirectory(va("%s/" BASEDIRNAME, fs_basedir->string));

	/* then add a '.ufoai/base' directory in home directory by default */
	if (fs_usehomedir->integer)
		FS_AddHomeAsGameDirectory(BASEDIRNAME);

	/* any set gamedirs will be freed up to here */
	fs_base_searchpaths = fs_searchpaths;

	/* check for game override */
	fs_gamedir = Cvar_Get("fs_gamedir", "", CVAR_LATCH | CVAR_SERVERINFO, "If you want to start a mod not located in "BASEDIRNAME);
	if (fs_gamedir->string[0])
		FS_SetGamedir(fs_gamedir->string);
}

/* FIXME: This block list code is broken in terms of filename order
 * To see the bug reduce the FL_BLOCKSIZE to 1024 and verify the order of the
 * filenames FS_NextScriptHeader gives you - you will see that the last files
 * will be in reversed order
 */

#define FL_BLOCKSIZE	2048
typedef struct listBlock_s {
	char path[MAX_QPATH];
	char files[FL_BLOCKSIZE];
	struct listBlock_s *next;
} listBlock_t;

static listBlock_t *fs_blocklist = NULL;

/**
 * @brief Add one name to the filelist
 * @note also checks for duplicates
 * @sa FS_BuildFileList
 */
static void _AddToListBlock (char** fl, listBlock_t* block, listBlock_t* tblock, char* name)
{
	char *f, *tl = NULL;
	int len;

	/* strip path */
	f = strrchr(name, '/');
	if (!f)
		f = name;
	else
		f++;
/*	Com_Printf("_AddToListBlock: %s\n", name);*/

	/* check for double occurences */
	for (tblock = block; tblock; tblock = tblock->next) {
		tl = tblock->files;
		while (*tl) {
			if (!Q_strcmp(tl, f))
				break;
			tl += strlen(tl) + 1;
		}
		if (*tl)
			break;
	}

	if (tl && !*tl) {
		len = strlen(f);
		if (*fl - block->files + len >= FL_BLOCKSIZE) {
			/* terminalize the last block */
			**fl = 0;

			/* allocate a new block */
			tblock = Mem_PoolAlloc(sizeof(listBlock_t), com_fileSysPool, 0);
			tblock->next = block->next;
			block->next = tblock;

			Q_strncpyz(tblock->path, block->path, sizeof(tblock->path));
			*fl = tblock->files;
			block = tblock;
		}

		/* add the new file */
		Q_strncpyz(*fl, f, len + 1);
		*fl += len + 1;
	}
}

/**
 * @brief Build a filelist
 * @param[in] fileList e.g. ufos\*.ufo to get all ufo files in the gamedir/ufos dir
 */
void FS_BuildFileList (const char *fileList)
{
	listBlock_t *block, *tblock;
	searchpath_t *search;
	char files[MAX_QPATH];
	char findname[1024];
	char **filenames;
	char *fl, *ext;
	int nfiles;
	int i, l;
	pack_t *pak;

	/* bring it into normal form */
	Q_strncpyz(files, fileList, sizeof(files));
	FS_NormPath(files);

	/* check the blocklist for older searches
	 * and do a new one after deleting them */
	for (block = fs_blocklist, tblock = NULL; block;) {
		if (!Q_strncmp(block->path, files, MAX_QPATH)) {
			/* delete old one */
			if (tblock)
				tblock->next = block->next;
			else
				fs_blocklist = block->next;

			Mem_Free(block);

			if (tblock)
				block = tblock->next;
			else
				block = fs_blocklist;
			continue;
		}

		tblock = block;
		block = block->next;
	}

	/* allocate a new block and link it into the list */
	block = Mem_PoolAlloc(sizeof(listBlock_t), com_fileSysPool, 0);
	block->next = fs_blocklist;
	fs_blocklist = block;

	/* store the search string */
	Q_strncpyz(block->path, files, sizeof(block->path));

	/* search for the files */
	fl = block->files;
	nfiles = 0;

	/* search through the path, one element at a time */
	for (search = fs_searchpaths; search; search = search->next) {
		/* is the element a pak file? */
		if (search->pack) {
			ext = strrchr(files, '.');
			/* *.* is not implemented here - only e.g. *.ufo */
			if (!ext)
				break;
			l = strlen(search->filename);
			Com_sprintf(findname, sizeof(findname), search->filename);
			FS_NormPath(findname);

			/* look through all the pak file elements */
			pak = search->pack;
			for (i = 0; i < pak->numfiles; i++)
				/* found it! */
				if (!Q_strncmp(pak->files[i].name, findname, l) && strstr(pak->files[i].name, ext)) {
					_AddToListBlock(&fl, block, tblock, pak->files[i].name);
				}
		} else {
			Com_sprintf(findname, sizeof(findname), "%s/%s", search->filename, files);
			FS_NormPath(findname);

			if ((filenames = FS_ListFiles(findname, &nfiles, 0, SFF_HIDDEN | SFF_SYSTEM)) != 0) {
				for (i = 0; i < nfiles - 1; i++) {
					_AddToListBlock(&fl, block, tblock, filenames[i]);
					Mem_Free(filenames[i]);
				}
				Mem_Free(filenames);
			}
		}
	}

	/* terminalize the list */
	*fl = 0;
}

const char* FS_NextFileFromFileList (const char *files)
{
	static char *fileList = NULL;
	static listBlock_t *_block = NULL;
	listBlock_t *block;
	char *file = NULL;

	/* restart the list? */
	if (files == NULL) {
		_block = NULL;
		return NULL;
	}

	for (block = fs_blocklist; block; block = block->next)
		if (!Q_strncmp(files, block->path, MAX_QPATH))
			break;

	if (!block) {
		FS_BuildFileList(files);
		for (block = fs_blocklist; block; block = block->next)
			if (!Q_strncmp(files, block->path, MAX_QPATH))
				break;
		if (!block) {
			/* still no filelist */
			Com_Printf("FS_NextFileFromFileList: Could not create filelist for %s\n", files);
			return NULL;
		}
	}

	/* everytime we switch between different blocks we get the
	 * first file again when we switch back */
	if (_block != block) {
		_block = block;
		fileList = block->files;
	}

	assert(fileList);

	if (*fileList) {
		file = fileList;
		fileList += strlen(fileList) + 1;
	}

	/* finished */
	return file;
}

/**
 * @brief Returns the buffer of a file
 * @param[in] files If NULL, reset the filelist
 * If not NULL it may be something like ufos\*.ufo (slash) to get a list
 * of all ufo files in base/ufos. Calling FS_GetFileData("ufos\*.ufo");
 * until it returns NULL is sufficient to get on buffer after another.
 * @note You don't have to free the file buffer on the calling side.
 * This is done in this function, too
 */
const char *FS_GetFileData (const char *files)
{
	listBlock_t *block;
	static char *fileList;
	static byte *buffer = NULL;

	if (!files) {
		fileList = NULL;
		return NULL;
	}

	/* free the old file */
	if (buffer) {
		FS_FreeFile(buffer);
		buffer = NULL;
	}

	for (block = fs_blocklist; block; block = block->next)
		if (!Q_strncmp(files, block->path, MAX_QPATH))
			break;

	if (!block) {
		/* didn't find any valid file list */
		fileList = NULL;
		FS_BuildFileList(files);
		for (block = fs_blocklist; block; block = block->next)
			if (!Q_strncmp(files, block->path, MAX_QPATH))
				break;
		if (!block) {
			/* still no filelist */
			Com_Printf("FS_GetFileData: Could not create filelist for %s\n", files);
			return NULL;
		}
	}

	/* list start */
	if (!fileList)
		fileList = block->files;

	if (*fileList) {
		char filename[MAX_QPATH];

		/* load a new file */
		Q_strncpyz(filename, block->path, sizeof(filename));
		strcpy(strrchr(filename, '/') + 1, fileList);

		FS_LoadFile(filename, &buffer);
		/* search a new file */
		fileList += strlen(fileList) + 1;
		return (const char*)buffer;
	}

	/* free the old file */
	if (buffer) {
		FS_FreeFile(buffer);
		fileList = NULL;
		buffer = NULL;
	}

	/* finished */
	return NULL;
}

void FS_SkipBlock (const char **text)
{
	const char *token;
	int depth;

	depth = 1;

	do {
		token = COM_Parse(text);
		if (*token == '{')
			depth++;
		else if (*token == '}')
			depth--;
	} while (depth && *text);
}

char *FS_NextScriptHeader (const char *files, const char **name, const char **text)
{
	static char lastList[MAX_QPATH] = "";
	static listBlock_t *lBlock;
	static char *lFile = NULL;
	static byte *lBuffer = NULL;

	static char headerType[32];
	static char headerName[32];
	listBlock_t *block;
	const char *token;

	if (!text) {
		*lastList = 0;
		return NULL;
	}

	if (Q_strncmp(files, lastList, MAX_QPATH)) {
		/* search for file lists */
		Q_strncpyz(lastList, files, sizeof(lastList));

		for (block = fs_blocklist; block; block = block->next)
			if (!Q_strncmp(files, block->path, MAX_QPATH))
				break;

		if (!block)
			/* didn't find any valid file list */
			return NULL;

		lBlock = block;
		lFile = block->files;
	}

	while (lBlock) {
		if (lBuffer) {
			/* continue reading the current file */
			if (*text) {
				token = COM_Parse(text);
				if (*token == '{') {
					FS_SkipBlock(text);
					continue;
				}

				Q_strncpyz(headerType, token, sizeof(headerType));
				if (*text) {
					token = COM_Parse(text);
					Q_strncpyz(headerName, token, sizeof(headerName));
					*name = headerName;
					return headerType;
				}
			}

			/* search a new file */
			lFile += strlen(lFile) + 1;

			while (!*lFile && lBlock)
				/* it was the last file in the block, continue to next block */
				for (lBlock = lBlock->next; lBlock; lBlock = lBlock->next)
					if (!strcmp(files, lBlock->path)) {
						lFile = lBlock->files;
						break;
					}
		}

		if (*lFile) {
			char filename[MAX_QPATH];

			/* free the old file */
			if (lBuffer) {
				FS_FreeFile(lBuffer);
				lBuffer = NULL;
			}

			/* load a new file */
			Q_strncpyz(filename, lBlock->path, sizeof(filename));
			strcpy(strrchr(filename, '/') + 1, lFile);

			FS_LoadFile(filename, &lBuffer);
			*text = (char*)lBuffer;
		}
	}

	/* free the old file */
	if (lBuffer) {
		FS_FreeFile(lBuffer);
		lBuffer = NULL;
	}

	/* finished */
	return NULL;
}

/* global vars for maplisting */
char *fs_maps[MAX_MAPS];
int fs_numInstalledMaps = -1;
static qboolean fs_mapsInstalledInit = qfalse;

/**
 * @param[in] reset If true the directory is scanned everytime for new maps (useful for dedicated servers).
 * If false we only use the maps array (for clients e.g.)
 */
void FS_GetMaps (qboolean reset)
{
	char findname[MAX_OSPATH];
	char filename[MAX_QPATH];
	int status, i;
	const char *baseMapName = NULL;
	char **dirnames;
	int ndirs;
	searchpath_t *search;
	pack_t *pak;

	/* force a reread */
	if (!reset && fs_mapsInstalledInit)
		return;
	else if (fs_mapsInstalledInit) {
		Com_DPrintf(DEBUG_ENGINE, "Free old list with %i entries\n", fs_numInstalledMaps);
		for (i = 0; i < fs_numInstalledMaps; i++)
			Mem_Free(fs_maps[i]);
	}

	fs_numInstalledMaps = -1;

	/* search through the path, one element at a time */
	for (search = fs_searchpaths; search; search = search->next) {
		/* is the element a pak file? */
		if (search->pack) {
			/* look through all the pak file elements */
			pak = search->pack;
			for (i = 0; i < pak->numfiles; i++) {
				/* found it! */
				baseMapName = strchr(pak->files[i].name, '/');
				if (baseMapName) {
					/* FIXME: paths are normalized here? */
					baseMapName = strchr(baseMapName + 1, '/');
					/* ugly hack - only show the maps in base/maps - not in base/maps/b and so on */
					if (baseMapName)
						continue;
				} else
					continue;

				if (strstr(pak->files[i].name, ".bsp")) {
					if (fs_numInstalledMaps + 1 >= MAX_MAPS) {
						Com_Printf("FS_GetMaps: Max maps limit hit\n");
						break;
					}
					fs_maps[fs_numInstalledMaps + 1] = (char *)Mem_PoolAlloc(MAX_QPATH * sizeof(char), com_fileSysPool, 0);
					if (fs_maps[fs_numInstalledMaps + 1] == NULL) {
						Com_Printf("Could not allocate memory in FS_GetMaps\n");
						continue;
					}
					Q_strncpyz(findname, pak->files[i].name, sizeof(findname));
					FS_NormPath(findname);
					baseMapName = COM_SkipPath(findname);
					COM_StripExtension(baseMapName, filename, sizeof(filename));
					Q_strncpyz(fs_maps[fs_numInstalledMaps + 1], filename, MAX_QPATH);
					fs_numInstalledMaps++;
				}
			}
		} else {
			Com_sprintf(findname, sizeof(findname), "%s/maps/*.bsp", search->filename);
			FS_NormPath(findname);

			if ((dirnames = FS_ListFiles(findname, &ndirs, 0, SFF_HIDDEN | SFF_SYSTEM)) != 0) {
				for (i = 0; i < ndirs - 1; i++) {
					Com_DPrintf(DEBUG_ENGINE, "... found map: '%s' (pos %i out of %i)\n", dirnames[i], i + 1, ndirs);
					baseMapName = COM_SkipPath(dirnames[i]);
					COM_StripExtension(baseMapName, filename, sizeof(filename));
					status = CheckBSPFile(filename);
					if (!status) {
						if (fs_numInstalledMaps + 1 >= MAX_MAPS) {
							Com_Printf("FS_GetMaps: Max maps limit hit\n");
							break;
						}
						fs_maps[fs_numInstalledMaps + 1] = (char *) Mem_PoolAlloc(MAX_QPATH * sizeof(char), com_fileSysPool, 0);
						if (fs_maps[fs_numInstalledMaps + 1] == NULL) {
							Com_Printf("Could not allocate memory in FS_GetMaps\n");
							Mem_Free(dirnames[i]);
							continue;
						}
						Q_strncpyz(fs_maps[fs_numInstalledMaps + 1], filename, MAX_QPATH);
						fs_numInstalledMaps++;
					} else
						Com_Printf("invalid mapstatus: %i (%s)\n", status, dirnames[i]);
					Mem_Free(dirnames[i]);
				}
				Mem_Free(dirnames);
			}
		}
	}

	fs_mapsInstalledInit = qtrue;
}

/**
 * @brief Properly handles partial writes
 */
int FS_Write (const void *buffer, int len, qFILE * f)
{
	int block, remaining;
	int written;
	const byte *buf;
	int tries;

	if (!f->f)
		return 0;

	buf = (const byte *) buffer;

	remaining = len;
	tries = 0;
	while (remaining) {
		block = remaining;
		written = fwrite(buf, 1, block, f->f);
		if (written == 0) {
			if (!tries) {
				tries = 1;
			} else {
				Com_Printf("FS_Write: 0 bytes written\n");
				return 0;
			}
		}

		if (written == -1) {
			Com_Printf("FS_Write: -1 bytes written\n");
			return 0;
		}

		remaining -= written;
		buf += written;
	}
/* 	fflush( f->f ); */
	return len;
}


int FS_WriteFile (const void *buffer, size_t len, const char *filename)
{
	qFILE f;
	int c, lencheck;

	FS_CreatePath(filename);

	f.f = fopen(filename, "wb");

	if (f.f)
		c = fwrite(buffer, 1, len, f.f);
	else
		return 0;

	lencheck = FS_FileLength(&f);
	fclose(f.f);

	/* if file write failed (file is incomplete) then delete it */
	if (c != len || lencheck != len) {
		Com_Printf("FS_WriteFile: failed to finish writing '%s'\n", filename);
		Com_Printf("FS_WriteFile: deleting this incomplete file\n");
		if (remove(filename))
			Com_Printf("FS_WriteFile: could not remove file: %s\n", filename);
		return 0;
	}

	return c;
}

/**
 * @brief Return current working dir
 */
const char *FS_GetCwd (void)
{
	static char buf[MAX_OSPATH];
	Q_strncpyz(buf, Sys_Cwd(), sizeof(buf));
	FS_NormPath(buf);
	return buf;
}

/**
 * @brief Checks whether a file exists (not in virtual filesystem)
 * @sa FS_CheckFile
 * @param[in] filename Full filesystem path to the file
 */
qboolean FS_FileExists (const char *filename)
{
#ifdef _WIN32
	return (_access(filename, 00) == 0);
#else
	return (access(filename, R_OK) == 0);
#endif
}

/**
 * @brief Cleanup function
 * @sa FS_InitFilesystem
 * @sa FS_RestartFilesystem
 */
void FS_Shutdown (void)
{
	int i;
	searchpath_t *p, *next;
	const cmdList_t *commands;

	/* free malloc'ed space for maplist */
	if (fs_mapsInstalledInit) {
		for (i = 0; i <= fs_numInstalledMaps; i++)
			Mem_Free(fs_maps[i]);
	}

	/* free everything */
	for (p = fs_searchpaths; p; p = next) {
		next = p->next;

		if (p->pack) {
			unzClose(p->pack->handle.z);
			Mem_Free(p->pack->files);
			Mem_Free(p->pack);
		}
		Mem_Free(p);
	}

	/* any FS_ calls will now be an error until reinitialized */
	fs_searchpaths = NULL;

	for (commands = fs_commands; commands->name; commands++)
		Cmd_RemoveCommand(commands->name);
}

/**
 * @brief Restart the filesystem (reload all pk3 files)
 * @note Call this after you finished a download
 * @sa FS_Shutdown
 * @sa FS_InitFilesystem
 */
void FS_RestartFilesystem (void)
{
	/* free anything we currently have loaded */
	FS_Shutdown();

	/* try to start up normally */
	FS_InitFilesystem();

	/**
	 * if we can't find default.cfg, assume that the paths are
	 * busted and error out now, rather than getting an unreadable
	 * graphics screen when the font fails to load
	 */
	if (FS_CheckFile("default.cfg") < 0) {
		Com_Error(ERR_FATAL, "Couldn't load default.cfg");
	}
}

/**
 * @brief Copy a fully specified file from one place to another
 */
void FS_CopyFile (const char *fromOSPath, const char *toOSPath)
{
	FILE *f;
	int len;
	byte *buf;

	Com_Printf("FS_CopyFile: copy %s to %s\n", fromOSPath, toOSPath);

	/* @todo: Allow copy of pk3 file content */
	f = fopen(fromOSPath, "rb");
	if (!f)
		return;

	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);

	buf = Mem_PoolAlloc(len, com_fileSysPool, 0);
	if (fread(buf, 1, len, f) != len)
		Com_Error(ERR_FATAL, "Short read in FS_CopyFile()");
	fclose(f);

	FS_CreatePath(toOSPath);

	f = fopen(toOSPath, "wb");
	if (!f)
		return;

	if (fwrite(buf, 1, len, f) != len)
		Com_Error(ERR_FATAL, "Short write in FS_CopyFile()");

	fclose(f);
	Mem_Free(buf);
}

/**
 * @sa FS_CopyFile
 */
void FS_Remove (const char *osPath)
{
	Com_Printf("FS_Remove: remove %s", osPath);
	remove(osPath);
}

/**
 * @brief Renames a file
 * @sa FS_Remove
 * @sa FS_CopyFile
 */
qboolean FS_Rename (const char *from, const char *to, qboolean relative)
{
	const char *from_ospath, *to_ospath;
	char from_buf[MAX_OSPATH];
	char to_buf[MAX_OSPATH];

	if (!fs_searchpaths)
		Com_Error(ERR_FATAL, "Filesystem call made without initialization");

	if (relative) {
		from_ospath = FS_BuildOSPath(FS_Gamedir(), from, from_buf, sizeof(from_buf));
		to_ospath = FS_BuildOSPath(FS_Gamedir(), to, to_buf, sizeof(to_buf));
	} else {
		from_ospath = from;
		to_ospath = to;
	}

	if (rename(from_ospath, to_ospath))
		return qfalse;
	return qtrue;
}
