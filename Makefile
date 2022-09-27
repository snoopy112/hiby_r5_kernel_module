export CROSS_COMPILE:=~/toolchain/bin/aarch64-linux-android-
export ARCH:=arm64
export SUBARCH:=arm64
export CFLAGS_MODULE:="-fno-pic -w"
export KDIR:=~/kernel

export FILE_NAME:=vol_keys

obj-m += $(FILE_NAME).o

all:
	make -j4 CFLAGS_MODULE=$(CFLAGS_MODULE) ARCH=$(ARCH) SUBARCH=$(SUBARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KDIR)/build M=$(PWD) modules
	$(CROSS_COMPILE)strip --strip-unneeded $(FILE_NAME).ko
	$(KDIR)/scripts/sign-file sha512 $(KDIR)/signing_key.priv $(KDIR)/signing_key.x509 $(FILE_NAME).ko

clean:
	make -j4 CFLAGS_MODULE=$(CFLAGS_MODULE) ARCH=$(ARCH) SUBARCH=$(SUBARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KDIR)/build M=$(PWD) clean

