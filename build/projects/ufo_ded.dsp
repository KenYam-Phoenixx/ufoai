# Microsoft Developer Studio Project File - Name="ufo_ded" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=ufo_ded - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "ufo_ded.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "ufo_ded.mak" CFG="ufo_ded - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "ufo_ded - Win32 Release" (based on "Win32 (x86) Console Application")
!MESSAGE "ufo_ded - Win32 Debug" (based on "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "ufo_ded - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /c
# ADD BASE RSC /l 0x407 /d "NDEBUG"
# ADD RSC /l 0x407 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386

!ELSEIF  "$(CFG)" == "ufo_ded - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS DEDICATED_ONLY" /YX /FD /GZ /c
# ADD BASE RSC /l 0x407 /d "_DEBUG"
# ADD RSC /l 0x407 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 winmm.lib wsock32.lib gdi32.lib kernel32.lib user32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept

!ENDIF 

# Begin Target

# Name "ufo_ded - Win32 Release"
# Name "ufo_ded - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\..\src\ports\null\cd_null.c
# End Source File
# Begin Source File

SOURCE=..\..\src\ports\null\cl_null.c
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\cmd.c
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\cmodel.c
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\common.c
# End Source File
# Begin Source File

SOURCE=..\..\src\ports\win32\conproc.c
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\cvar.c
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\files.c
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\ioapi.c
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\md4.c
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\md5.c
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\net_chan.c
# End Source File
# Begin Source File

SOURCE=..\..\src\ports\win32\net_wins.c
# End Source File
# Begin Source File

SOURCE=..\..\src\game\q_shared.c
# End Source File
# Begin Source File

SOURCE=..\..\src\ports\win32\q_shwin.c
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\scripts.c
# End Source File
# Begin Source File

SOURCE=..\..\src\server\sv_ccmds.c
# End Source File
# Begin Source File

SOURCE=..\..\src\server\sv_game.c
# End Source File
# Begin Source File

SOURCE=..\..\src\server\sv_init.c
# End Source File
# Begin Source File

SOURCE=..\..\src\server\sv_main.c
# End Source File
# Begin Source File

SOURCE=..\..\src\server\sv_send.c
# End Source File
# Begin Source File

SOURCE=..\..\src\server\sv_user.c
# End Source File
# Begin Source File

SOURCE=..\..\src\server\sv_world.c
# End Source File
# Begin Source File

SOURCE=..\..\src\ports\win32\sys_win.c
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\unzip.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\..\src\qcommon\cmd.h
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\cmodel.h
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\common.h
# End Source File
# Begin Source File

SOURCE=..\..\src\ports\win32\conproc.h
# End Source File
# Begin Source File

SOURCE=..\..\src\client\console.h
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\cvar.h
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\filesys.h
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\ioapi.h
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\md4.h
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\mem.h
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\net_chan.h
# End Source File
# Begin Source File

SOURCE=..\..\src\game\q_shared.h
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\scripts.h
# End Source File
# Begin Source File

SOURCE=..\..\src\server\server.h
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\ufotypes.h
# End Source File
# Begin Source File

SOURCE=..\..\src\qcommon\unzip.h
# End Source File
# Begin Source File

SOURCE=..\..\src\ports\win32\winquake.h
# End Source File
# Begin Source File

SOURCE=..\..\src\ports\win32\zconf.h
# End Source File
# Begin Source File

SOURCE=..\..\src\ports\win32\zlib.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# Begin Source File

SOURCE=..\..\src\ports\win32\ufo.rc
# End Source File
# Begin Source File

SOURCE=..\..\src\ports\win32\zlib1.lib
# End Source File
# End Group
# End Target
# End Project
