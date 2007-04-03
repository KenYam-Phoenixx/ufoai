MASTERSERVER_SRCS = \
	tools/masterserver/master.c

MASTERSERVER_OBJS= \
	$(patsubst %.c, $(BUILDDIR)/masterserver/%.o, $(filter %.c, $(MASTERSERVER_SRCS))) \
	$(patsubst %.m, $(BUILDDIR)/masterserver/%.o, $(filter %.m, $(MASTERSERVER_SRCS))) \
	$(patsubst %.rc, $(BUILDDIR)/masterserver/%.o, $(filter %.rc, $(MASTERSERVER_SRCS)))

MASTERSERVER_DEPS=$(MASTERSERVER_OBJS:%.o=%.d)
MASTERSERVER_TARGET=ufomaster$(EXE_EXT)

ifeq ($(BUILD_MASTER),1)
	ALL_OBJS+=$(MASTERSERVER_OBJS)
	ALL_DEPS+=$(MASTERSERVER_DEPS)
	TARGETS+=$(MASTERSERVER_TARGET)
endif

MASTER_CFLAGS=

# Say how to like the exe
$(MASTERSERVER_TARGET): $(MASTERSERVER_OBJS) $(BUILDDIR)/.dirs
	@echo " * [MST] ... linking $(LNKFLAGS) ($(MASTER_LIBS))"; \
		$(CC) $(LDFLAGS) -o $@ $(MASTERSERVER_OBJS) $(MASTER_LIBS) $(LNKFLAGS)

# Say how to build .o files from .c files for this module
$(BUILDDIR)/masterserver/%.o: $(SRCDIR)/%.c $(BUILDDIR)/.dirs
	@echo " * [MST] $<"; \
		$(CC) $(CFLAGS) $(MASTER_CFLAGS) -o $@ -c $<

ifeq ($(TARGET_OS),mingw32)
# Say how to build .o files from .rc files for this module
$(BUILDDIR)/masterserver/%.o: $(SRCDIR)/%.rc $(BUILDDIR)/.dirs
	@echo " * [RC ] $<"; \
		$(WINDRES) -i $< -o $@
endif

# Say how to build .o files from .m files for this module
$(BUILDDIR)/masterserver/%.m: $(SRCDIR)/%.c $(BUILDDIR)/.dirs
	@echo " * [MST] $<"; \
		$(CC) $(CFLAGS) $(MASTER_CFLAGS) -o $@ -c $<

# Say how to build the dependencies
ifdef BUILDDIR
$(BUILDDIR)/masterserver/%.d: $(SRCDIR)/%.c $(BUILDDIR)/.dirs
	@echo " * [DEP] $<"; \
		$(DEP) $(MASTER_CFLAGS)

ifeq ($(TARGET_OS),mingw32)
$(BUILDDIR)/masterserver/%.d: $(SRCDIR)/%.rc $(BUILDDIR)/.dirs
	@echo " * [DEP] $<"; touch $@
endif

$(BUILDDIR)/masterserver/%.d: $(SRCDIR)/%.m $(BUILDDIR)/.dirs
	@echo " * [DEP] $<"; \
		$(DEP) $(MASTER_CFLAGS)
endif

