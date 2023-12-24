# Architecture
ARCH=arm64

# Branch
BRANCH=udc

# Compiler
LLVM=1
DEPMOD=depmod
CLANG_VERSION=r487747c
CLANG_PREBUILT_BIN=prebuilts-master/clang/host/linux-x86/clang-${CLANG_VERSION}/bin
BUILDTOOLS_PREBUILT_BIN=build/kernel/build-tools/path/linux-x86

# Defconfig
DEFCONFIG=ruby_defconfig
KERNEL_DIR=kernel/xiaomi/mt6877

# Build artifacts which are copied to prebuilts directory after build.
FILES="
arch/arm64/boot/Image.gz
arch/arm64/boot/dtbo.img
arch/arm64/boot/dts/mediatek/mt6877.dtb
"

# Extra
EXTRA_CMDS=''
STOP_SHIP_TRACEPRINTK=1
IN_KERNEL_MODULES=1
DO_NOT_STRIP_MODULES=1
SKIP_MRPROPER=1
