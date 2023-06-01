#ifndef FILE_H
#define FILE_H

FILE *open_file(const char *);

void *read_file(FILE *, size_t, off_t);

#endif  // FILE_H
