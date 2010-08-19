########################################################################################################
# radiant
########################################################################################################

RADIANT_BASE = tools/radiant

RADIANT_CFLAGS+=-Isrc/$(RADIANT_BASE)/libs -Isrc/$(RADIANT_BASE)/include
RADIANT_LIBS+=-lgthread-2.0 $(OPENAL_LIBS) -lvorbisfile -lvorbis -logg

RADIANT_SRCS_CPP = \
	$(RADIANT_BASE)/radiant/archivezip.cpp \
	$(RADIANT_BASE)/radiant/colorscheme.cpp \
	$(RADIANT_BASE)/radiant/commands.cpp \
	$(RADIANT_BASE)/radiant/console.cpp \
	$(RADIANT_BASE)/radiant/dialog.cpp \
	$(RADIANT_BASE)/radiant/eclass.cpp \
	$(RADIANT_BASE)/radiant/eclass_def.cpp \
	$(RADIANT_BASE)/radiant/entity.cpp \
	$(RADIANT_BASE)/radiant/entitymodule.cpp \
	$(RADIANT_BASE)/radiant/environment.cpp \
	$(RADIANT_BASE)/radiant/exec.cpp \
	$(RADIANT_BASE)/radiant/filetypes.cpp \
	$(RADIANT_BASE)/radiant/filters.cpp \
	$(RADIANT_BASE)/radiant/gtkmisc.cpp \
	$(RADIANT_BASE)/radiant/image.cpp \
	$(RADIANT_BASE)/radiant/imagemodules.cpp \
	$(RADIANT_BASE)/radiant/lastused.cpp \
	$(RADIANT_BASE)/radiant/material.cpp \
	$(RADIANT_BASE)/radiant/ump.cpp \
	$(RADIANT_BASE)/radiant/particles.cpp \
	$(RADIANT_BASE)/radiant/main.cpp \
	$(RADIANT_BASE)/radiant/mainframe.cpp \
	$(RADIANT_BASE)/radiant/parse.cpp \
	$(RADIANT_BASE)/radiant/pathfinding.cpp \
	$(RADIANT_BASE)/radiant/plugin.cpp \
	$(RADIANT_BASE)/radiant/pluginmenu.cpp \
	$(RADIANT_BASE)/radiant/plugintoolbar.cpp \
	$(RADIANT_BASE)/radiant/settings/preferences.cpp \
	$(RADIANT_BASE)/radiant/qe3.cpp \
	$(RADIANT_BASE)/radiant/referencecache.cpp \
	$(RADIANT_BASE)/radiant/scenegraph.cpp \
	$(RADIANT_BASE)/radiant/select.cpp \
	$(RADIANT_BASE)/radiant/selection.cpp \
	$(RADIANT_BASE)/radiant/server.cpp \
	$(RADIANT_BASE)/radiant/stacktrace.cpp \
	$(RADIANT_BASE)/radiant/sound.cpp \
	$(RADIANT_BASE)/radiant/shaders.cpp \
	$(RADIANT_BASE)/radiant/texmanip.cpp \
	$(RADIANT_BASE)/radiant/textures.cpp \
	$(RADIANT_BASE)/radiant/timer.cpp \
	$(RADIANT_BASE)/radiant/toolbars.cpp \
	$(RADIANT_BASE)/radiant/treemodel.cpp \
	$(RADIANT_BASE)/radiant/undo.cpp \
	$(RADIANT_BASE)/radiant/url.cpp \
	$(RADIANT_BASE)/radiant/windowobservers.cpp \
	$(RADIANT_BASE)/radiant/levelfilters.cpp \
	$(RADIANT_BASE)/radiant/vfs.cpp \
	$(RADIANT_BASE)/radiant/model.cpp \
	\
	$(RADIANT_BASE)/radiant/render/qgl.cpp \
	$(RADIANT_BASE)/radiant/render/renderstate.cpp \
	\
	$(RADIANT_BASE)/radiant/referencecache/nullmodel.cpp \
	\
	$(RADIANT_BASE)/radiant/ufoscript/UFOScript.cpp \
	$(RADIANT_BASE)/radiant/ufoscript/common/Parser.cpp \
	$(RADIANT_BASE)/radiant/ufoscript/common/ScriptValues.cpp \
	$(RADIANT_BASE)/radiant/ufoscript/common/DataBlock.cpp \
	$(RADIANT_BASE)/radiant/ufoscript/terrain/Terrain.cpp \
	$(RADIANT_BASE)/radiant/ufoscript/mapdef/MapDef.cpp \
	$(RADIANT_BASE)/radiant/ufoscript/particles/Particle.cpp \
	\
	$(RADIANT_BASE)/radiant/ump/UMPFile.cpp \
	$(RADIANT_BASE)/radiant/ump/UMPTile.cpp \
	$(RADIANT_BASE)/radiant/ump/UMPAssembly.cpp \
	\
	$(RADIANT_BASE)/radiant/map/parse.cpp \
	$(RADIANT_BASE)/radiant/map/write.cpp \
	$(RADIANT_BASE)/radiant/map/autosave.cpp \
	$(RADIANT_BASE)/radiant/map/map.cpp \
	$(RADIANT_BASE)/radiant/map/MapFileChooserPreview.cpp \
	$(RADIANT_BASE)/radiant/map/mapmodule.cpp \
	\
	$(RADIANT_BASE)/radiant/namespace/NameObserver.cpp \
	$(RADIANT_BASE)/radiant/namespace/BasicNamespace.cpp \
	$(RADIANT_BASE)/radiant/namespace/NamespaceAPI.cpp \
	\
	$(RADIANT_BASE)/radiant/xyview/grid.cpp \
	$(RADIANT_BASE)/radiant/xyview/xywindow.cpp \
	\
	$(RADIANT_BASE)/radiant/camera/camwindow.cpp \
	$(RADIANT_BASE)/radiant/camera/view.cpp \
	\
	$(RADIANT_BASE)/radiant/brush/brush.cpp \
	$(RADIANT_BASE)/radiant/brush/brushmanip.cpp \
	$(RADIANT_BASE)/radiant/brush/brushmodule.cpp \
	$(RADIANT_BASE)/radiant/brush/brush_primit.cpp \
	$(RADIANT_BASE)/radiant/brush/winding.cpp \
	$(RADIANT_BASE)/radiant/brush/csg/csg.cpp \
	\
	$(RADIANT_BASE)/radiant/brush/construct/Cone.cpp \
	$(RADIANT_BASE)/radiant/brush/construct/Cuboid.cpp \
	$(RADIANT_BASE)/radiant/brush/construct/Prism.cpp \
	$(RADIANT_BASE)/radiant/brush/construct/Rock.cpp \
	$(RADIANT_BASE)/radiant/brush/construct/Sphere.cpp \
	$(RADIANT_BASE)/radiant/brush/construct/Terrain.cpp \
	\
	$(RADIANT_BASE)/radiant/brushexport/BrushExportOBJ.cpp \
	\
	$(RADIANT_BASE)/radiant/selection/BestPoint.cpp \
	$(RADIANT_BASE)/radiant/selection/Intersection.cpp \
	$(RADIANT_BASE)/radiant/selection/Manipulatables.cpp \
	$(RADIANT_BASE)/radiant/selection/Manipulators.cpp \
	\
	$(RADIANT_BASE)/radiant/plugin/PluginManager.cpp \
	$(RADIANT_BASE)/radiant/plugin/PluginSlots.cpp \
	\
	$(RADIANT_BASE)/radiant/sidebar/sidebar.cpp \
	$(RADIANT_BASE)/radiant/sidebar/entitylist.cpp \
	$(RADIANT_BASE)/radiant/sidebar/entityinspector.cpp \
	$(RADIANT_BASE)/radiant/sidebar/surfaceinspector.cpp \
	$(RADIANT_BASE)/radiant/sidebar/PrefabSelector.cpp \
	$(RADIANT_BASE)/radiant/sidebar/MapInfo.cpp \
	$(RADIANT_BASE)/radiant/sidebar/JobInfo.cpp \
	$(RADIANT_BASE)/radiant/sidebar/texturebrowser.cpp \
	$(RADIANT_BASE)/radiant/sidebar/ParticleBrowser.cpp \
	\
	$(RADIANT_BASE)/radiant/dialogs/about.cpp \
	$(RADIANT_BASE)/radiant/dialogs/findbrush.cpp \
	$(RADIANT_BASE)/radiant/dialogs/maptools.cpp \
	$(RADIANT_BASE)/radiant/dialogs/particle.cpp \
	$(RADIANT_BASE)/radiant/dialogs/findtextures.cpp \
	\
	$(RADIANT_BASE)/radiant/ui/common/MapPreview.cpp \
	$(RADIANT_BASE)/radiant/ui/common/ModelPreview.cpp \
	$(RADIANT_BASE)/radiant/ui/common/MaterialDefinitionView.cpp \
	$(RADIANT_BASE)/radiant/ui/common/UMPDefinitionView.cpp \
	$(RADIANT_BASE)/radiant/ui/common/UFOScriptDefinitionView.cpp \
	$(RADIANT_BASE)/radiant/ui/common/RenderableAABB.cpp \
	$(RADIANT_BASE)/radiant/ui/common/SoundChooser.cpp \
	$(RADIANT_BASE)/radiant/ui/common/SoundPreview.cpp \
	$(RADIANT_BASE)/radiant/ui/common/TexturePreviewCombo.cpp \
	\
	$(RADIANT_BASE)/radiant/ui/modelselector/ModelSelector.cpp \
	\
	$(RADIANT_BASE)/radiant/ui/materialeditor/MaterialEditor.cpp \
	\
	$(RADIANT_BASE)/radiant/ui/scripteditor/UFOScriptEditor.cpp \
	\
	$(RADIANT_BASE)/radiant/ui/umpeditor/UMPEditor.cpp \
	\
	$(RADIANT_BASE)/radiant/ui/particleeditor/ParticleEditor.cpp \
	$(RADIANT_BASE)/radiant/ui/particleeditor/ParticlePreview.cpp \
	\
	$(RADIANT_BASE)/radiant/ui/ortho/EntityClassChooser.cpp \
	$(RADIANT_BASE)/radiant/ui/ortho/OrthoContextMenu.cpp \
	\
	$(RADIANT_BASE)/radiant/ui/lightdialog/LightDialog.cpp \
	\
	$(RADIANT_BASE)/radiant/pathfinding/Routing.cpp \
	$(RADIANT_BASE)/radiant/pathfinding/RoutingLumpLoader.cpp \
	$(RADIANT_BASE)/radiant/pathfinding/RoutingLump.cpp \
	$(RADIANT_BASE)/radiant/pathfinding/RoutingRenderable.cpp \
	\
	$(RADIANT_BASE)/libs/gtkutil/accelerator.cpp \
	$(RADIANT_BASE)/libs/gtkutil/button.cpp \
	$(RADIANT_BASE)/libs/gtkutil/clipboard.cpp \
	$(RADIANT_BASE)/libs/gtkutil/cursor.cpp \
	$(RADIANT_BASE)/libs/gtkutil/dialog.cpp \
	$(RADIANT_BASE)/libs/gtkutil/filechooser.cpp \
	$(RADIANT_BASE)/libs/gtkutil/frame.cpp \
	$(RADIANT_BASE)/libs/gtkutil/glfont.cpp \
	$(RADIANT_BASE)/libs/gtkutil/glwidget.cpp \
	$(RADIANT_BASE)/libs/gtkutil/image.cpp \
	$(RADIANT_BASE)/libs/gtkutil/menu.cpp \
	$(RADIANT_BASE)/libs/gtkutil/messagebox.cpp \
	$(RADIANT_BASE)/libs/gtkutil/paned.cpp \
	$(RADIANT_BASE)/libs/gtkutil/timer.cpp \
	$(RADIANT_BASE)/libs/gtkutil/toolbar.cpp \
	$(RADIANT_BASE)/libs/gtkutil/window.cpp \
	$(RADIANT_BASE)/libs/gtkutil/ModelProgressDialog.cpp \
	$(RADIANT_BASE)/libs/gtkutil/SourceView.cpp \
	$(RADIANT_BASE)/libs/gtkutil/MenuItemAccelerator.cpp \
	$(RADIANT_BASE)/libs/gtkutil/TreeModel.cpp \
	$(RADIANT_BASE)/libs/gtkutil/TextPanel.cpp \
	$(RADIANT_BASE)/libs/gtkutil/VFSTreePopulator.cpp \
	$(RADIANT_BASE)/libs/gtkutil/menu/PopupMenu.cpp \
	\
	$(RADIANT_BASE)/libs/profile/profile.cpp \
	$(RADIANT_BASE)/libs/profile/file.cpp \
	$(RADIANT_BASE)/libs/sound/SoundManager.cpp \
	$(RADIANT_BASE)/libs/sound/SoundPlayer.cpp \
	$(RADIANT_BASE)/libs/shaders/shaders.cpp \
	$(RADIANT_BASE)/libs/archivezip/ZipArchive.cpp \
	$(RADIANT_BASE)/libs/archivedir/archive.cpp \
	$(RADIANT_BASE)/libs/entity/entity.cpp \
	$(RADIANT_BASE)/libs/entity/eclassmodel.cpp \
	$(RADIANT_BASE)/libs/entity/generic.cpp \
	$(RADIANT_BASE)/libs/entity/group.cpp \
	$(RADIANT_BASE)/libs/entity/miscmodel.cpp \
	$(RADIANT_BASE)/libs/entity/miscparticle.cpp \
	$(RADIANT_BASE)/libs/entity/miscsound.cpp \
	$(RADIANT_BASE)/libs/entity/filters.cpp \
	$(RADIANT_BASE)/libs/entity/light.cpp \
	$(RADIANT_BASE)/libs/entity/targetable.cpp \
	$(RADIANT_BASE)/libs/picomodel/model.cpp \
	$(RADIANT_BASE)/libs/picomodel/RenderablePicoSurface.cpp \
	$(RADIANT_BASE)/libs/picomodel/RenderablePicoModel.cpp

