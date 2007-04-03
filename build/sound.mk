###################################################################################################
# SDL
###################################################################################################

SND_SDL_SRCS=ports/linux/snd_sdl.c
SND_SDL_OBJS=$(SND_SDL_SRCS:%.c=$(BUILDDIR)/snd-sdl/%.o)
SND_SDL_DEPS=$(SND_SDL_OBJS:%.o=%.d)
SND_SDL_TARGET=snd_sdl.$(SHARED_EXT)

ifeq ($(BUILD_CLIENT), 1)
ifeq ($(HAVE_SND_SDL),1)
	TARGETS += $(SND_SDL_TARGET)
	ALL_DEPS += $(SND_SDL_DEPS)
	ALL_OBJS += $(SND_SDL_OBJS)
endif
endif

# Say about to build the target
$(SND_SDL_TARGET) : $(SND_SDL_OBJS) $(BUILDDIR)/.dirs
	@echo " * [SDL] ... linking $(LNKFLAGS) ($(SND_SDL_LIBS))"; \
		$(CC) $(LDFLAGS) $(SHARED_LDFLAGS) -o $@ $(SND_SDL_OBJS) $(SND_LIBS_LIBS) $(LNKFLAGS)

# Say how to build .o files from .c files for this module
$(BUILDDIR)/snd-sdl/%.o: $(SRCDIR)/%.c $(BUILDDIR)/.dirs
	@echo " * [SDL] $<"; \
		$(CC) $(CFLAGS) $(SHARED_CFLAGS) $(SDL_CFLAGS) -o $@ -c $<

# Say how to build the dependencies
$(BUILDDIR)/snd-sdl/%.d: $(SRCDIR)/%.c $(BUILDDIR)/.dirs
	@echo " * [DEP] $<"; $(DEP)

###################################################################################################
# ALSA
###################################################################################################

SND_ALSA_SRCS=ports/linux/snd_alsa.c
SND_ALSA_OBJS=$(SND_ALSA_SRCS:%.c=$(BUILDDIR)/snd-alsa/%.o)
SND_ALSA_DEPS=$(SND_ALSA_OBJS:%.o=%.d)
SND_ALSA_TARGET=snd_alsa.$(SHARED_EXT)

ifeq ($(BUILD_CLIENT), 1)
ifeq ($(HAVE_SND_ALSA),1)
	TARGETS += $(SND_ALSA_TARGET)
	ALL_DEPS += $(SND_ALSA_DEPS)
	ALL_OBJS += $(SND_ALSA_OBJS)
endif
endif

# Say about to build the target
$(SND_ALSA_TARGET) : $(SND_ALSA_OBJS) $(BUILDDIR)/.dirs
	@echo " * [ALSA] ... linking $(LNKFLAGS) ($(SND_ALSA_LIBS))"; \
		$(CC) $(LDFLAGS) $(SHARED_LDFLAGS) -o $@ $(SND_ALSA_OBJS) $(SND_ALSA_LIBS) $(LNKFLAGS)

# Say how to build .o files from .c files for this module
$(BUILDDIR)/snd-alsa/%.o: $(SRCDIR)/%.c $(BUILDDIR)/.dirs
	@echo " * [ALSA] $<"; \
		$(CC) $(CFLAGS) $(SHARED_CFLAGS) -o $@ -c $<

# Say how to build the dependencies
$(BUILDDIR)/snd-alsa/%.d: $(SRCDIR)/%.c $(BUILDDIR)/.dirs
	@echo " * [DEP] $<"; $(DEP)

###################################################################################################
# JACK
###################################################################################################

SND_JACK_SRCS=ports/linux/snd_jack.c
SND_JACK_OBJS=$(SND_JACK_SRCS:%.c=$(BUILDDIR)/snd-jack/%.o)
SND_JACK_DEPS=$(SND_JACK_OBJS:%.o=%.d)
SND_JACK_TARGET=snd_jack.$(SHARED_EXT)

