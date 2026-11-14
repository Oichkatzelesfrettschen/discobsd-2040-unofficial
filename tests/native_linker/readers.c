/* Exercise the production reader with input supplied independently of it. */
#define main native_linker_main
#include NATIVE_LINKER_SOURCE
#undef main

int
main(int argc, char **argv)
{
    struct nlist symbol;

    if (argc != 2)
        return 2;
    if (strcmp(argv[1], "word") == 0) {
        printf("%u\n", fgetword(stdin));
    } else if (strcmp(argv[1], "section") == 0 ||
               strcmp(argv[1], "short-section") == 0) {
        unsigned size = fgetword(stdin);
        int available = strcmp(argv[1], "short-section") == 0 ? getc(stdin) : 8;
        FILE *output = tmpfile(), *records = tmpfile();
        int byte;

        if (available < 0 || available > 8)
            return 2;
        text = tmpfile();
        if (text == NULL || output == NULL || records == NULL)
            return 2;
        for (byte = 0; byte < available; byte++)
            putc(0, text);
        rewind(text);
        reloc = stdin;
        relocate_thumb(local, output, records, 8, 0, size, 0);
        printf("%ld\n", ftell(output));
        fclose(text);
        fclose(output);
        fclose(records);
    } else if (strcmp(argv[1], "relocation") == 0) {
        struct reloc record;

        fgetrel_thumb(stdin, &record, getc(stdin));
        printf("%u %u %u\n", record.flags, record.addr, record.index);
    } else if (strcmp(argv[1], "symbol") == 0) {
        int length = fgetsym(stdin, &symbol, getc(stdin));

        if (length == 0) {
            puts("end");
        } else {
            printf("%d %u %u %s\n", length, (unsigned)symbol.n_type,
                symbol.n_value, symbol.n_name);
            free(symbol.n_name);
        }
    } else {
        return 2;
    }
    return 0;
}
