#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <endian.h>
#include <unistd.h>

uint8_t *processingPTR0;
uint32_t state0, state1, origBytesToProcess;

signed int beginDecompress(uint8_t *, uint8_t **);
int decompressGivenPOutAndOutFileSize(uint8_t *, uint32_t);
int fun0();
int fun1(unsigned int);
void print_usage(char*);
void check_headers(FILE**, uint32_t*, uint32_t*);
void organise_input(int, char**, int*, int*);

void print_usage(char* prog_name)
{
    fprintf(stderr, "\
Usage: %s [input_file] [output_file]\n", prog_name);
    fputs("\
Decompress Binary MOF file specified by input_file into output_file\n\
\n\
input_file and output_file can be replaced by redirections or pipes\n\
of the standard input/output. If only one argument is specified, the\n\
other will be assumed to be whichever redirection is available. If\n\
there aren't enough parameters available, this usage message will be\n\
shown.\n\
\n\
\tExample usages:\n", stderr);
    fprintf(stderr, "\
\t\t%s file_in file_out\n\
\t\t%s file_in > file_out\n\
\t\t%s file_out < file_in\n\
\t\tcat file_in | %s | cat\n\
\n\
", prog_name, prog_name, prog_name, prog_name);
    exit(EXIT_FAILURE);
}

int fun1(unsigned int a1) //input is just a number
{
    int32_t v1;
    int v2;
    int v3;
    uint32_t v4;
    unsigned int v5;
    int v6;
    int v8;
    int v9;
    int v10;

    v1 = state1;
    if ( a1 > state1 ) {
        v3 = 0;
        v9 = 0;
        v4 = state0;
        v8 = 0;
        v5 = a1;
        v10 = 0;
        while ( 1 ) {
            if ( !v1 ) { //if local copy of state 1 is 0
                v6 = origBytesToProcess--; //process 1 byte
                if ( !v6 ) {
                    perror("An error occurred whilst decompressing: ran out of input to use!");
                    exit(EXIT_FAILURE);
                }
                v4 = *processingPTR0; //read a byte
                v3 = v8;
                v1 = 8;
                state0 = *processingPTR0++; //read single byte, no need for endian fix
            }
            v9 |= v4 << v3;
            if ( v1 >= v5 )
                break;
            v3 += v1;
            v5 -= v1;
            v1 = 0;
            v8 = v3;
            state1 = 0;
            if ( !v5 ) {
                v10 = 1;
                break;
            }
        }
        if (!v10) {
            state1 = v1 - v5;
            state0 = v4 >> v5;
        }
        v2 = v9;
    } else {
        v2 = state0;
        state1 -= a1;
        state0 = state0 >> a1;
    }
    return v2 & ((1 << a1) - 1); //select bit a1 of state0
}

int fun0()
{
    int32_t v0;
    int32_t v1;
    uint32_t v2;

    if ( state1 ) {
        --state1;
        v0 = state0 & 1;
        state0 = state0 >> 1;
    } else {
        v1 = origBytesToProcess--;
        if ( !v1 ) { //quit if needs be
            perror("An error occurred whilst decompressing: ran out of input to use!");
            exit(EXIT_FAILURE);
        }
        v0 = *processingPTR0 & 1;
        v2 = le32toh((uint32_t)*processingPTR0) >> 1;
        state1 = 7;
        ++processingPTR0;
        state0 = v2;
    }
    return v0;
}

int decompressGivenPOutAndOutFileSize(uint8_t *pOut, uint32_t outFileSize)
{
    uint8_t curByte;
    uint8_t *byte_ptr0;
    uint8_t byte0;
    uint8_t *byte_ptr1;
    uint8_t *heap_ptr0;
    int32_t v7;
    uint8_t v8;
    int32_t v9;
    uint32_t v11;
    int32_t v12;
    uint8_t *v13;
    uint8_t *v14;
    uint8_t *v15;
    int32_t v16;

    curByte = *processingPTR0;
    byte_ptr0 = ++processingPTR0; //pre-increment
    if ( curByte == 68 ) { //== 0x44 == 'D'
        byte0 = *byte_ptr0;
        byte_ptr1 = byte_ptr0 + 1;
        processingPTR0 = byte_ptr1;
        if ( byte0 == 83 ) { //== 0x53 == 'S'
            origBytesToProcess -= 4;
            processingPTR0 = byte_ptr1 + 2; //moved forward 4 bytes
            heap_ptr0 = pOut;
            while ( 1 ) { //loop forever
                v7 = fun1(2u);
                if ( v7 == 1) {
                    v8 = fun1(7u) | 0x80;
                    *heap_ptr0++ = v8;
                    continue;
                }
                if (v7 == 2 ) {
                    v8 = fun1(7u);
                    *heap_ptr0++ = v8;
                    continue;
                } else {
                    if ( v7 ) {
                        if ( fun0() )
                            v9 = fun1(0xCu) + 320;
                        else
                            v9 = fun1(8u) + 64;
                    } else {
                        v9 = fun1(6u);
                    }
                    if ( v9 == 4415 ) {
                        if ( heap_ptr0 - pOut >= outFileSize ) {
                            return heap_ptr0 - pOut;
                        }
                    } else {
                        v11 = 0;
                        while ( !fun0() )
                            ++v11;
                        if ( v11 )
                            v12 = fun1(v11) + (1 << v11) + 1;
                        else
                            v12 = 2;
                        v13 = heap_ptr0;
                        v14 = heap_ptr0;
                        heap_ptr0 += v12;
                        v15 = v13 - v9;
                        if ( v12 ) {
                            v16 = v15 - v14;
                            do {
                                *v14 = *(v16 + v14);
                                ++v14;
                                --v12;
                            } while ( v12 );
                        }
                    }
                }
            }
        }
    }
    return -1;
}