ifeq ($(BUILD_CLIENT), 1)
ifeq ($(HAVE_SND_JACK),1)
	TARGETS += $(SND_JACK_TARGET)
	ALL_DEPS += $(SND_JACK_DEPS)
	ALL_OBJS += $(SND_JACK_OBJS)
endif
endif

# Say about to build the target
$(SND_JACK_TARGET) : $(SND_JACK_OBJS) $(BUILDDIR)/.dirs
	@echo " * [JACK] ... linking $(LNKFLAGS) ($(SND_JACK_LIBS))"; \
		$(CC) $(LDFLAGS) $(SHARED_LDFLAGS) -o $@ $(SND_JACK_OBJS) $(SND_JACK_LIBS) $(LNKFLAGS)

# Say how to build .o files from .c files for this module
$(BUILDDIR)/snd-jack/%.o: $(SRCDIR)/%.c $(BUILDDIR)/.dirs
	@echo " * [JACK] $<"; \
		$(CC) $(CFLAGS) $(SHARED_CFLAGS) -o $@ -c $<

# Say how to build the dependencies
$(BUILDDIR)/snd-jack/%.d: $(SRCDIR)/%.c $(BUILDDIR)/.dirs
	@echo " * [DEP] $<"; $(DEP)

###################################################################################################
# OSS
###################################################################################################

SND_OSS_SRCS=ports/linux/snd_oss.c
SND_OSS_OBJS=$(SND_OSS_SRCS:%.c=$(BUILDDIR)/snd-oss/%.o)
SND_OSS_DEPS=$(SND_OSS_OBJS:%.o=%.d)
SND_OSS_TARGET=snd_oss.$(SHARED_EXT)

ifeq ($(BUILD_CLIENT), 1)
ifeq ($(HAVE_SND_OSS),1)
	TARGETS += $(SND_OSS_TARGET)
	ALL_DEPS += $(SND_OSS_DEPS)
	ALL_OBJS += $(SND_OSS_OBJS)
endif
endif

# Say about to build the target
$(SND_OSS_TARGET) : $(SND_OSS_OBJS) $(BUILDDIR)/.dirs
	@echo " * [OSS] ... linking $(LNKFLAGS) ($(SND_OSS_LIBS))"; \
		$(CC) $(LDFLAGS) $(SHARED_LDFLAGS) -o $@ $(SND_OSS_OBJS) $(SND_OSS_LIBS) $(LNKFLAGS)

# Say how to build .o files from .c files for this module
$(BUILDDIR)/snd-oss/%.o: $(SRCDIR)/%.c $(BUILDDIR)/.dirs
	@echo " * [OSS] $<"; \
		$(CC) $(CFLAGS) $(SHARED_CFLAGS) -o $@ -c $<

# Say how to build the dependencies
$(BUILDDIR)/snd-oss/%.d: $(SRCDIR)/%.c $(BUILDDIR)/.dirs
	@echo " * [DEP] $<"; $(DEP) $(SHARED_CFLAGS)

###################################################################################################
# ARTS
###################################################################################################

SND_ARTS_SRCS=ports/linux/snd_arts.c
SND_ARTS_OBJS=$(SND_ARTS_SRCS:%.c=$(BUILDDIR)/snd-arts/%.o)
SND_ARTS_DEPS=$(SND_ARTS_OBJS:%.o=%.d)
SND_ARTS_TARGET=snd_arts.$(SHARED_EXT)

ifeq ($(BUILD_CLIENT), 1)
ifeq ($(HAVE_SND_ARTS),1)
	TARGETS += $(SND_ARTS_TARGET)
	ALL_DEPS += $(SND_ARTS_DEPS)
	ALL_OBJS += $(SND_ARTS_OBJS)
endif
endif

# Say about to build the target
$(SND_ARTS_TARGET) : $(SND_ARTS_OBJS) $(BUILDDIR)/.dirs
	@echo " * [ARTS] ... linking $(LNKFLAGS) ($(SND_ARTS_LIBS))"; \
		$(CC) $(LDFLAGS) $(SHARED_LDFLAGS) -o $@ $(SND_ARTS_OBJS) $(LNKFLAGS) $(SND_ARTS_LIBS)

