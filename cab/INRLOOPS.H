;/*`.......................... assembly language .............................
        .586
        .model flat

_IL_DATA segment page use32 public 'DATA'   ;  ;DATA segment prototype
_IL_DATA ends
_IL_TEXT segment page use32 public 'CODE'   ;  ;CODE segment prototype
_IL_TEXT ends

 comment `....................... C language ...............................*/
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <search.h>
//#include <graph.h>

#pragma aux MS_C "_*"                            \
         parm caller []                          \
         value struct float struct routine [eax] \
         modify [eax ecx edx];
         
;/*`.......................... assembly language .............................

_IL_DATA segment
dummytimer      dd      0
_IL_DATA ends

 comment `....................... C language ...............................*/
//
// CHRANDM function prototypes
//
#pragma aux (MS_C) IL_StandardRandom_A;
long               IL_StandardRandom_A();
#pragma aux (MS_C) IL_SimpleRandom_A;
long               IL_SimpleRandom_A();
#pragma aux (MS_C) IL_FastRandom_A;
long               IL_FastRandom_A();
#pragma aux (MS_C) IL_Shift32Random_A;
long               IL_Shift32Random_A();
#pragma aux (MS_C) IL_MixRandom_A;
long               IL_MixRandom_A();
#pragma aux (MS_C) IL_MixRandomSeed_A;
void               IL_MixRandomSeed_A(long (*rand)());
#pragma aux (MS_C) IL_TrillionsRandom_A;
long               IL_TrillionsRandom_A();
#pragma aux (MS_C) IL_TrillionsRandomSeed_A;
void               IL_TrillionsRandomSeed_A(long (*rand)());
#pragma aux (MS_C) IL_HashRandom_A;
long               IL_HashRandom_A();
#pragma aux (MS_C) IL_R250Random_A;
long               IL_R250Random_A();
#pragma aux (MS_C) IL_R250RandomSeed_A;
void               IL_R250RandomSeed_A(long (*rand)());
#pragma aux (MS_C) IL_PiByMonteCarlo_A;
unsigned long      IL_PiByMonteCarlo(long (*rand)(), unsigned long tests);
#pragma aux (MS_C) IL_PrimeSieve_A;
long               IL_PrimeSieve_A(void *arrayptr, unsigned long arraybits, 
                                                         unsigned long prime);
//
// CHHUFMN function prototypes
//
typedef struct {
    long          value;
    unsigned long count;
} IL_HuffItem;

typedef struct IL_HuffNode {            // 16-byte node structure
    union {                             // the FIRST four bytes
        struct IL_HuffNode *zero;       // points to the "zero" branch or...
        unsigned long       value;      // holds the value of an end node
    };
    union {                             // the SECOND four bytes
        struct IL_HuffNode *one;        // points to the "one" branch or...
        struct IL_HuffNode *link;       // points to next end node in list
    };
    struct IL_HuffNode     *parent;     // the THIRD four bytes, parent node
    union {                             // the FOURTH four bytes
        unsigned long       bits;       // the bit pattern of this end node
        struct {
            unsigned char   flag;
            unsigned char   curdepth;
            unsigned char   maxdepth;
            unsigned char   unused;  
        };
    };
} IL_HuffNode;

typedef struct {
    long wid;
    long bits;
} IL_HuffEncode;

#pragma aux (MS_C) IL_HuffmanByteCensus_A;
void               IL_HuffmanByteCensus_A(IL_HuffItem *array, 
                                           unsigned char *source, long count);
#pragma aux (MS_C) IL_MakeHuffTreeFromHuffItems_A;
void               IL_MakeHuffTreeFromHuffItems_A(IL_HuffNode *base,
                                                IL_HuffItem *src, long count);
#pragma aux (MS_C) IL_MakeCodeLengthsFromHuffTree_A;
void               IL_MakeCodeLengthsFromHuffTree_A(long *dest, 
                                          IL_HuffNode *source, long maxdepth);
#pragma aux (MS_C) IL_HuffEncodeBytes_A;
long               IL_HuffEncodeBytes_A(long *ptrout, char *ptrin, long count,
                                                        IL_HuffEncode *table);
#pragma aux (MS_C) IL_MakeHuffmanDecodeTable_A;
void IL_MakeHuffmanDecodeTable_A(long *outtbl, long *depth, long depthmax, 
                                                                  char *list);
