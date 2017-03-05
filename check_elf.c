#include <stdio.h>      // standard IO
#include <stdlib.h>     // standard library
#include <gelf.h>       // library ELF
#include <sys/stat.h>   // file status
#include <fcntl.h>      // file control
#include <errno.h>      // error number
#include <string.h>     // string
#include <unistd.h>     // standard symbolic constants and types
#include <stdint.h>
#include <limits.h>

int main(int argc, char *argv[])
{
    struct stat elf_stat;
    Elf *elf;
    GElf_Phdr phdr; // PHDR = Program Header
    uint32_t addr_min, addr_max;
    char *fname;
    int err, fd, i;
    void *elf_buf;
    size_t n;

    // initial values
    addr_max = 0;
    addr_min = UINT_MAX;

    if (argc < 2) {
        printf("Please specify an ELF file\n");
        return -1;
    }

    fname = argv[1];

    if ((err = elf_version(EV_CURRENT)) == EV_NONE) {
        printf("ELF library initialization failed: %s", elf_errmsg(err));
        return -2;
    }

    if ((fd = open(fname, O_RDONLY, 0)) < 0) {
        printf("Open %s failed\n", fname);
        return -3;
    }

    if (fstat(fd, &elf_stat) < 0) {
        printf("fstat() failed, error: %s\n", strerror(errno));
        goto FAILED_FSTAT;
    }

    // st_size = size of file, in bytes
    elf_buf = malloc(elf_stat.st_size);

    if (elf_buf == NULL) {
        printf("malloc() failed\n");
        goto FAILED_FSTAT;
    }

    if (read(fd, elf_buf, elf_stat.st_size) != elf_stat.st_size) {
        printf("read() failed\n");
        goto FAILED_READ;
    }

    // elf_begin() returns pointer to struct Elf
    // ELF_C_READ <- Elf_Cmd
    if ((elf = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
        printf("elf_begin() failed\n");
        goto FAILED_READ;
    }

    // ELF_K_FILE <- Elf_Kind
    if (elf_kind(elf) != ELF_K_ELF) {
        printf("%s is not a valid ELF file\n", fname);
        goto FAILED_UC_MAP;
    }

    if (elf_getphdrnum(elf, &n) != 0) {
        printf("elf_getphdrnum() failed\n");
        goto FAILED_UC_MAP;
    }

    for (i = 0; i < n; i++) {
        if (gelf_getphdr(elf, i, &phdr) != &phdr) {
            printf("gelf_getphdr() failed\n");
            break;
        }

        // p_paddr = Segment Physical Address
        if (phdr.p_paddr < addr_min)
            addr_min = phdr.p_paddr;

        // p_memsz = Segment size in memory
        if (phdr.p_paddr+phdr.p_memsz > addr_max)
            addr_max = phdr.p_paddr+phdr.p_memsz;
    }

    if (i < n)
        goto FAILED_UC_MAP;

    printf("%s is a valid ELF file\n", fname);
    printf("Mapping ELF from [0x%08x - 0x%08x] (len = %u)\n",
            addr_min, addr_max, addr_max - addr_min);

FAILED_UC_MAP:
    elf_end(elf);
FAILED_READ:
    free(elf_buf);
FAILED_FSTAT:
    close(fd);

    return 0;
}
