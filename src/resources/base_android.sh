#!/bin/sh

if [ \! -d base ] ; then
	echo "You should run this command from the UfoAI root dir, like: src/resources/base_android.sh"
	exit 1
fi

rm -rf base_light base_full 0base.pk3 0music.pk3 1base.pk3
src/resources/light_base.py

src/resources/shrink_textures.sh base_light base_light
mkdir -p base_full
cd base
find . -type f -exec sh -c "[ -e ../base_light/{} ] || cp --parents {} ../base_full" \;
cd ..
src/resources/shrink_textures.sh base_full base_full
cat base_light/downsampledimages.txt base_full/downsampledimages.txt > downsampledimages-1.txt
mv -f downsampledimages-1.txt base_full/downsampledimages.txt

cd base_light
cp -f ../base/android.cfg config.cfg
cp -f ufos/android/* ufos
zip -r -n .png:.jpg:.ogg:.ogm ../0base.pk3 *
cd ../base_full
zip -r -n .png:.jpg:.ogg:.ogm ../0music.pk3 music
rm -rf music
zip -r -n .png:.jpg:.ogg:.ogm ../1base.pk3 *
cd ..
zip -r i18n.zip base/i18n
