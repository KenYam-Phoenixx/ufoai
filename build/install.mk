VERSION=$(shell grep UFO_VERSION src/common/common.h | sed -e 's/.*UFO_VERSION \"\(.*\)\"/\1/')

installer: wininstaller linuxinstaller sourcearchive mappack

mappack:
	$(MAKE) maps
	tar -cvjp --exclude-from=src/ports/linux/tar.ex -f ufoai-$(VERSION)-mappack.tar.bz2 ./base/maps
	scp ufoai-$(VERSION)-mappack.tar.bz2 ufo:~/public_html/download

wininstaller:
	$(MAKE) lang
	$(MAKE) maps
	cd base; ./archives.sh
	makensis src/ports/windows/installer.nsi
	md5sum src/ports/windows/ufoai-$(VERSION)-win32.exe > src/ports/windows/ufoai-$(VERSION)-win32.md5
#	scp src/ports/windows/ufoai-$(VERSION)-win32.exe src/ports/windows/ufoai-$(VERSION)-win32.md5 ufo:~/public_html/download
	scp src/ports/windows/ufoai-$(VERSION)-win32.exe src/ports/windows/ufoai-$(VERSION)-win32.md5 mirror:~/public_html

linuxarchive:
	tar -cvjp --exclude-from=src/ports/linux/tar.ex -f ufoai-$(VERSION)-linux.tar.bz2 ./

linuxinstaller:
	$(MAKE) lang
	$(MAKE) maps
	cd base; ./archives.sh
	cd src/ports/linux/installer; $(MAKE) packdata; $(MAKE)
#	scp src/ports/linux/installer/ufoai-$(VERSION)-linux.run ufo:~/public_html/download
	scp src/ports/linux/installer/ufoai-$(VERSION)-linux.run mirror:~/public_html

macinstaller:
	$(MAKE) lang
	# delete all the maps first - we have to get them precompiled, otherwise multiplayer
	# won't work due to mismatching checksums
	cd base; ./archives.sh
	cd base; wget http://mattn.ninex.info/download/0maps.pk3
	cd src/ports/macosx/installer; $(MAKE) TARGET_CPU=$(TARGET_CPU)
	scp src/ports/macosx/installer/ufoai-$(VERSION)-macosx-$(TARGET_CPU).dmg ufo:~/public_html/download
	scp src/ports/macosx/installer/ufoai-$(VERSION)-macosx-$(TARGET_CPU).dmg mirror:~/public_html

#
# Generate a tar archive of the sources.
#
sourcearchive:
# Create the tarsrc/ufoai-$(VERSION)-source directory in order that the
# resulting tar archive extracts to one directory.
	mkdir -p ./tarsrc
	ln -fsn ../ tarsrc/ufoai-$(VERSION)-source
# Take a list of files from SVN. Trim SVN's output to include only the filenames
# and paths. Then feed that list to tar.
	svn status -v|grep -v "^?"|cut -c 7-|awk '{print $$4}'|sed "s/^/ufoai-$(VERSION)-source\//"> ./tarsrc/filelist
# Also tell tar to exclude base/ and contrib/ directories.
	tar -cvjh --no-recursion	\
		-C ./tarsrc				\
		--exclude "*base/*"		\
		--exclude "*contrib*"	\
		-T ./tarsrc/filelist	\
		-f ./tarsrc/ufoai-$(VERSION)-source.tar.bz2
	mv ./tarsrc/ufoai-$(VERSION)-source.tar.bz2 ./
	rm -rf ./tarsrc

# this done by base/archives.sh
#packfiles:
#	cd base; zip -r 0textures.zip textures -x \*.svn\*
#	cd base; zip -r 0sounds.zip sound -x \*.svn\*
#	cd base; zip -r 0pics.zip pics -x \*.svn\*
#	cd base; zip -r 0music.zip music -x \*.svn\*
#	cd base; zip -r 0maps.zip maps -x \*.svn\*
#	cd base; zip -r 0models.zip models -x \*.svn\*
#	cd base; zip -r 0ufos.zip ufos -x \*.svn\*