#pragma aux (MS_C) IL_DecompressHuffman_A;
long IL_DecompressHuffman_A(char *dest, long *src, long *table);

//
// CHHASH function prototypes
//
typedef struct IL_HashSymbol {
    long                  hash;         // hash code
    long                  len;          // length in longwords
    struct IL_HashSymbol *below;        // must immediately preceed above
    struct IL_HashSymbol *above;        // must immediately follow below
    long                  data;         // arbitrary data
} IL_HashSymbol;

typedef struct {
    long    negmax;              // negative of max string byte length
    IL_HashSymbol **table;       // pointer to table of hash pointers
    long    tablesize;           // size of table in bytes
    void  *(*malloc)(long size); // malloc function
    long    chunksize;           // size for memory symbol chunks
    long   *string;              // pointer to new symbol string storage
    long   *strlim;              // pointer to end of string storage
    long   *cutoff;              // chunk cutoff for strlim value
    long    symlen;              // bytes in latest symbol looked up
    long    msk;                 // mask for table lookup
    char   *xlattbl;             // character translation table
} IL_HashTable;

typedef union {
    unsigned char byte[4];
    unsigned long hash;
} IL_HashWord;

#pragma aux (MS_C) IL_HashFindSymbol_A;
IL_HashSymbol **IL_HashFindSymbol_A(char *symptr, IL_HashTable *table);
#pragma aux (MS_C) IL_HashAddSymbol_A;
IL_HashSymbol *IL_HashAddSymbol_A(IL_HashSymbol **symptr, long val,
                                                        IL_HashTable *table);
#pragma aux (MS_C) IL_HashCountSymbols_A;
long IL_HashCountSymbols_A(char *text, IL_HashTable *table, long *ucount);
//
// CHMEM function prototypes
//
#pragma aux (MS_C) IL_MemCopyToSecondaryCache_A;
void               IL_MemCopyToSecondaryCache_A(void *dest, void *src,
                                                                   int count);
#pragma aux (MS_C) IL_MemCopyAlignedRepMovsd_A;
void               IL_MemCopyAlignedRepMovsd_A(void *dest, void *src,
                                                                   int count);
#pragma aux (MS_C) IL_MemCopyLoadStoreLoop_A;
void               IL_MemCopyLoadStoreLoop_A(void *dest, void *src,
                                                                   int count);
#pragma aux (MS_C) IL_MemCopyToMainMemory_A;
void               IL_MemCopyToMainMemory_A(void *dest, void *src, int count);
//
// CHSORT function prototypes
//
#pragma aux (MS_C) IL_DistributionSortPart1_A;
void               IL_DistributionSortPart1_A(unsigned long *countarray, 
 long offset, unsigned char **sourceptr, unsigned char *bytelist, long count);
#pragma aux (MS_C) IL_DistributionSortPart1A_A;
void               IL_DistributionSortPart1A_A(unsigned long *countarray,
 long offset, unsigned char **sourceptr, unsigned char *bytelist, long count);
#pragma aux (MS_C) IL_DistributionSortPart2_A;
void               IL_DistributionSortPart2_A(unsigned char ***destptr, 
                 unsigned char **srcptr, unsigned char *bytelist, long count);
//
// CHMATRX function prototypes
//
#pragma aux (MS_C) IL_3DRotateAndDisplace_A;
void               IL_3DRotateAndDisplace_A(float *dest, float *source,
                                                   float *matrix, long count);
//
// CHSPEED function prototypes
//
#pragma aux (MS_C) IL_WordCount7;
int                IL_WordCount7(unsigned char *p, int count);
#pragma aux (MS_C) IL_WordCount8;
int                IL_WordCount8(unsigned char *p, int count);
#pragma aux (MS_C) IL_WordCount9;
int                IL_WordCount9(unsigned char *p, int count);
#pragma aux (MS_C) IL_WordCount10;
int                IL_WordCount10(unsigned char *p, int count);

;/*`.......................... assembly language .............................
 comment `....................... C language ...............................*/

// . . . . . . . . . . . . . . .  IL_malloc()  . . . . . . . . . . . . . . . .

static unsigned long IL_malloc_count  = 0;
static unsigned long IL_malloc_space  = 0;
static unsigned long IL_malloc_chunks = 0;