# Say how to build .o files from .c files for this module
$(BUILDDIR)/snd-arts/%.o: $(SRCDIR)/%.c $(BUILDDIR)/.dirs
	@echo " * [ARTS] $<"; \
		$(CC) $(CFLAGS) $(SHARED_CFLAGS) $(SND_ARTS_CFLAGS) -o $@ -c $<

# Say how to build the dependencies
$(BUILDDIR)/snd-arts/%.d: $(SRCDIR)/%.c $(BUILDDIR)/.dirs
	@echo " * [DEP] $<"; $(DEP) $(SHARED_CFLAGS) $(SND_ARTS_CFLAGS)


###################################################################################################
# WAPI
###################################################################################################

SND_WAPI_SRCS=ports/win32/snd_wapi.c
SND_WAPI_OBJS=$(SND_WAPI_SRCS:%.c=$(BUILDDIR)/snd-wapi/%.o)
SND_WAPI_DEPS=$(SND_WAPI_OBJS:%.o=%.d)
SND_WAPI_TARGET=snd_wapi.$(SHARED_EXT)

ifeq ($(BUILD_CLIENT), 1)
ifeq ($(HAVE_SND_WAPI),1)
	TARGETS += $(SND_WAPI_TARGET)
	ALL_DEPS += $(SND_WAPI_DEPS)
	ALL_OBJS += $(SND_WAPI_OBJS)
endif
endif

# Say about to build the target
$(SND_WAPI_TARGET) : $(SND_WAPI_OBJS) $(BUILDDIR)/.dirs
	@echo " * [WAPI] ... linking $(LNKFLAGS) ($(SND_WAPI_LIBS))"; \
		$(CC) $(LDFLAGS) $(SHARED_LDFLAGS) -o $@ $(SND_WAPI_OBJS) $(SND_WAPI_LIBS) $(LNKFLAGS)

# Say how to build .o files from .c files for this module
$(BUILDDIR)/snd-wapi/%.o: $(SRCDIR)/%.c $(BUILDDIR)/.dirs
	@echo " * [WAPI] $<"; \
		$(CC) $(CFLAGS) $(SHARED_CFLAGS) -o $@ -c $<

# Say how to build the dependencies
$(BUILDDIR)/snd-wapi/%.d: $(SRCDIR)/%.c $(BUILDDIR)/.dirs
	@echo " * [DEP] $<"; $(DEP) $(SHARED_CFLAGS)

###################################################################################################
# DirectX
###################################################################################################

SND_DX_SRCS=ports/win32/snd_dx.c
SND_DX_OBJS=$(SND_DX_SRCS:%.c=$(BUILDDIR)/snd-dx/%.o)
SND_DX_DEPS=$(SND_DX_OBJS:%.o=%.d)
SND_DX_TARGET=snd_dx.$(SHARED_EXT)

ifeq ($(BUILD_CLIENT), 1)
ifeq ($(HAVE_SND_DX),1)
	TARGETS += $(SND_DX_TARGET)
	ALL_DEPS += $(SND_DX_DEPS)
	ALL_OBJS += $(SND_DX_OBJS)
endif
endif

# Say about to build the target
$(SND_DX_TARGET) : $(SND_DX_OBJS) $(BUILDDIR)/.dirs
	@echo " * [DX] ... linking $(LNKFLAGS) ($(SND_DX_LIBS))"; \
		$(CC) $(LDFLAGS) $(SHARED_LDFLAGS) -o $@ $(SND_DX_OBJS) $(SND_DX_LIBS) $(LNKFLAGS)

# Say how to build .o files from .c files for this module
$(BUILDDIR)/snd-dx/%.o: $(SRCDIR)/%.c $(BUILDDIR)/.dirs
	@echo " * [DX] $<"; \
		$(CC) $(CFLAGS) $(SHARED_CFLAGS) -o $@ -c $<

# Say how to build the dependencies
$(BUILDDIR)/snd-dx/%.d: $(SRCDIR)/%.c $(BUILDDIR)/.dirs
	@echo " * [DEP] $<"; $(DEP) $(SHARED_CFLAGS)