RADIANT_SRCS_C = \
	shared/parse.c \
	shared/entitiesdef.c \
	$(RADIANT_BASE)/libs/picomodel/picointernal.c \
	$(RADIANT_BASE)/libs/picomodel/picomodel.c \
	$(RADIANT_BASE)/libs/picomodel/picomodules.c \
	$(RADIANT_BASE)/libs/picomodel/pm_ase.c \
	$(RADIANT_BASE)/libs/picomodel/pm_md3.c \
	$(RADIANT_BASE)/libs/picomodel/pm_obj.c \
	$(RADIANT_BASE)/libs/picomodel/pm_md2.c

RADIANT_CPP_OBJS=$(RADIANT_SRCS_CPP:%.cpp=$(BUILDDIR)/tools/radiant/%.o)
RADIANT_C_OBJS=$(RADIANT_SRCS_C:%.c=$(BUILDDIR)/tools/radiant_c/%.o)
RADIANT_TARGET=radiant/uforadiant$(EXE_EXT)

# PLUGINS

RADIANT_PLUGIN_BRUSHEXPORT_SRCS_CPP = \
	$(RADIANT_BASE)/plugins/brushexport/callbacks.cpp \
	$(RADIANT_BASE)/plugins/brushexport/export.cpp \
	$(RADIANT_BASE)/plugins/brushexport/interface.cpp \
	$(RADIANT_BASE)/plugins/brushexport/plugin.cpp \
	$(RADIANT_BASE)/plugins/brushexport/support.cpp

