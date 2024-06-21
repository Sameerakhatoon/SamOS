// Host test of firmware/fdt_dev.c, the board devices the
// riscv64 port reads from a device tree, on:
//   vf2.dtb       a VisionFive 2 v1.3B tree (tests64/arch/dtb-jh7110.py
//                 board): the JH7110 node layout of Linux's jh7110.dtsi
//   vf2-host.dtb  the same with the Cadence USB in host mode and the S7
//                 core left enabled
//   fixtures/qemu-virt-riscv64.dtb   QEMU's virt, for contrast
// and the riscv64 UART driver's register accesses with reg-shift 2 and
// reg-io-width 4 (the JH7110's DesignWare UART) on a fake register
// block.
//
//   board_test <dir with vf2.dtb and vf2-host.dtb> <fixtures dir>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "firmware/fdt_dev.h"
#include "arch/riscv64/uart16550.h"

static int checks, failures;
#define CHECK(c, ...) do { checks++; if(!(c)){ failures++; printf("FAIL line %d: ", __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while(0)

static uint8_t* blobs[8];
static int nblobs;

static void load(struct fdt* f, const char* dir, const char* name){
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE* fp = fopen(path, "rb");
    if(!fp){
        printf("FAIL: cannot open %s\n", path);
        exit(1);
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t* blob = blobs[nblobs++] = malloc(size);
    if(fread(blob, 1, size, fp) != (size_t)size || fdt_init(f, blob, 0) != 0){
        printf("FAIL: %s is not a device tree\n", path);
        exit(1);
    }
    fclose(fp);
}

static int cpu(const struct fdt* f, int n){
    char path[32];
    snprintf(path, sizeof path, "/cpus/cpu@%x", n);
    return fdt_path_offset(f, path);
}

static const char* str(const char* s){ return s ? s : "(none)"; }

static void test_vf2(const char* dir){
    struct fdt f;
    load(&f, dir, "vf2.dtb");

    struct fdt_uart u;
    CHECK(fdt_find_uart(&f, &u) == 0 && u.base == 0x10000000 && u.reg_shift == 2 && u.io_width == 4,
          "vf2: console = serial0 alias, DesignWare UART 0x10000000, shift 2, width 4 (0x%llx %u %u)",
          (unsigned long long)u.base, u.reg_shift, u.io_width);
    CHECK(!strcmp(fdt_node_name(&f, u.node), "serial@10000000"), "vf2: the UART node");
    CHECK(fdt_interrupt(&f, u.node, 0) == 32, "vf2: UART0 on PLIC source 32");

    CHECK(!strcmp(str(fdt_hart_unfit(&f, cpu(&f, 0))), "disabled in the device tree"),
          "vf2: hart 0 (S7) left out: %s", str(fdt_hart_unfit(&f, cpu(&f, 0))));
    for(int h = 1; h <= 4; h++){
        CHECK(fdt_hart_unfit(&f, cpu(&f, h)) == NULL, "vf2: hart %d (U74) usable: %s", h,
              str(fdt_hart_unfit(&f, cpu(&f, h))));
    }
    CHECK(fdt_hart_has_ext(&f, cpu(&f, 1), "a") && fdt_hart_has_ext(&f, cpu(&f, 1), "zba")
          && fdt_hart_has_ext(&f, cpu(&f, 1), "zicsr") && !fdt_hart_has_ext(&f, cpu(&f, 1), "zicbom")
          && !fdt_hart_has_ext(&f, cpu(&f, 1), "z"),
          "vf2: U74 extensions: a, zba (riscv,isa), zicsr (riscv,isa-extensions), no zicbom");

    struct fdt_region cc;
    uint32_t line = 0;
    CHECK(fdt_find_ccache(&f, &cc, &line) == 0 && cc.base == 0x2010000 && line == 64,
          "vf2: composable cache at 0x2010000, 64-byte lines");
    CHECK(!fdt_dma_noncoherent(&f), "vf2 tree (no dma-noncoherent): coherent");

    struct fdt_simplefb fb;
    CHECK(fdt_find_simplefb(&f, &fb) == 0 && fb.base == 0xfe000000 && fb.width == 1920 && fb.height == 1080
          && fb.stride == 7680 && fb.bpp == 32, "vf2: simple-framebuffer 1920x1080 a8r8g8b8 at 0xfe000000");

    struct fdt_xhci x[2];
    const char* why = NULL;
    CHECK(fdt_find_xhci(&f, x, 2, &why) == 0 && why && !strcmp(why, "cdns,usb3 dr_mode peripheral"),
          "vf2: the Cadence USB is a peripheral on the board: no host (%s)", str(why));

    struct fdt_mmc m[3];
    CHECK(fdt_find_dw_mmc(&f, m, 3) == 2, "vf2: two DesignWare MMC controllers");
    CHECK(m[0].base == 0x16010000 && m[0].irq == 74 && m[0].bus_width == 8 && m[0].non_removable
          && m[0].fifo_depth == 32, "vf2: mmc0, the eMMC: 0x16010000, irq 74, 8-bit, non-removable");
    CHECK(m[1].base == 0x16020000 && m[1].irq == 75 && m[1].bus_width == 4 && !m[1].non_removable
          && m[1].no_mmc && !m[1].no_sd && m[1].max_hz == 100000000 && m[1].ciu_hz == 0,
          "vf2: mmc1, the SD slot: 0x16020000, irq 75, 4-bit, SD only, no ciu rate in the tree");

    load(&f, dir, "vf2-host.dtb");
    CHECK(!strcmp(str(fdt_hart_unfit(&f, cpu(&f, 0))), "no mmu-type"),
          "vf2, S7 enabled: still left out: %s", str(fdt_hart_unfit(&f, cpu(&f, 0))));
    why = NULL;
    CHECK(fdt_find_xhci(&f, x, 2, &why) == 1 && x[0].base == 0x10110000 && x[0].size == 0x10000 && x[0].irq == 100
          && !why, "vf2, dr_mode host: xHCI at 0x10110000 (reg \"xhci\" through the glue's ranges), irq 100 (\"host\")");
}

static void test_virt(const char* fixtures){
    struct fdt f;
    load(&f, fixtures, "qemu-virt-riscv64.dtb");
    struct fdt_uart u;
    CHECK(fdt_find_uart(&f, &u) == 0 && u.base == 0x10000000 && u.reg_shift == 0 && u.io_width == 1,
          "virt: ns16550a at 0x10000000, byte registers");
    CHECK(fdt_hart_unfit(&f, cpu(&f, 0)) == NULL, "virt: hart 0 usable");
    struct fdt_region cc;
    uint32_t line;
    struct fdt_simplefb fb;
    struct fdt_xhci x[1];
    struct fdt_mmc m[1];
    CHECK(!fdt_dma_noncoherent(&f) && fdt_find_ccache(&f, &cc, &line) < 0 && fdt_find_simplefb(&f, &fb) < 0
          && fdt_find_xhci(&f, x, 1, NULL) == 0 && fdt_find_dw_mmc(&f, m, 1) == 0,
          "virt: coherent, none of the JH7110 devices");
}

// uart16550.c with reg-shift 2, reg-io-width 4: every access a 32-bit
// one at register * 4.
static uint32_t regs[16];

static void test_uart(void){
    regs[5] = 0x20;                             // LSR: THRE
    uart16550_init((uintptr_t)regs, 2, 4);
    CHECK(regs[1] == 0 && regs[2] == 0x07, "uart: IER 0 and FCR 7 written as words at 4 and 8");
    uart16550_putc('S');
    CHECK(regs[0] == 'S', "uart: THR written as a word at 0");
    regs[5] = 0x21;                             // LSR: DR too
    regs[0] = 0xFFFFFF41;                       // the upper bits are not the byte
    CHECK(uart16550_getc() == 'A', "uart: RBR read as a word, low byte");
    uart16550_enable_rx_irq();
    CHECK(regs[1] == 1 && regs[4] == 0x0B, "uart: ERBFI and MCR as words");
}

int main(int argc, char** argv){
    if(argc < 3){
        printf("usage: board_test <dir with vf2.dtb> <fixtures>\n");
        return 2;
    }
    test_vf2(argv[1]);
    test_virt(argv[2]);
    test_uart();
    while(nblobs){
        free(blobs[--nblobs]);
    }
    printf("board host test: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
