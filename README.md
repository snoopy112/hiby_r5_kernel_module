# Build kernel module for HiBy R5 music player

I use [Ubuntu 64-bit 16.04.7](https://releases.ubuntu.com/16.04/ubuntu-16.04.7-desktop-amd64.iso) on virtual machine using VMware Fusion

### Clone this project:
to i.e. `~/hiby_r5_kernel_module`
```sh
git clone https://github.com/snoopy112/hiby_r5_kernel_module.git
```

### Download kernel distributive from this [source](https://github.com/android-linux-stable/msm-3.18.git):
to i.e. `~/kernel`
```sh
git clone https://github.com/android-linux-stable/msm-3.18.git
```

### Download toolchain binaries from this [source](https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9):
to i.e. `~/toolchain`
```sh
git clone https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9
```

### Create `build` directory in ~/kernel folder and copy `.config` and `Module.symvers` files to:
```sh
mkdir ~/kernel/build
cp ~/hiby_r5_kernel_module/extras/.config ~/kernel/build/.config
cp ~/hiby_r5_kernel_module/extras/Module.symvers ~/kernel/build/Module.symvers
```
You can get `.config` file from /proc/config.gz on device.
`Module.symvers` file can be created with python script `extract-symvers.py`:
```sh
python3 extract-symvers.py boot
```

### Run build kernel:
```sh
make -j5 CFLAGS_MODULE="-fno-pic" silentoldconfig prepare headers_install scripts modules ARCH=arm64 CROSS_COMPILE=~/toolchain/bin/aarch64-linux-android- O=build KERNELRELEASE=3.18.71-perf
```

### Copy genearted sign keys or generate yours:
```sh
cp ~/hiby_r5_kernel_module/extras/sign_keys/signing_key.priv ~/kernel/signing_key.priv
cp ~/hiby_r5_kernel_module/extras/sign_keys/signing_key.x509 ~/kernel/signing_key.x509
```
To generate certificate keys copy `x509.genkey` setting file to ~/kernel folder and run openssl command:
```sh
cp ~/hiby_r5_kernel_module/extras/sign_keys/x509.genkey ~/kernel/x509.genkey
cd ~/kernel
openssl req -new -nodes -utf8 -sha512 -days 36500 -batch -x509 -config x509.genkey -outform DER -out signing_key.x509 -keyout signing_key.priv
```
After that `signing_key.x509` and `signing_key.priv` files should be created.

Show information of the certificate key:
```sh
openssl x509 -in signing_key.x509 -inform der -text
```

### Stock and new builded modules should be signed with the new key:
```sh
cd ~/kernel
scripts/sign-file sha512 signing_key.priv signing_key.x509 /{module_path}/{module_name}.ko
```
I've added this line to the Makefile. Stock modules should be stripped (last 604 bytes) from old sign and resigned with the new key.

### Build new kernel module:
i.e. vol_keys.c module
```sh
cd ~/hiby_r5_kernel_module
make
```

### You should replace stock certificate in bootloader:
Copy `boot.img-zImage` from original bootloader dump (boot.img) to `extras` folder, run `resign_boot.sh` script and flash the device with the patched boot image.
```sh
cd ~/hiby_r5_kernel_module/extras
chmod +x ./resign_boot.sh
./resign_boot.sh
```

### Run new kernel module on device in su mode:
```sh
insmod ./vol_keys.ko
```
This module adds volume control by using wired 3-buttons headset.

### Profit!