RADIANT_PLUGIN_BRUSHEXPORT_CPP_OBJS=$(RADIANT_PLUGIN_BRUSHEXPORT_SRCS_CPP:%.cpp=$(BUILDDIR)/tools/radiant/plugins_cpp/%.o)
RADIANT_PLUGIN_BRUSHEXPORT_TARGET=radiant/plugins/brushexport.$(SHARED_EXT)

ifeq ($(BUILD_UFORADIANT),1)

ALL_RADIANT_OBJS+=$(RADIANT_CPP_OBJS) $(RADIANT_C_OBJS) \
	$(RADIANT_PLUGIN_BRUSHEXPORT_CPP_OBJS)
RADIANT_DEPS = $(patsubst %.o, %.d, $(ALL_RADIANT_OBJS))

uforadiant: $(BUILDDIR)/.dirs $(RADIANT_PLUGIN_BRUSHEXPORT_TARGET) \
	$(RADIANT_TARGET)
else

uforadiant:
	@echo "Radiant is not enabled - use './configure --enable-uforadiant'"

clean-uforadiant:
	@echo "Radiant is not enabled - use './configure --enable-uforadiant'"

ALL_RADIANT_OBJS=""

endif

# Say how to build .o files from .cpp files for this module
$(BUILDDIR)/tools/radiant_c/%.o: $(SRCDIR)/%.c
	@echo " * [RAD] $<"; \
		$(CC) $(CFLAGS) $(RADIANT_CFLAGS) -o $@ -c $< $(CFLAGS_M_OPTS)
