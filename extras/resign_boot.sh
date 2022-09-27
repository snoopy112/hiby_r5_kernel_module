#!/bin/bash

FILE="boot.img-zImage"
SIGN_KEY="sign_keys/signing_key.x509"

# Find offset of the device tree binary (DTB)
OFFSETS=$(binwalk -o1 -l10000000 -R "\xd0\x0d\xfe\xed" $FILE | sed -r 's/([0-9]+*).*/\1/')
declare -i DTB_OFFSET=$(echo $OFFSETS | sed -r 's/([0-9]+*).*/\1/')

# Split image onto kernel image and DTB
head -c +$DTB_OFFSET ./$FILE > boot.gz
tail -c +$((DTB_OFFSET+1)) ./$FILE > dtb

# Unzip boot
gzip -d ./boot.gz

# Find offset and length of signing certificate
CERT_STR=$(binwalk -y 'certificate in der format' boot)
declare -i CERT_OFFSET=$(echo $CERT_STR | sed -r 's/.*( [0-9]+ ).*/\1/')
declare -i HEAD_LEN=$(echo $CERT_STR | sed -r 's/(.*header length: ([0-9]*)){1}.*/\2/')
declare -i SEQ_LEN=$(echo $CERT_STR | sed -r 's/(.*sequence length: ([0-9]*)){1}.*/\2/')
declare -i COUNT=$(($HEAD_LEN+$SEQ_LEN))

# Extract original certificate
#dd if=boot bs=1 skip=$CERT_OFFSET count=$COUNT of=cert_orig.dsa

# Replace certificate with the new one
dd conv=notrunc if=$SIGN_KEY of=boot bs=1 seek=$CERT_OFFSET count=$COUNT

# Check inserted certificate
#dd if=boot bs=1 skip=$CERT_OFFSET count=$COUNT of=cert_check.dsa
#sdiff cert_check.dsa $SIGN_KEY

# Zip boot back
gzip -9 ./boot

# Repack the patched image and remove unnecessary files
mv ./$FILE ./$(echo $FILE'_orig')
cat ./boot.gz ./dtb > ./$FILE
rm -f ./boot.gz
rm -f ./dtb

echo "Done! Bootloader image resigned."