void *
IL_malloc(unsigned long size)
{
    unsigned long *ret;

    IL_malloc_count++;
    if ((size > 0xffffffff - sizeof(long)) || 
                                      !(ret = malloc(size + sizeof(long)))) {
        fprintf(stderr, 
                "Program aborted: IL_malloc(%ld) failed on call #%ld.  "
                "(%ld bytes previously allocated in %ld chunks)\n",
                size, IL_malloc_count, IL_malloc_space, IL_malloc_chunks);
        exit(1);
    }
    IL_malloc_chunks++;
    IL_malloc_space += size;
    *ret++ = size;
    return ret;
}

// . . . . . . . . . . . . . . .  IL_calloc()  . . . . . . . . . . . . . . . .

static unsigned long IL_calloc_count  = 0;

void *
IL_calloc(unsigned long num, unsigned long size)
{
    unsigned long *ret;

    IL_calloc_count++;
    size *= num;
    if ((size > 0xffffffff - sizeof(long)) || 
                                   !(ret = calloc(size + sizeof(long), 1))) {
        fprintf(stderr, 
                "Program aborted: IL_calloc(%ld) failed on call #%ld.  "
                "(%ld bytes previously allocated in %ld chunks)\n",
                size, IL_calloc_count, IL_malloc_space, IL_malloc_chunks);
        exit(1);
    }
    IL_malloc_chunks++;
    IL_malloc_space += size;
    *ret++ = size;
    return ret;
}

// . . . . . . . . . . . . . . . . . IL_free() . . . . . . . . . . . . . . . .

void
IL_free(void *memblock)
{
    unsigned long *temp;

    temp = memblock;
    IL_malloc_space -= *--temp;
    IL_malloc_chunks--;
    free(temp);
}

// . . . . . . . . . . . . . . .  IL_fopen() . . . . . . . . . . . . . . . . .

static unsigned long IL_fopen_count = 0;

FILE *
IL_fopen(char *filename, char *mode)
{
    FILE *ret;

    IL_fopen_count++;
    ret = fopen(filename, mode);
    if (!ret) {
        fprintf(stderr,
                "Program aborted: IL_fopen(\"%s\", \"%s\") "
                "failed on call #%ld.\n",
                filename, mode, IL_fopen_count);
        exit(1);
    }
    return ret;
}

// . . . . . . . . . . . . . . .  IL_fclose()  . . . . . . . . . . . . . . . .

static unsigned long IL_fclose_count = 0;

int
IL_fclose(FILE *stream)
{
    int ret;

    IL_fclose_count++;
    ret = fclose(stream);
    if (ret != 0) {
        fprintf(stderr, 
                "Program aborted: IL_fclose(0x%08lx) failed on call #%ld.\n",
                stream, IL_fclose_count);
        exit(1);
    }
    return ret;
}

// . . . . . . . . . . . . . . .  IL_fread() . . . . . . . . . . . . . . . . .

static unsigned long IL_fread_count = 0;

unsigned long
IL_fread(void *buffer, unsigned long size, unsigned long count, FILE *stream)
{
    unsigned long ret;

    IL_fread_count++;
    ret = fread(buffer, size, count, stream);
    if (!(ret == size * count)) {
        fprintf(stderr,
                "Program aborted: IL_fread(0x%08lx, %ld, %ld, 0x%08lx) "
                "failure on call %ld.  (returned %ld)\n",
                buffer, size, count, stream, IL_fread_count, ret);
        exit(1);
    }
    return ret;
}

// . . . . . . . . . . . . . . .  IL_fwrite()  . . . . . . . . . . . . . . . .

static unsigned long IL_fwrite_count = 0;

unsigned long
IL_fwrite(void *buffer, unsigned long size, unsigned long count, FILE *stream)
{
    unsigned long ret;

    IL_fwrite_count++;
    ret = fwrite(buffer, size, count, stream);
    if (!(ret == size * count)) {
        fprintf(stderr,
                "Program aborted: IL_fwrite(0x%08lx, %ld, %ld, 0x%08lx) "
                "failure on call %ld.  (returned %ld)\n",
                buffer, size, count, stream, IL_fwrite_count, ret);
        exit(1);
    }
    return ret;
}

// . . . . . . . . . . . . . . .  IL_fseek() . . . . . . . . . . . . . . . . .

static unsigned long IL_fseek_count = 0;

