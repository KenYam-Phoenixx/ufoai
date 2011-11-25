#!/usr/bin/env python
# -*- coding: utf-8; tab-width: 4; indent-tabs-mode: nil -*-

import sys, os, re
try:
	from hashlib import md5
except ImportError:
	from md5 import md5
import urllib2
from gzip import GzipFile
import optparse
import mapsync
from tempfile import mkstemp

# path where exists ufo binary
UFOAI_ROOT = os.path.realpath(sys.path[0] + '/../..')

INTERACTIVE_REPLY = 'query'

REPOSITORY = 'http://ufoai.ninex.info/maps'
__version__ = '0.0.4.2'

displayDownloadStatus = True
displayAlreadyUpToDate = True

# TODO: use os.path.join

import time
def download(uri):
    global displayDownloadStatus
    request = urllib2.Request(uri)
    import platform
    p = ' '.join([platform.platform()] + list(platform.dist()))
    request.add_header('User-Agent', 'ufoai_map-get/%s (%s)' % (mapsync.__version__, p))
    f = urllib2.build_opener().open(request)

    re = out = ''
    t = 1
    data = f.read(10240)
    if not displayDownloadStatus:
        print('Downloading ' + uri)
    while data:
        re+= data
        if displayDownloadStatus and sys.stdout.isatty:
            out = '\r%s %9ikb' % (uri, len(re) / 1024)
            sys.stdout.write(out)
            sys.stdout.flush()
        t = time.time()
        data = f.read(10240)
        t = time.time() - t
    f.close()
    if displayDownloadStatus:
        sys.stdout.write('\r%s\r' % (' '*len(out)))
    return re

class Object:
    pass

def ask_boolean_question(question):
    if INTERACTIVE_REPLY == 'yes':
        print question, " YES (auto reply)"
        return True
    if INTERACTIVE_REPLY == 'no':
        print question, " NO (auto reply)"
        return False
    return raw_input(question).lower() in ('y', '')

def upgrade(repository):
    """Download the list of maps."""
    global displayAlreadyUpToDate

    ufo2mapMeta = mapsync.Ufo2mapMetadata()
    ufo2mapMeta.extract()
    print "Local ufo2map information:"
    print '* ' + ufo2mapMeta.get_full_content().strip().replace("\n", "\n* ")
    print

    data = download(repository + '/UFO2MAP')
    ufo2mapRef = mapsync.Ufo2mapMetadata()
    ufo2mapRef.set_content(data)
    print "Remote/reference ufo2map information:"
    print '* ' + ufo2mapRef.get_full_content().strip().replace("\n", "\n* ")
    print

    if ufo2mapMeta.hash_from_source != ufo2mapRef.hash_from_source:
        print 'WARNING: ufo2map source code mismatch'
        if ufo2mapMeta.version != ufo2mapRef.version:
            print 'WARNING: ufo2map version mismatch'
            if not ask_boolean_question('Continue? [Y]es, [n]o: '):
                sys.exit(3)

    maps = {}
    print 'Getting list of available maps'
    for l in download(repository + '/MAPS').split('\n'):
        if l == "":
            continue
        i = l.split(' ')
        if len(i) == 3:
            o = Object()
            if i[0] == 'ufo2map':
                continue
            o.bsphash = i[1]
            o.maphash = i[2]
            maps[i[0]] = o
        else:
            print 'Line "%s" corrupted' % l

	print 'Start updating files to ' + UFOAI_ROOT
    updated = missmatch = uptodate = 0
    mapnames = maps.keys()
    mapnames.sort()
    for i in mapnames:
        map_name = i[:-4] + ".map"
        mappath = UFOAI_ROOT + '/' + map_name
        bsppath = UFOAI_ROOT + '/' + i

        if not os.path.exists(mappath):
            print '* %s not found' % map_name
            continue


        if mapsync.md5sum(mappath, False) != maps[i].maphash:
            print '* %s version mismatch, skip update' % map_name
            missmatch += 1
            continue

        if not os.path.exists(bsppath) or mapsync.md5sum(bsppath, True) != maps[i].bsphash:
            fd, name = mkstemp()
            os.write(fd, download('%s/%s.gz' %(repository, i)))
            os.close(fd)
            data = GzipFile(name, 'rb').read()
            os.unlink(name)
            open(bsppath, 'wb').write(data)
            print '* %s - updated' % i
            updated += 1
            continue

        if displayAlreadyUpToDate:
            print '* %s - already up to date' % i
        uptodate += 1

    print
    print '%d upgraded, %d version mismatched, %d already up to date' % (updated, missmatch, uptodate)

def main(argv=None):
    global displayDownloadStatus
    global displayAlreadyUpToDate
    global INTERACTIVE_REPLY, REPOSITORY

    print "map-get version " + mapsync.__version__

    parser = optparse.OptionParser(argv)
    parser.add_option('-v', '--verbose', action='store_true', dest='verbose')
    parser.add_option('', '--no-downloadstatus', action='store_false', default="True", dest='displayDownloadStatus')
    parser.add_option('', '--hide-uptodate', action='store_false', default="True", dest='displayAlreadyUpToDate')
    parser.add_option("", "--reply", action="store", default="query", type="string", dest="reply",
        help='Allow to auto reply interactive questions, REPLY={yes,no,query}')
    parser.add_option("", "--repository", action="store", default=REPOSITORY, type="string", dest="repository",
        help='Use a custom repository. The default one is "%s"' % REPOSITORY)
    parser.add_option("", "--branch", action="store", default="auto", type="string", dest="branch",
        help='Use a custom a custom branch version for the repository, Default value is "auto"')

    options, args = parser.parse_args()

    if not (options.reply in ['yes', 'no', 'query']):
        print 'Wrong param --reply={yes,no,query}'
        sys.exit(1)
    branch = options.branch
    if branch == "auto":
        branch = mapsync.get_branch()
    repository = options.repository + "/" + branch

    print repository

    displayDownloadStatus = options.displayDownloadStatus
    displayAlreadyUpToDate = options.displayAlreadyUpToDate
    INTERACTIVE_REPLY = options.reply

    upgrade(repository)

if __name__ == '__main__':
    main()
