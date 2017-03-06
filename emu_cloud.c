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
#include <unicorn/unicorn.h>

struct symbols {
    uint32_t unicorn_uart_init;
    uint32_t unicorn_uart_tx;
    uint32_t unicorn_uart_rx;
    int fd_pipe_in;
    int fd_pipe_out;
};

uint8_t get_elf_stat(const char *file, Elf **elf, int *fd, void **elf_buf)
{
    struct stat elf_stat;
    int err;

    if ((err = elf_version(EV_CURRENT)) == EV_NONE) {
        /* printf("ELF library initialization failed: %s", elf_errmsg(err)); */
        return 1;
    }

    if (((*fd) = open(file, O_RDONLY, 0)) < 0) {
        /* printf("Open %s failed\n", file); */
        return 2;
    }

    if (fstat(*fd, &elf_stat) < 0) {
        /* printf("fstat() failed, error: %s\n", strerror(errno)); */
        return 3;
    }

    // st_size = size of file, in bytes
    (*elf_buf) = malloc(elf_stat.st_size);

    if ((*elf_buf) == NULL) {
        /* printf("malloc() failed\n"); */
        return 4;
    }

    if (read(*fd, *elf_buf, elf_stat.st_size) != elf_stat.st_size) {
        /* printf("read() failed\n"); */
        return 5;
    }

    // elf_begin() returns pointer to struct Elf
    // ELF_C_READ <- Elf_Cmd
    if (((*elf) = elf_begin(*fd, ELF_C_READ, NULL)) == NULL) {
        /* printf("elf_begin() failed\n"); */
        return 6;
    }

    // ELF_K_FILE <- Elf_Kind
    if (elf_kind(*elf) != ELF_K_ELF) {
        /* printf("%s is not a valid ELF file\n", file); */
        return 7;
    }

    return 0;
}

uint8_t get_mem_range_elf(Elf *elf, uint32_t *addr_min, uint32_t *addr_max)
{
    GElf_Phdr phdr; // PHDR = Program Header
    size_t n, i;

    // initial values
    (*addr_max) = 0;
    (*addr_min) = UINT_MAX;

    if (elf_getphdrnum(elf, &n) != 0) {
        /* printf("elf_getphdrnum() failed\n"); */
        return 1;
    }

    for (i = 0; i < n; i++) {
        if (gelf_getphdr(elf, i, &phdr) != &phdr) {
            /* printf("gelf_getphdr() failed\n"); */
            break;
        }

        // p_paddr = Segment Physical Address
        if (phdr.p_paddr < (*addr_min))
            (*addr_min) = phdr.p_paddr;

        // p_memsz = Segment size in memory
        if (phdr.p_paddr+phdr.p_memsz > (*addr_max))
            (*addr_max) = phdr.p_paddr+phdr.p_memsz;
    }

    if (i < n)
        return 2;

    /* printf("%s is a valid ELF file\n", file); */
    /* printf("Mapping ELF from [0x%08x - 0x%08x] (len = %u)\n", */
    /*         addr_min, addr_max, addr_max - addr_min); */

    return 0;
}

uint8_t get_symbol_values(
        Elf *elf,
        uint32_t *main_sym,
        uint32_t *uart_init_sym,
        uint32_t *uart_tx_sym,
        uint32_t *uart_rx_sym,
        uint32_t *stack_sym,
        uint32_t *stack_size_sym)
{
    Elf_Scn     *scn = NULL;
    GElf_Shdr   shdr;
    Elf_Data    *data;
    GElf_Sym    sym;
    uint32_t    i, count;
    uint8_t     progress = 0;

    // @TODO initialize to dynamic value
    (*stack_size_sym) = 1024 * 100;

    while ((scn = elf_nextscn(elf, scn)) != NULL) {
        gelf_getshdr(scn, &shdr);

        if (shdr.sh_type == SHT_SYMTAB)
            /* found a symbol table, go print it. */
            break;
    }

    if (scn == NULL)
        return 1;

    data    = elf_getdata(scn, NULL);
    count   = shdr.sh_size / shdr.sh_entsize;

    /* print the symbol name */
    for (i = 0; i < count; ++i) {
        gelf_getsym(data, i, &sym);
        const char *sym_name = elf_strptr(elf, shdr.sh_link, sym.st_name);

        if (!strcmp("main", sym_name)) {
            (*main_sym) = sym.st_value;
            ++progress;
        } else if (!strcmp("Stack_Size", sym_name)) {
            (*stack_size_sym) = sym.st_value;
            ++progress;
        } else if (!strcmp("__stack", sym_name)) {
            (*stack_sym) = sym.st_value;
            ++progress;
        } else if (!strcmp("unicorn_uart_tx", sym_name)) {
            (*uart_tx_sym) = sym.st_value;
            ++progress;
        } else if (!strcmp("unicorn_uart_rx", sym_name)) {
            (*uart_rx_sym) = sym.st_value;
            ++progress;
        }
    }

    /* printf("progress: %u\n", progress); */
    if (progress < 4)
        return 2;

    return 0;
}

