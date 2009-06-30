#FIXME: check for -ldl (mingw doesn't have this)

SERVER_SRCS += \
	common/cmd.c \
	common/http.c \
	common/ioapi.c \
	common/unzip.c \
	common/cmodel.c \
	common/common.c \
	common/cvar.c \
	common/files.c \
	common/md4.c \
	common/md5.c \
	common/mem.c \
	common/msg.c \
	common/dbuffer.c \
	common/net.c \
	common/netpack.c \
	common/scripts.c \
	\
	server/sv_ccmds.c \
	server/sv_game.c \
	server/sv_init.c \
	server/sv_main.c \
	server/sv_send.c \
	server/sv_user.c \
	server/sv_world.c \
	server/sv_clientstub.c \
	\
	game/q_shared.c \
	game/inv_shared.c \
	shared/byte.c \
	shared/infostring.c \
	shared/shared.c

ifneq ($(findstring $(TARGET_OS), netbsd freebsd linux-gnu),)
	SERVER_SRCS += \
		ports/linux/linux_main.c \
		ports/unix/unix_console.c \
		ports/unix/unix_main.c \
		ports/unix/unix_glob.c
endif

ifeq ($(TARGET_OS),mingw32)
	SERVER_SRCS+=\
		ports/windows/win_shared.c \
		ports/windows/win_main.c \
		ports/windows/ufo.rc
endif

ifeq ($(TARGET_OS),darwin)
	SERVER_SRCS+=\
		ports/macosx/osx_main.m \
		ports/unix/unix_console.c \
		ports/unix/unix_main.c \
		ports/unix/unix_glob.c
endif

ifeq ($(TARGET_OS),solaris)
	SERVER_SRCS += \
		ports/solaris/solaris_main.c \
		ports/unix/unix_console.c \
		ports/unix/unix_main.c \
		ports/unix/unix_glob.c
endif

SERVER_OBJS= \
	$(patsubst %.c, $(BUILDDIR)/server/%.o, $(filter %.c, $(SERVER_SRCS))) \
	$(patsubst %.m, $(BUILDDIR)/server/%.o, $(filter %.m, $(SERVER_SRCS))) \
	$(patsubst %.rc, $(BUILDDIR)/server/%.o, $(filter %.rc, $(SERVER_SRCS)))

SERVER_TARGET=ufoded$(EXE_EXT)

ifeq ($(BUILD_DEDICATED),1)
	ALL_OBJS+=$(SERVER_OBJS)
	TARGETS+=$(SERVER_TARGET)
endif

DEDICATED_CFLAGS=-DDEDICATED_ONLY

ifeq ($(HAVE_CURSES),1)
	DEDICATED_CFLAGS+=-DHAVE_CURSES
endif

# Say how to link the exe
$(SERVER_TARGET): $(SERVER_OBJS) $(BUILDDIR)/.dirs
	@echo " * [DED] ... linking $(LNKFLAGS) ($(SERVER_LIBS))"; \
		$(CC) $(LDFLAGS) -o $@ $(SERVER_OBJS) $(SERVER_LIBS)

# Say how to build .o files from .c files for this module
$(BUILDDIR)/server/%.o: $(SRCDIR)/%.c $(BUILDDIR)/.dirs
	@echo " * [DED] $<"; \
		$(CC) $(CFLAGS) $(CPPFLAGS) $(DEDICATED_CFLAGS) -o $@ -c $<

ifeq ($(TARGET_OS),mingw32)
# Say how to build .o files from .rc files for this module
$(BUILDDIR)/server/%.o: $(SRCDIR)/%.rc $(BUILDDIR)/.dirs
	@echo " * [RC ] $<"; \
		$(WINDRES) -DCROSSBUILD -i $< -o $@
endif

# Say how to build .o files from .m files for this module
$(BUILDDIR)/server/%.o: $(SRCDIR)/%.m $(BUILDDIR)/.dirs
	@echo " * [DED] $<"; \
		$(CC) $(CFLAGS) $(CPPFLAGS) $(DEDICATED_CFLAGS) -o $@ -c $<
