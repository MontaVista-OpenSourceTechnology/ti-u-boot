.. SPDX-License-Identifier: GPL-2.0+

Booting Linux on x86 with FIT
=============================

Background
----------

Generally Linux x86 uses its own very complex booting method. There is a setup
binary which contains all sorts of parameters and a compressed self-extracting
binary for the kernel itself, often with a small built-in serial driver to
display decompression progress.

The x86 CPU has various processor modes. I am no expert on these, but my
understanding is that an x86 CPU (even a really new one) starts up in a 16-bit
'real' mode where only 1MB of memory is visible, moves to 32-bit 'protected'
mode where 4GB is visible (or more with special memory access techniques) and
then to 64-bit 'long' mode if 64-bit execution is required.

Partly the self-extracting nature of Linux was introduced to cope with boot
loaders that were barely capable of loading anything. Even changing to 32-bit
mode was something of a challenge, so putting this logic in the kernel seemed
to make sense.

Bit by bit more and more logic has been added to this post-boot pre-Linux
wrapper:

- Changing to 32-bit mode
- Decompression
- Serial output (with drivers for various chips)
- Load address randomisation
- Elf loader complete with relocation (for the above)
- Random number generator via 3 methods (again for the above)
- Some sort of EFI mini-loader (1000+ glorious lines of code)
- Locating and tacking on a device tree and ramdisk

To my mind, if you sit back and look at things from first principles, this
doesn't make a huge amount of sense. Any boot loader worth its salts already
has most of the above features and more besides. The boot loader already knows
the layout of memory, has a serial driver, can decompress things, includes an
ELF loader and supports device tree and ramdisks. The decision to duplicate
all these features in a Linux wrapper caters for the lowest common
denominator: a boot loader which consists of a BIOS call to load something off
disk, followed by a jmp instruction.