int
IL_fseek(FILE *stream, long offset, int origin)
{
    int ret;
    
    IL_fseek_count++;
    ret = fseek(stream, offset, origin);
    if (ret) {
        fprintf(stderr,
                "Program aborted: IL_fseek(0x%08lx, 0x%08lx, %d) "
                "failure on call %ld.  (returned %d)\n",
                stream, offset, origin, IL_fseek_count, ret);
        exit(1);
    }
    return ret;
    
}

// . . . . . . . . . . . . . . .  IL_ftell() . . . . . . . . . . . . . . . . .

static unsigned long IL_ftell_count = 0;

long
IL_ftell(FILE *stream)
{
    long ret;
    
    IL_ftell_count++;
    ret = ftell(stream);
    if (ret == -1) {
        fprintf(stderr,
                "Program aborted: IL_ftell(0x%08lx) "
                "failure on call %ld.  (returned %d)\n",
                stream, IL_ftell_count, ret);
        exit(1);
    }
    return ret;
    
}

// . . . . . . . . . . . .  IL_ReadFileIntoMemory()  . . . . . . . . . . . . .

typedef struct {
    unsigned char *memfile;
    long           size;
} IL_MemFile;

#define IL_MEMFILE_TAIL_PADDING 256 // extra space to insure prefetch hits
                                    // don't cause a crash

IL_MemFile *
IL_ReadFileIntoMemory(char *filename)
{
    FILE *f;
    long size;
    IL_MemFile *ptr;

    f            = IL_fopen(filename, "rb");
    IL_fseek(f, 0, SEEK_END);
    size         = IL_ftell(f);
    IL_fseek(f, 0, SEEK_SET);
    ptr          = IL_malloc(size + sizeof(IL_MemFile) + 
                                                     IL_MEMFILE_TAIL_PADDING);
    ptr->memfile = (unsigned char *)(ptr + 1);
    ptr->size    = size;
    IL_fread(ptr->memfile, 1, ptr->size, f);
    IL_fclose(f);
    return ptr;
}

// . . . . . . . . . . . .  IL_WriteFileFromMemory() . . . . . . . . . . . . .

void
IL_WriteFileFromMemory(char *filename, IL_MemFile *ptr)
{
    FILE *f;

    f = IL_fopen(filename, "wb");
    IL_fwrite(ptr->memfile, 1, ptr->size, f);
    IL_fclose(f);
}

;/*`.......................... assembly language .............................

_IL_TEXT segment page use32 public 'CODE'

;rdtsc  macro
;       db      0fH, 31H
;       endm

IL_CyclesPerSecond_A proc near C public uses ebx esi

        mov     edx,46cH                ;  ;address of DOS tick counter
        mov     eax,[edx]               ;  ;read it
        .repeat                         ;  ;
        .until  eax != [edx]            ;  ;wait for a tick
        mov     esi,[edx]               ;  ;
        rdtsc                           ;  ;read cycles
        mov     ecx,eax                 ;  ;store 32 bits
        mov     ebx,10                  ;  ;
        mov     edx,46cH                ;  ;address of DOS tick counter
        .repeat                         ;  ;
          mov   eax,[edx]               ;  ;read it again
          .repeat                       ;  ;
          .until eax != [edx]           ;  ;wait for a tick
          dec   ebx                     ;  ;
        .until ZERO?                    ;  ;
        sub     esi,[edx]               ;  ;
        rdtsc                           ;  ;read cycles
        neg     esi                     ;  ;esi has actual tick count
        sub     eax,ecx                 ;  ;find difference
        mov     ebx,eax                 ;  ;
        mov     eax,1820651             ;  ;scale to secs, fixed point
        xor     edx,edx                 ;  ;
        div     esi                     ;  ;adjust for actual ticks
        mul     ebx                     ;  ;
        mov     ecx,100000              ;  ;
        div     ecx                     ;  ;eax has cycles per second
        ret                             ;  ;and return it

IL_CyclesPerSecond_A endp

 comment `....................... C language ...............................*/
