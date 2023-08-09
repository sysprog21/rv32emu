#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

#include "elf.h"
#include "elf_header.h"
#include "file.h"
#include "readelf.h"
#include "section.h"

void read_elf_data(const char *path, int flags)
{
    FILE *file = open_file(path);
    Elf_Internal_Ehdr *header = get_elf_header(file);

    if (flags & FLAG_ELF_HEADER)
        display_file_header(header);

    if (flags & FLAG_SECTION_HEADER)
        display_section_header(file, header);

    free(header);

    if (fclose(file)) {
        perror("readelf");
        exit(errno);
    }
}

int main(int argc, char *argv[])
{
    read_elf_data(argv[0], FLAG_ELF_HEADER | FLAG_SECTION_HEADER);

    return 0;
}
