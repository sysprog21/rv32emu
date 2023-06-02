#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

FILE *open_file(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("readelf");
        exit(errno);
    }

    return file;
}

void *read_file(FILE *file, size_t size, off_t offset)
{
    void *buf = malloc(size);
    if (!buf) {
        perror("readelf");
        exit(errno);
    }

    off_t off = fseek(file, offset, SEEK_SET);
    if (off == -1) {
        perror("readelf");
        exit(errno);
    }

    int n = fread(buf, size, 1, file);
    if (n == 1) {
        return buf;
    }

    if (n < 0) {
        perror("readelf");
    } else {
        fprintf(stderr, "readelf: Error: Not a valid ELF file\n");
    }

    free(buf);
    exit(errno);
}