(Aside: On ARM systems, we worry that the boot loader won't know where to load
the kernel. It might be easier to just provide that information in the image,
or in the boot loader rather than adding a self-relocator to put it in the
right place. Or just use ELF?

As a result, the x86 kernel boot process is needlessly complex. The file
format is also complex, and obfuscates the contents to a degree that it is
quite a challenge to extract anything from it. This bzImage format has become
so prevalent that is actually isn't possible to produce the 'raw' kernel build
outputs with the standard Makefile (as it is on ARM for example, at least at
the time of writing).

This document describes an alternative boot process which uses simple raw
images which are loaded into the right place by the boot loader and then
executed.


Build the kernel
----------------

Note: these instructions assume a 32-bit kernel. U-Boot also supports directly
booting a 64-bit kernel by jumping into 64-bit mode first (see below).

You can build the kernel as normal with 'make'. This will create a file called
'vmlinux'. This is a standard ELF file and you can look at it if you like::

    $ objdump -h vmlinux

    vmlinux:     file format elf32-i386

    Sections:
    Idx Name          Size      VMA       LMA       File off  Algn
      0 .text         00416850  81000000  01000000  00001000  2**5
                      CONTENTS, ALLOC, LOAD, RELOC, READONLY, CODE
      1 .notes        00000024  81416850  01416850  00417850  2**2
                      CONTENTS, ALLOC, LOAD, READONLY, CODE
      2 __ex_table    00000c50  81416880  01416880  00417880  2**3
                      CONTENTS, ALLOC, LOAD, RELOC, READONLY, DATA
      3 .rodata       00154b9e  81418000  01418000  00419000  2**5
                      CONTENTS, ALLOC, LOAD, RELOC, READONLY, DATA
      4 __bug_table   0000597c  8156cba0  0156cba0  0056dba0  2**0
                      CONTENTS, ALLOC, LOAD, RELOC, READONLY, DATA
      5 .pci_fixup    00001b80  8157251c  0157251c  0057351c  2**2
                      CONTENTS, ALLOC, LOAD, RELOC, READONLY, DATA
      6 .tracedata    00000024  8157409c  0157409c  0057509c  2**0
                      CONTENTS, ALLOC, LOAD, RELOC, READONLY, DATA
      7 __ksymtab     00007ec0  815740c0  015740c0  005750c0  2**2
                      CONTENTS, ALLOC, LOAD, RELOC, READONLY, DATA
      8 __ksymtab_gpl 00004a28  8157bf80  0157bf80  0057cf80  2**2
                      CONTENTS, ALLOC, LOAD, RELOC, READONLY, DATA
      9 __ksymtab_strings 0001d6fc  815809a8  015809a8  005819a8  2**0
                      CONTENTS, ALLOC, LOAD, READONLY, DATA
     10 __init_rodata 00001c3c  8159e0a4  0159e0a4  0059f0a4  2**2
                      CONTENTS, ALLOC, LOAD, RELOC, READONLY, DATA
     11 __param       00000ff0  8159fce0  0159fce0  005a0ce0  2**2
                      CONTENTS, ALLOC, LOAD, RELOC, READONLY, DATA
     12 __modver      00000330  815a0cd0  015a0cd0  005a1cd0  2**2
                      CONTENTS, ALLOC, LOAD, RELOC, READONLY, DATA
     13 .data         00063000  815a1000  015a1000  005a2000  2**12
                      CONTENTS, ALLOC, LOAD, RELOC, DATA
     14 .init.text    0002f104  81604000  01604000  00605000  2**2
                      CONTENTS, ALLOC, LOAD, RELOC, READONLY, CODE
     15 .init.data    00040cdc  81634000  01634000  00635000  2**12
                      CONTENTS, ALLOC, LOAD, RELOC, DATA
     16 .x86_cpu_dev.init 0000001c  81674cdc  01674cdc  00675cdc  2**2
                      CONTENTS, ALLOC, LOAD, RELOC, READONLY, DATA
     17 .altinstructions 0000267c  81674cf8  01674cf8  00675cf8  2**0
                      CONTENTS, ALLOC, LOAD, RELOC, READONLY, DATA
     18 .altinstr_replacement 00000942  81677374  01677374  00678374  2**0
                      CONTENTS, ALLOC, LOAD, READONLY, CODE
     19 .iommu_table  00000014  81677cb8  01677cb8  00678cb8  2**2
                      CONTENTS, ALLOC, LOAD, RELOC, READONLY, DATA
     20 .apicdrivers  00000004  81677cd0  01677cd0  00678cd0  2**2
                      CONTENTS, ALLOC, LOAD, RELOC, DATA
     21 .exit.text    00001a80  81677cd8  01677cd8  00678cd8  2**0
                      CONTENTS, ALLOC, LOAD, RELOC, READONLY, CODE
     22 .data..percpu 00007880  8167a000  0167a000  0067b000  2**12
                      CONTENTS, ALLOC, LOAD, RELOC, DATA
     23 .smp_locks    00003000  81682000  01682000  00683000  2**2
                      CONTENTS, ALLOC, LOAD, RELOC, READONLY, DATA
     24 .bss          000a1000  81685000  01685000  00686000  2**12
                      ALLOC
     25 .brk          00424000  81726000  01726000  00686000  2**0
                      ALLOC
     26 .comment      00000049  00000000  00000000  00686000  2**0
                      CONTENTS, READONLY
     27 .GCC.command.line 0003e055  00000000  00000000  00686049  2**0
                      CONTENTS, READONLY
     28 .debug_aranges 0000f4c8  00000000  00000000  006c40a0  2**3
                      CONTENTS, RELOC, READONLY, DEBUGGING
     29 .debug_info   0440b0df  00000000  00000000  006d3568  2**0
                      CONTENTS, RELOC, READONLY, DEBUGGING
     30 .debug_abbrev 0022a83b  00000000  00000000  04ade647  2**0
                      CONTENTS, READONLY, DEBUGGING
     31 .debug_line   004ead0d  00000000  00000000  04d08e82  2**0
                      CONTENTS, RELOC, READONLY, DEBUGGING
     32 .debug_frame  0010a960  00000000  00000000  051f3b90  2**2
                      CONTENTS, RELOC, READONLY, DEBUGGING
     33 .debug_str    001b442d  00000000  00000000  052fe4f0  2**0
                      CONTENTS, READONLY, DEBUGGING
     34 .debug_loc    007c7fa9  00000000  00000000  054b291d  2**0
                      CONTENTS, RELOC, READONLY, DEBUGGING
     35 .debug_ranges 00098828  00000000  00000000  05c7a8c8  2**3
                      CONTENTS, RELOC, READONLY, DEBUGGING

There is also the setup binary mentioned earlier. This is at
arch/x86/boot/setup.bin and is about 12KB in size. It includes the command
line and various settings need by the kernel. Arguably the boot loader should
provide all of this also, but setting it up is some complex that the kernel
helps by providing a head start.

As you can see the code loads to address 0x01000000 and everything else
follows after that. We could load this image using the 'bootelf' command but
we would still need to provide the setup binary. This is not supported by
U-Boot although I suppose you could mostly script it. This would permit the
use of a relocatable kernel.

All we need to boot is the vmlinux file and the setup.bin file.


Create a FIT
------------

To create a FIT you will need a source file describing what should go in the
FIT. See kernel.its for an example for x86 and also instructions on setting
the 'arch' value for booting 64-bit kernels if desired. Put this into a file
called image.its.

Note that setup is loaded to the special address of 0x90000 (a special address
you just have to know) and the kernel is loaded to 0x01000000 (the address you
saw above). This means that you will need to load your FIT to a different
address so that U-Boot doesn't overwrite it when decompressing. Something like
0x02000000 will do so you can set CONFIG_SYS_LOAD_ADDR to that.

In that example the kernel is compressed with lzo. Also we need to provide a
flat binary, not an ELF. So the steps needed to set things are are::

    # Create a flat binary
    objcopy -O binary vmlinux vmlinux.bin

    # Compress it into LZO format
    lzop vmlinux.bin

    # Build a FIT image
    mkimage -f image.its image.fit

(be careful to run the mkimage from your U-Boot tools directory since it
will have x86_setup support.)

You can take a look at the resulting fit file if you like::

    $ dumpimage -l image.fit
    FIT description: Simple image with single Linux kernel on x86
    Created:         Tue Oct  7 10:57:24 2014
     Image 0 (kernel)
      Description:  Vanilla Linux kernel
      Created:      Tue Oct  7 10:57:24 2014
      Type:         Kernel Image
      Compression:  lzo compressed
      Data Size:    4591767 Bytes = 4484.15 kB = 4.38 MB
      Architecture: Intel x86
      OS:           Linux
      Load Address: 0x01000000
      Entry Point:  0x00000000
      Hash algo:    sha256
      Hash value:   4bbf49981ade163ed089f8525236fedfe44508e9b02a21a48294a96a1518107b
     Image 1 (setup)
      Description:  Linux setup.bin
      Created:      Tue Oct  7 10:57:24 2014
      Type:         x86 setup.bin
      Compression:  uncompressed
      Data Size:    12912 Bytes = 12.61 kB = 0.01 MB
      Hash algo:    sha256
      Hash value:   6aa50c2e0392cb119cdf0971dce8339f100608ed3757c8200b0e39e889e432d2
     Default Configuration: 'config-1'
     Configuration 0 (config-1)
      Description:  Boot Linux kernel
      Kernel:       kernel


Booting the FIT
---------------

To make it boot you need to load it and then use 'bootm' to boot it. A
suitable script to do this from a network server is::

    bootp
    tftp image.fit
    bootm

This will load the image from the network and boot it. The command line (from
the 'bootargs' environment variable) will be passed to the kernel.

If you want a ramdisk you can add it as normal with FIT. If you want a device
tree then x86 doesn't normally use those - it has ACPI instead.


Why Bother?
-----------

#. It demystifies the process of booting an x86 kernel
#. It allows use of the standard U-Boot boot file format
#. It allows U-Boot to perform decompression - problems will provide an error
   message and you are still in the boot loader. It is possible to investigate.
#. It avoids all the pre-loader code in the kernel which is quite complex to
   follow
#. You can use verified/secure boot and other features which haven't yet been
   added to the pre-Linux
#. It makes x86 more like other architectures in the way it boots a kernel.
   You can potentially use the same file format for the kernel, and the same
   procedure for building and packaging it.


References
----------

In the Linux kernel, `Documentation/arch/x86/boot.rst
<https://docs.kernel.org/arch/x86/boot.html>`_ defines the boot protocol for
the kernel including the setup.bin format. This is handled in U-Boot in
arch/x86/lib/zimage.c and arch/x86/lib/bootm.c.

The FIT file format is described in the `Flattened Image Tree Specification
<https://fitspec.osfw.foundation/>`_.

.. sectionauthor:: Simon Glass <sjg@chromium.org> 7-Oct-2014