uint8_t load_elf(
        uc_engine   *uc,
        const char  *file,
        uint32_t    *addr_min,
        uint32_t    *addr_len,
        uint32_t    *main_sym,
        uint32_t    *uart_init_sym,
        uint32_t    *uart_tx_sym,
        uint32_t    *uart_rx_sym)
{
    uc_err err;
    GElf_Phdr phdr; // PHDR = Program Header
    Elf *elf;
    int fd;
    void *elf_buf;
    uint32_t addr_max, stack_sym, stack_size_sym;
    size_t n, i;
    uint8_t ret = 0;

    if (get_elf_stat(file, &elf, &fd, &elf_buf) != 0) {
        ret = 1;

        if (!elf_buf)
            goto FAILED_MOLLOC;
        else if (!elf)
            goto FAILED_OPEN_ELF;
        else if (!fd)
            goto FAILED_READ;
    }

    if (get_mem_range_elf(elf, addr_min, &addr_max) != 0) {
        ret = 2;
        goto FAILED_MOLLOC;
    }

    /* printf("Mapping Memory from 0x%08x to 0x%08x, length = %u\n", */
    /*         addr_min, addr_max, addr_max - addr_min); */

    (*addr_len) = (((addr_max - (*addr_min)) + 1023) / 1024) * 1024;

    err = uc_mem_map(uc, *addr_min, *addr_len, UC_PROT_ALL);
    if (err) {
        /* printf("uc_mem_map() error: %u (%s)\n", err, uc_strerror(err)); */
        ret = 3;
        goto FAILED_MOLLOC;
    }
    /* printf("mapping from %x to %x\n", addr_min, addr_max); */

    if (elf_getphdrnum(elf, &n) != 0) {
        ret = 4;
        goto FAILED_MOLLOC;
    }

    for (i = 0; i < n; ++i) {
        if (gelf_getphdr(elf, i, &phdr) != &phdr)
            break;

        err = uc_mem_write(uc, phdr.p_paddr,
                &((uint8_t*)elf_buf)[phdr.p_offset], phdr.p_filesz);

        if (err)
            break;
    }

    if (i < n) {
        ret = 5;
        goto FAILED_MOLLOC;
    }

    if (get_symbol_values(
                elf,
                main_sym,
                uart_init_sym,
                uart_tx_sym,
                uart_rx_sym,
                &stack_sym,
                &stack_size_sym) != 0) {
        ret = 6;
        goto FAILED_MOLLOC;
    }

    uc_reg_write(uc, UC_ARM_REG_SP, &stack_sym);
    /* printf("stack_sym: 0x%x\n", stack_sym); */
    /* printf("stack_size_sym: 0x%x\n", stack_size_sym); */

    /* map stack */
    err = uc_mem_map(
            uc,
            stack_sym,
            stack_size_sym,
            UC_PROT_ALL);

    /* if (err) { */
    /*     printf("uc_mem_map() error: %u (%s)\n", err, uc_strerror(err)); */
    /* } */

FAILED_MOLLOC:
    free(elf_buf);
FAILED_OPEN_ELF:
    elf_end(elf);
FAILED_READ:
    close(fd);

    return ret;
}

static void hook_uart_tx(int fd, char tx)
{
    printf("%c", tx);
    /* write(fd, &tx, 1); */
}

static char hook_uart_rx(int fd)
{
    char x = 0;

    while (read(fd, &x, 1) <= 0);

    return x;
}