int main(int argc, char**argv)
{
    FILE *inp_fd, *out_fd;
    uint8_t *inp_map, *out_map = NULL;
    int manual_infile = 0, manual_outfile = 0, ret = 0;
    uint32_t in_size, out_filesize;

    organise_input(argc, argv, &manual_infile, &manual_outfile);

    if (manual_infile) { //open input file
        inp_fd = fopen(argv[manual_infile], "rb");
        if (inp_fd == NULL) {
            perror("Error opening input file for reading");
            exit(EXIT_FAILURE);
        }
    } else
        inp_fd = stdin;

    //check headers, read input and outout file expected sizes
    check_headers(&inp_fd, &in_size, &out_filesize);

    //allocate memory for input file and read it in
    inp_map = (uint8_t*)malloc(in_size*sizeof(uint8_t));
    if (inp_map == NULL) {
        fclose(inp_fd);
        perror("Error allocating memory for the input file");
        exit(EXIT_FAILURE);
    }
    if (fread((void*)inp_map, in_size, 1, inp_fd) != 1) {
        free(inp_map);
        fclose(inp_fd);
        perror("Error reading the input file");
        exit(EXIT_FAILURE);
    }
    fclose(inp_fd);

    if (manual_outfile) { //open output file
        out_fd = fopen(argv[manual_outfile], "w+b");
        if (out_fd == NULL) {
            fclose(inp_fd);
            perror("Error opening output file for writing");
            exit(EXIT_FAILURE);
        }
    } else
        out_fd = stdout;

    //allocate memory for output file buffer
    out_map = (uint8_t*)malloc(out_filesize*sizeof(uint8_t));
    if (out_map == NULL) {
        perror("Error allocating memory for the output file");
        exit(EXIT_FAILURE);
    }

    processingPTR0 = inp_map;
    state0 = 0;
    state1 = 0;
    origBytesToProcess = in_size;
    ret = decompressGivenPOutAndOutFileSize(out_map, out_filesize);
    if ( ret == out_filesize ) { //check for successful expansion
        fprintf(stderr, "Input expanded to %d bytes!\n", ret);
        fwrite((void*)out_map, ret, 1, out_fd);
    } else {
        fprintf(stderr, "Decompression returned %d\n", ret);
    }

    free(inp_map);
    free(out_map);
    fclose(out_fd);

    return ret;
}

void organise_input(int argc, char**argv, int *manual_infile, int *manual_outfile)
{ //sort out where the io is coming from - stdin, stdout and/or parameters
    int stdin_tty, stdout_tty;
    stdin_tty = isatty(fileno(stdin));
    stdout_tty = isatty(fileno(stdout));

    if (argc == 1 && (stdin_tty || stdout_tty))
        print_usage(argv[0]);

    if (argc == 2) {
        if (stdin_tty &&  !stdout_tty) {
            *manual_infile = 1;
        } else if (stdout_tty && !stdin_tty) {
            *manual_outfile = 1;
        } else {
            print_usage(argv[0]);
        }    
    }

    if (argc == 3) {
        *manual_infile = 1;
        *manual_outfile = 2;
    }

    if (argc >=4)
        print_usage(argv[0]);
}

void check_headers(FILE **inp_fd, uint32_t *in_size, uint32_t *out_filesize)
{
    uint32_t sig, version;
    //get the input file size by reading the header
    if (fread(&sig, 4, 1, *inp_fd) != 1) {
        fclose(*inp_fd);
        perror("Error reading the input file header");
        exit(EXIT_FAILURE);
    }

    if (fread(&version, 4, 1, *inp_fd) != 1) {
        fclose(*inp_fd);
        perror("Error reading the input file header");
        exit(EXIT_FAILURE);
    }

    if (fread(in_size, 4, 1, *inp_fd) != 1) {
        fclose(*inp_fd);
        perror("Error reading the input file header");
        exit(EXIT_FAILURE);
    }

    if (fread(out_filesize, 4, 1, *inp_fd) != 1) {
        fclose(*inp_fd);
        perror("Error reading the input file header");
        exit(EXIT_FAILURE);
    }

    //DWORD lpBaseAddress is FOMB = (opposite in hex edit due to little endian) 424d4f46 = 1112362822
    //DWORD lpBaseAddress + 1 appears to have to be the number 1, probably a version number
    //DWORD lpBaseAddress + 2 appears to be the size of the data, which is the size of this input file, less 16 bytes for the header
    //DWORD lpBaseAddress + 3 is little endian expected expanded file size

    sig = le32toh(sig);
    version = le32toh(version);
    *in_size = le32toh(*in_size);
    *out_filesize = le32toh(*out_filesize);

    if ( sig != 1112362822 || version != 1 ) {//check magic header and version
        fclose(*inp_fd);
        perror("Input is not a valid binary MOF file");
        exit(EXIT_FAILURE);
    }

    if ( *in_size <= 4 ) {
        fclose(*inp_fd);
        perror("Input is too small");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "Input data size is %d bytes\n", *in_size);
}

