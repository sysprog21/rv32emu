/*
 * Hello world example.
 * Must be prefixed with architecture-specific code for write().
 */

int main()
{
    write(1 /* stdin*/, "Hello World\x0d\x0a" /* "\x0d\x0a" is a newline */,
          13 /* the length of the string */);
    return 0;
}