$(BUILDDIR)/tools/radiant/%.o: $(SRCDIR)/%.cpp
	@echo " * [RAD] $<"; \
		$(CPP) $(CPPFLAGS) $(RADIANT_CFLAGS) -o $@ -c $< $(CFLAGS_M_OPTS)

# Say how to build .o files from .cpp/.c files for this module
$(BUILDDIR)/tools/radiant/plugins_c/%.o: $(SRCDIR)/%.c
	@echo " * [RAD] $<"; \
		$(CC) $(CFLAGS) $(SHARED_CFLAGS) $(RADIANT_CFLAGS) -o $@ -c $< $(CFLAGS_M_OPTS)
$(BUILDDIR)/tools/radiant/plugins_cpp/%.o: $(SRCDIR)/%.cpp
	@echo " * [RAD] $<"; \
		$(CPP) $(CPPFLAGS) $(SHARED_CFLAGS) $(RADIANT_CFLAGS) -o $@ -c $< $(CFLAGS_M_OPTS)

# and now link the plugins
$(RADIANT_PLUGIN_BRUSHEXPORT_TARGET) : $(RADIANT_PLUGIN_BRUSHEXPORT_CPP_OBJS)
	@echo " * [BRS] ... linking $(LNKFLAGS) ($(RADIANT_LIBS))"; \
		$(CPP) $(LDFLAGS) $(SHARED_LDFLAGS) -o $@ $(RADIANT_PLUGIN_BRUSHEXPORT_CPP_OBJS) $(RADIANT_LIBS) $(LNKFLAGS)

# Say how to link the exe
$(RADIANT_TARGET): $(RADIANT_CPP_OBJS) $(RADIANT_C_OBJS)
	@echo " * [RAD] ... linking $(LNKFLAGS) ($(RADIANT_LIBS))"; \
		$(CPP) $(LDFLAGS) -o $@ $(RADIANT_CPP_OBJS) $(RADIANT_C_OBJS) $(RADIANT_LIBS) $(LNKFLAGS) -lz;
		@echo " * [RAD] ... done"

