#!/bin/sh
FILE=$1
if [ -f "$FILE" ]; then
    echo "ftp-publish: publishing $FILE..."
else
    echo "ftp-publish: first argument must be valid file"
    exit 1
fi
HOST='ftp.oidadb.com'
USER='htmluploads@oidadb.com'
PASSWD='VzvZFyInss~x'


ftp -inv $HOST <<END_SCRIPT | grep -q '^226'
quote USER $USER
quote PASS $PASSWD
cd /public_html
binary
put $FILE 
quit
END_SCRIPT

OUTFTP=$?

if [ $OUTFTP -eq 0 ]; then
    echo "ftp-publish: published oidadb.com/$FILE"
else
    echo "ftp-publish: failed to publish oidadb.com/$FILE: 226 ftp status not found (bad file? connection?)"
fi

exit 0