#pragma aux (MS_C) IL_CyclesPerSecond_A;
long               IL_CyclesPerSecond_A();
;/*`.......................... assembly language .............................

;long IL_ReadPentiumClock_A1(unsigned long *hi)
;
;Returns low longword of processor clock and optionally the high longword
;if "hi" is non-null.
;
IL_ReadPentiumClock_A1 proc near C public, hi:dword ; 2;
        mov     ecx,hi                          ; 1;
        rdtsc                                   ; 8;
        .if ecx                                 ; 1;
          mov   [ecx],edx                       ; 1;
        .endif                                  ;  ;
        ret                                     ; 3;
IL_ReadPentiumClock_A1 endp                     ;;;;16 cycles

;long IL_FakePentiumClock_A(unsigned long *hi)
;
;Returns a fake Pentium clock value (actually just an incrementing value)
;to make code run on pre-Pentium processors.
;
IL_FakePentiumClock_A proc near C public, hi:dword ; 2;
        mov     ecx,hi                          ; 1;
                                                ;  ;
;       rdtsc                                   ; 8;
        inc     dummytimer                      ; 3;
        mov     eax,dummytimer                  ; 1;
        xor     edx,edx                         ; 0;
                                                ;  ;
        .if ecx                                 ; 1;
          mov   [ecx],edx                       ; 1;
        .endif                                  ;  ;
        ret                                     ; 3;
IL_FakePentiumClock_A endp                      ;;;;16 cycles

;long IL_CpuType_A()
;
;Returns number identifying CPU type: 3 for 386, 4 for 486,
;5 for Pentium, 6 for Pentium Pro, (> 6 for posible later generations).
;
IL_CpuType_A proc near C uses ebx esi edi       ;  ;
        pushfd                                  ;  ;
        pop     eax                             ;  ;flags in eax
        mov     ebx,eax                         ;  ;flags in ebx also
        xor     eax,40000H                      ;  ;toggle bit 18
        push    eax                             ;  ;
        popfd                                   ;  ;put it back into flags reg
        pushfd                                  ;  ;
        pop     eax                             ;  ;flags back in eax
        .if eax == ebx                          ;  ;if the bit got undone
          mov   eax,3                           ;  ;then report a 386
          ret                                   ;  ;
        .endif                                  ;  ;
        mov     ebx,eax                         ;  ;flags in eax and ebx
        xor     eax,200000H                     ;  ;toggle bit 21
        push    eax                             ;  ;
        popfd                                   ;  ;put it back into flags reg
        pushfd                                  ;  ;
        pop     eax                             ;  ;flags back in eax
        .if eax == ebx                          ;  ;if the bit got undone
          mov   eax,4                           ;  ;then report a 486
          ret                                   ;  ;
        .endif                                  ;  ;
        mov     eax,1                           ;  ;
        cpuid                                   ;  ;CPUID - 0fH, 0a2H
        and     eax,0f00H                       ;  ;isolate CPU type
        shr     eax,8                           ;  ;in low 4 bits
        ret                                     ;  ;return 5, 6, or later

IL_CpuType_A endp

_IL_TEXT ends

 comment `....................... C language ...............................*/
#pragma aux (MS_C) IL_CpuType_A;
long               IL_CpuType_A(void);
;/*`.......................... assembly language .............................
 comment `....................... C language ...............................*/

int  IL_cputype;
long IL_cps = 2600000000; // seeded with a reasonable dummy value

void
IL_Init(int cpuspeedflag, int pentwarnflag)
{
    char *p;

    printf("\n\n");
    IL_cputype = 6;
    if (IL_cputype < 5) {
        if (pentwarnflag) {
            printf(
"Warning: This program was designed to use the RDTSC fine timing           \n"
"         instruction available only on Pentium and later Intel processors.\n"
"         A pre-Pentium processor has been found in this computer.         \n"
"         Fine timing information generated by this program will be        \n"
"         reported using bogus timing values and should be disregarded.    \n"
"\nPress \'Enter\' to continue.\n");
            getchar();
        } 
    } else {
        //IL_cps = IL_CyclesPerSecond_A();
    }
    if (cpuspeedflag) {
        switch(IL_cputype) {
            case  3: p = "386"; break;
            case  4: p = "486"; break;
            case  5: p = "Pentium"; break;
            case  6: p = "Pentium Pro"; break;
            default: p = "post-Pentium-Pro"; break;
        }
        printf("Processor found: %s", p);
        if (IL_cputype >= 5) {
            printf("        Speed: %5.2f MHz", IL_cps / 1000000.0);
        }
        printf("\n");
    }
}

;/*`.......................... assembly language .............................
 comment `....................... C language ...............................*/