static void hook_code(
        uc_engine *uc,
        uint64_t address,
        uint32_t size,
        void *user_data)
{
    struct symbols *sym = (struct symbols *) user_data;
    uint32_t pc;

    uc_reg_read(uc, UC_ARM_REG_PC, &pc);
    /* printf("pc@ %08x\n", pc); */

    /* stubs for the serial port */
    if (pc == (sym->unicorn_uart_tx & -2)) {
        /* my uart tx char */
        /* void my_uart_tx_char(char c); */
        uint32_t lr, r0;
        uc_reg_read(uc, UC_ARM_REG_LR, &lr);
        uc_reg_read(uc, UC_ARM_REG_R0, &r0);

        hook_uart_tx(sym->fd_pipe_out, (char)r0);

        /* printf("my_uart_tx_char ret@0x%08x\n", lr); */
        uc_reg_write(uc, UC_ARM_REG_PC, &lr);
        return;
    } else if (pc == (sym->unicorn_uart_rx & -2)) {
        /* unsigned my_uart_rx_char(void); */
        uint32_t lr, r0;

        uc_reg_read(uc, UC_ARM_REG_LR, &lr);

        r0 = hook_uart_rx(sym->fd_pipe_in);

        uc_reg_write(uc, UC_ARM_REG_R0, &r0);
        /* printf("my_uart_rx_char ret@0x%08x\n", lr); */
        uc_reg_write(uc, UC_ARM_REG_PC, &lr);
        return;
    }
}

int main(int argc, char *argv[])
{
    uc_err      err;
    uc_engine   *uc;
    uc_hook     trace_code;
    struct symbols sym;
    /* int         fd_pipe_in, fd_pipe_out; */
    uint8_t     result;
    uint32_t    main_sym,
                unicorn_uart_init,
                unicorn_uart_tx,
                unicorn_uart_rx,
                addr_min,
                addr_len;

    switch (argc) {
        case 1:
            printf("Please specify an ELF file\n");
        /* case 2: */
        /*     printf("Please specify an pipe.in file\n"); */
        /* case 3: */
        /*     printf("Please specify an pipe.out file\n"); */
            return -1;
        /* case 4: */
        case 2:
            break;
    }

    err = uc_open(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS, &uc);
    if (err) {
        printf("uc_open() error: %u (%s)\n", err, uc_strerror(err));
        return -2;
    }

    result = load_elf(
            uc,
            argv[1],
            &addr_min,
            &addr_len,
            &main_sym,
            &unicorn_uart_init,
            &unicorn_uart_tx,
            &unicorn_uart_rx);

    if (result != 0) {
        printf("load_elf() error: %u\n", result);
        return -3;
    }

    /* printf("waiting for pipe connection ...\n"); */

    /* fd_pipe_in  = open(argv[2], O_RDONLY); */
    /* fd_pipe_out = open(argv[3], O_NOCTTY|O_SYNC|O_WRONLY); */

    /* if (fd_pipe_in == 0 || fd_pipe_out == 0) { */
    /*     printf("openning pipes error\n"); */
    /*     return -4; */
    /* } */

    sym.unicorn_uart_init   = unicorn_uart_init;
    sym.unicorn_uart_tx     = unicorn_uart_tx;
    sym.unicorn_uart_rx     = unicorn_uart_rx;
    /* sym.fd_pipe_in          = fd_pipe_in; */
    /* sym.fd_pipe_out         = fd_pipe_out; */

    uc_hook_add(
            uc,
            &trace_code,
            UC_HOOK_CODE,
            hook_code,
            &sym,
            addr_min,
            addr_len);

    /* printf("Load ELF file Success\n" */
    /*         "main:              0x%x\n" */
    /*         "unicorn_uart_init: 0x%x\n" */
    /*         "unicorn_uart_tx:   0x%x\n" */
    /*         "unicorn_uart_rx:   0x%x\n", */
    /*         main_sym, */
    /*         unicorn_uart_init, */
    /*         unicorn_uart_tx, */
    /*         unicorn_uart_rx); */

    err = uc_emu_start(
            uc,
            main_sym|1,
            addr_min + addr_len,
            10 * 1000 * 1000, 0);

    /* if (err) { */
    /*     printf("uc_emu_start(), error: %u: %s\n", err, uc_strerror(err)); */
    /* } */

    uc_close(uc);

    return 0;
}
