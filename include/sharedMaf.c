/* 
 * Copyright (C) 2012 by 
 * Dent Earl (dearl@soe.ucsc.edu, dentearl@gmail.com)
 * ... and other members of the Reconstruction Team of David Haussler's 
 * lab (BME Dept. UCSC).
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE. 
 */
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "CuTest.h"
#include "sharedMaf.h"

struct mafFileApi {
    // a mafFileApi struct provides an interface into a maf file.
    // Allows for easy reading of files in entirety or block by block via
    // functions
    uint32_t lineNumber; // last read line / wrote
    FILE *mfp; // maf file pointer
    char *filename; // filename of the maf
    char *lastLine; /* a temporary cache in case the header fails to have a blank 
                     * line before the first alignment block. 
                     */
};
struct mafLine {
    // a mafLine struct is a single line of a mafBlock
    char *line; // the entire line, unparsed
    uint32_t lineNumber; // line number in the maf file
    char type; // either a, s, i, q, e, h, f where h is header (an internal code)
    char *species; // species name
    uint32_t start;
    uint32_t length;
    char strand;
    uint32_t sourceLength;
    char *sequence; // sequence field
    struct mafLine *next;
};
struct mafBlock {
    // a mafBlock struct contains a maf block as a linked list
    // and itself can be part of a mafBlock linked list.
    mafLine_t *headLine;
    mafLine_t *tailLine;
    uint32_t lineNumber; // line number of start of the block (i.e. the `a' line)
    uint32_t numberOfLines; // number of mafLine_t structures in the *headLine list
    uint32_t numberOfSequences;
    struct mafBlock *next;
};
static bool maf_isBlankLine(char *s) {
    // return true if line is only whitespaces
    size_t n = strlen(s);
    for (size_t i = 0; i < n; ++i) {
        if (!isspace(*(s + i))) {
            return false;
        }
    }
    return true;
}
static void maf_checkForPrematureMafEnd(int status, char *line) {
    if (status == -1) {
        free(line);
        fprintf(stderr, "Error, premature end to maf file\n");
        exit(EXIT_FAILURE);
    }
}
static void maf_failBadFormat(uint32_t lineNumber, char *errorMessage) {
    fprintf(stderr, "The maf sequence at line %u is incorrectly formatted: %s\n", 
            lineNumber, errorMessage);
    exit(EXIT_FAILURE);
}
mafLine_t* maf_newMafLine(void) {
    mafLine_t *ml = (mafLine_t *) de_malloc(sizeof(*ml));
    ml->line = NULL;
    ml->lineNumber = 0;
    ml->type = '\0';
    ml->species = NULL;
    ml->start = 0;
    ml->length = 0;
    ml->strand = 0;
    ml->sourceLength = 0;
    ml->sequence = NULL;
    ml->next = NULL;
    return ml;
}
mafLine_t* maf_newMafLineFromString(char *s, uint32_t lineNumber) {
    extern const int kMaxStringLength;
    mafLine_t *ml = (mafLine_t *) de_malloc(sizeof(*ml));
    char *copy = (char *) de_malloc(strlen(s) + 1);
    char *cline = (char *) de_malloc(strlen(s) + 1);
    strcpy(copy, s);
    strcpy(cline, s);
    ml->next = NULL;
    ml->line = copy;
    ml->lineNumber = lineNumber;
    ml->species = NULL;
    ml->sequence = NULL;
    ml->start = 0;
    ml->length = 0;
    ml->sourceLength = 0;
    ml->strand = 0;
    ml->type = ml->line[0];
    if (ml->type != 's') {
        free(cline);
        return ml;
    }
    char *tkn = NULL;
    tkn = strtok(cline, " \t");
    if (tkn == NULL) {
        free(cline);
        maf_failBadFormat(lineNumber, "Unable to separate line on tabs and spaces at line definition field.");
    }
    tkn = strtok(NULL, " \t"); // name field
    if (tkn == NULL) {
        free(cline);
        maf_failBadFormat(lineNumber, "Unable to separate line on tabs and spaces at name field.");
    }
    char *species = (char *) de_malloc(strlen(tkn) + 1);
    strcpy(species, tkn);
    ml->species = species;
    tkn = strtok(NULL, " \t"); // start position
    if (tkn == NULL) {
        free(cline);
        maf_failBadFormat(lineNumber, "Unable to separate line on tabs and spaces at start position field.");
    }
    ml->start = strtoul(tkn, NULL, 10);
    tkn = strtok(NULL, " \t"); // length position
    if (tkn == NULL){
        free(cline);
        maf_failBadFormat(lineNumber, "Unable to separate line on tabs and spaces at length position field.");
    }
    ml->length = strtoul(tkn, NULL, 10);
    tkn = strtok(NULL, " \t"); // strand
    if (tkn == NULL) {
        free(cline);
        maf_failBadFormat(lineNumber, "Unable to separate line on tabs and spaces at strand field.");
    }
    if (tkn[0] != '-' && tkn[0] != '+') {
        char *error = (char*) de_malloc(kMaxStringLength);
        sprintf(error, "Strand must be either + or -, not %c.", tkn[0]);
        maf_failBadFormat(lineNumber, error);
    }
    ml->strand = tkn[0];
    tkn = strtok(NULL, " \t"); // source length position
    if (tkn == NULL) {
        free(cline);
        maf_failBadFormat(lineNumber, "Unable to separate line on tabs and spaces at source length field.");
    }
    ml->sourceLength = strtoul(tkn, NULL, 10);
    tkn = strtok(NULL, " \t"); // sequence field
    if (tkn == NULL) {
        free(cline);
        maf_failBadFormat(lineNumber, "Unable to separate line on tabs and spaces at sequence field.");
    }
    char *seq = (char *) de_malloc(strlen(tkn) + 1);
    strcpy(seq, tkn);
    ml->sequence = seq;
    free(cline);
    return ml;
}
mafBlock_t* maf_newMafBlock(void) {
    mafBlock_t *mb = (mafBlock_t *) de_malloc(sizeof(*mb));
    mb->next = NULL;
    mb->headLine = NULL;
    mb->tailLine = NULL;
    mb->lineNumber = 0;
    mb->numberOfSequences = 0;
    mb->numberOfLines = 0;
    return mb;
}
mafFileApi_t* maf_newMfa(const char *filename, char const *mode) {
    mafFileApi_t *mfa = (mafFileApi_t *) de_malloc(sizeof(*mfa));
    mfa->lineNumber = 0;
    mfa->lastLine = NULL;
    mfa->mfp = de_fopen(filename, mode);
    mfa->filename = de_strdup(filename);
    return mfa;
}
void maf_destroyMafLineList(mafLine_t *ml) {
    // walk down a mafLine_t following the ->next pointers, search and destroy
    mafLine_t *tmp = NULL;
    while(ml != NULL) {
        tmp = ml;
        ml = ml->next;
        free(tmp->line);
        free(tmp->species);
        free(tmp->sequence);
        free(tmp);
    }
}
void maf_destroyMafBlockList(mafBlock_t *mb) {
    mafBlock_t *tmp = NULL;
    while(mb != NULL) {
        tmp = mb;
        mb = mb->next;
        if (tmp->headLine != NULL)
            maf_destroyMafLineList(tmp->headLine);
        free(tmp);
    }
}
void maf_destroyMfa(mafFileApi_t *mfa) {
    if (mfa->mfp != NULL) {
        fclose(mfa->mfp);
        mfa->mfp = NULL;
    }
    free(mfa->lastLine);
    free(mfa->filename);
    free(mfa);
}
char* maf_mafFileApi_getFilename(mafFileApi_t *mfa) {
    return mfa->filename;
}
uint32_t maf_mafFileApi_getLineNumber(mafFileApi_t *mfa) {
    return mfa->lineNumber;
}
mafLine_t* maf_mafBlock_getHeadLine(mafBlock_t *mb) {
    return mb->headLine;
}
mafLine_t* maf_mafBlock_getTailLine(mafBlock_t *mb) {
    return mb->tailLine;
}
uint32_t maf_mafBlock_getLineNumber(mafBlock_t *mb) {
    return mb->lineNumber;
}
unsigned maf_mafBlock_getNumberOfSequences(mafBlock_t *mb) {
    return mb->numberOfSequences;
}
mafBlock_t* maf_mafBlock_getNext(mafBlock_t *mb) {
    return mb->next;
}
char** maf_mafBlock_getSequenceMatrix(mafBlock_t *mb, unsigned n, unsigned m) {
    // currently this is not stored and must be built
    // should return a matrix containing the alignment, one row per sequence
    char** matrix = NULL;
    matrix = (char**) de_malloc(sizeof(char*) * n);
    unsigned i;
    for (i = 0; i < n; ++i) {
        matrix[i] = (char*) de_malloc(sizeof(char) * (m + 1));
    }
    mafLine_t *ml = maf_mafBlock_getHeadLine(mb);
    i = 0;
    while (ml != NULL) {
        while (maf_mafLine_getType(ml) != 's') {
            ml = maf_mafLine_getNext(ml);
        }
        strncpy(matrix[i], maf_mafLine_getSequence(ml), m);
        matrix[i++][m] = '\0';
        ml = maf_mafLine_getNext(ml);
    }
    return matrix;
}
void maf_mafBlock_destroySequenceMatrix(char **mat, unsigned n) {
    // currently this is not stored and must be built
    // should return a matrix containing the alignment, one row per sequence
    for (unsigned i = 0; i < n; ++i)
        free(mat[i]);
    free(mat);
}
char* maf_mafBlock_getStrandArray(mafBlock_t *mb) {
    // currently this is not stored and must be built
    // should return a char array containing an in-order list of strandedness
    // for all sequence lines. Either + or - char permitted.
    if (maf_mafBlock_getNumberOfSequences(mb) == 0) {
        return NULL;
    }
    char *a = (char*) de_malloc(sizeof(*a) * (maf_mafBlock_getNumberOfSequences(mb) + 1));
    mafLine_t *ml = maf_mafBlock_getHeadLine(mb);
    unsigned i = 0;
    while (ml != NULL) {
        if (ml->type == 's')
            a[i++] = ml->strand;
        ml = ml->next;
    }
    a[i] = '\0';
    return a;
}
mafLine_t** maf_mafBlock_getMafLineArray_seqOnly(mafBlock_t *mb) {
    // currently this is not stored and must be built
    // should return an array of mafLine_t pointers to all sequences in mb
    if (maf_mafBlock_getNumberOfSequences(mb) == 0) {
        return NULL;
    }
    mafLine_t **a = (mafLine_t**) de_malloc(sizeof(*a) * (maf_mafBlock_getNumberOfSequences(mb)));
    mafLine_t *ml = maf_mafBlock_getHeadLine(mb);
    unsigned i = 0;
    while (ml != NULL) {
        if (ml->type == 's')
            a[i++] = ml;
        ml = ml->next;
    }
    return a;
}
int* maf_mafBlock_getStrandIntArray(mafBlock_t *mb) {
    // currently this is not stored and must be built
    // should return an int array containing an in-order list of strandedness
    // for all sequence lines. Either 1 or -1
    int *a = (int*) de_malloc(sizeof(*a) * maf_mafBlock_getNumberOfSequences(mb));
    mafLine_t *ml = maf_mafBlock_getHeadLine(mb);
    unsigned i = 0;
    while (ml != NULL) {
        if (ml->type == 's') {
            if (ml->strand == '+') {
                a[i++] = 1;
            } else {
                a[i++] = -1;
            }
        }
        ml = ml->next;
    }
    return a;
}
uint32_t* maf_mafBlock_getStartArray(mafBlock_t *mb) {
    // currently this is not stored and must be built
    // should return a uint32_t array containing an in-order list of source lengths 
    // for all sequence lines. 
    uint32_t *a = (uint32_t*) de_malloc(sizeof(*a) * maf_mafBlock_getNumberOfSequences(mb));
    mafLine_t *ml = maf_mafBlock_getHeadLine(mb);
    unsigned i = 0;
    while (ml != NULL) {
        if (ml->type == 's') {
            a[i++] = ml->start;
        }
        ml = ml->next;
    }
    return a;
}
uint32_t* maf_mafBlock_getPosCoordStartArray(mafBlock_t *mb) {
    // currently this is not stored and must be built
    // should return a uint32_t array containing an in-order list of the start position in
    // positive coordinates.
    uint32_t *a = (uint32_t*) de_malloc(sizeof(*a) * maf_mafBlock_getNumberOfSequences(mb));
    mafLine_t *ml = maf_mafBlock_getHeadLine(mb);
    unsigned i = 0;
    while (ml != NULL) {
        if (ml->type == 's') {
            if (ml->strand == '+')
                a[i++] = ml->start;
            else
                a[i++] = ml->sourceLength - ml->start - 1;
        }
        ml = ml->next;
    }
    return a;
}
uint32_t* maf_mafBlock_getPosCoordLeftArray(mafBlock_t *mb) {
    // currently this is not stored and must be built
    // should return a uint32_t array containing an in-order list of the left-most positon 
    // of the block in positive coordinates.
    uint32_t *a = (uint32_t*) de_malloc(sizeof(*a) * maf_mafBlock_getNumberOfSequences(mb));
    mafLine_t *ml = maf_mafBlock_getHeadLine(mb);
    unsigned i = 0;
    while (ml != NULL) {
        if (ml->type == 's') {
            if (ml->strand == '+')
                a[i++] = ml->start;
            else
                a[i++] = ml->sourceLength - (ml->start + ml->length);
        }
        ml = ml->next;
    }
    return a;
}
uint32_t* maf_mafBlock_getSourceLengthArray(mafBlock_t *mb) {
    // currently this is not stored and must be built
    // should return a uint32_t array containing an in-order list of positive 
    // coordinate start positions for all sequence lines. 
    uint32_t *a = (uint32_t*) de_malloc(sizeof(*a) * maf_mafBlock_getNumberOfSequences(mb));
    mafLine_t *ml = maf_mafBlock_getHeadLine(mb);
    unsigned i = 0;
    while (ml != NULL) {
        if (ml->type == 's') {
            a[i++] = ml->sourceLength;
        }
        ml = ml->next;
    }
    return a;
}
uint32_t* maf_mafBlock_getSequenceLengthArray(mafBlock_t *mb) {
    // currently this is not stored and must be built
    // should return a uint32_t array containing an in-order list of 
    // sequence length field values
    uint32_t *a = (uint32_t*) de_malloc(sizeof(*a) * maf_mafBlock_getNumberOfSequences(mb));
    mafLine_t *ml = maf_mafBlock_getHeadLine(mb);
    unsigned i = 0;
    while (ml != NULL) {
        if (ml->type == 's') {
            a[i++] = ml->length;
        }
        ml = ml->next;
    }
    return a;
}
char** maf_mafBlock_getSpeciesArray(mafBlock_t *mb) {
    // currently this is not stored and must be built
    // should return an array of char pointers containing an in-order list of 
    // sequence name fields for all sequences.
    char** m = NULL;
    m = (char**) de_malloc(sizeof(char*) * maf_mafBlock_getNumberOfSequences(mb));
    mafLine_t *ml = maf_mafBlock_getHeadLine(mb);
    unsigned i = 0;
    while (ml != NULL) {
        if (ml->type == 's')
            m[i++] = de_strdup(ml->species);
        ml = ml->next;
    }
    return m;
}
char* maf_mafLine_getLine(mafLine_t *ml) {
    return ml->line;
}
uint32_t maf_mafLine_getLineNumber(mafLine_t *ml) {
    return ml->lineNumber;
}
char maf_mafLine_getType(mafLine_t *ml) {
    return ml->type;
}
char* maf_mafLine_getSpecies(mafLine_t *ml) {
    return ml->species;
}
uint32_t maf_mafLine_getStart(mafLine_t *ml) {
    return ml->start;
}
uint32_t maf_mafLine_getLength(mafLine_t *ml) {
    return ml->length;
}
char maf_mafLine_getStrand(mafLine_t *ml) {
    return ml->strand;
}
uint32_t maf_mafLine_getSourceLength(mafLine_t *ml) {
    return ml->sourceLength;
}
char* maf_mafLine_getSequence(mafLine_t *ml) {
    return ml->sequence;
}
mafLine_t* maf_mafLine_getNext(mafLine_t *ml) {
    return ml->next;
}
unsigned maf_mafBlock_getNumberOfBlocks(mafBlock_t *b) {
    unsigned n = 0;
    while (b != NULL) {
        ++n;
        b = b->next;
    }
    return n;
}
unsigned umax(unsigned a, unsigned b) {
    return (a > b ? a : b);
}
unsigned maf_mafBlock_longestSequenceField(mafBlock_t *b) {
    // walk through the mafLines and find the longest sequence field.
    unsigned m = 0;
    mafLine_t *ml = maf_mafBlock_getHeadLine(b);
    if (maf_mafLine_getType(ml) == 's') {
        m = umax(m, strlen(maf_mafLine_getSequence(ml)));
    }
    while ((ml = maf_mafLine_getNext(ml)) != NULL) {
        if (maf_mafLine_getType(ml) == 's')
            m = umax(m, strlen(maf_mafLine_getSequence(ml)));
    }
    return m;
}
bool maf_mafBlock_containsSequence(mafBlock_t *mb) {
    if (mb->numberOfSequences > 0) {
        return true;
    } else {
        return false;
    }
}
uint32_t maf_mafBlock_getNumberOfLines(mafBlock_t *mb) {
    // count the number of mafLine_t lines in a headLine list
    return mb->numberOfLines;
}
uint32_t maf_mafLine_getNumberOfSequences(mafLine_t *ml) {
    // count the number of actual sequence lines in a mafLine_t list
    uint32_t s = 0;
    while (ml != NULL) {
        if (ml->type == 's')
            ++s;
        ml = ml->next;
    }
    return s;
}
uint32_t maf_mafLine_getPositiveCoord(mafLine_t *ml) {
    // return the start field coordinate in postive zero based coordinates.
    // NOTE THAT FOR - STRANDS, THIS COORDINATE WILL BE THE RIGHT-MOST (END POINT)
    // OF THE SEQUENCE. TO GET THE LEFT-MOST (START POINT) YOU WOULD NEED TO SUBTRACT
    // 
    if (ml->strand == '+') {
        return ml->start;
    } else {
        return ml->sourceLength - (ml->start + 1);
    }
}
uint32_t maf_mafLine_getPositiveLeftCoord(mafLine_t *ml) {
    // return the left most coordinate in postive zero based coordinates.
    // for - strands this includes the length of the sequence.
    if (ml->strand == '+') {
        return ml->start;
    } else {
        return ml->sourceLength - (ml->start + ml->length);
    }
}
void maf_mafBlock_setHeadLine(mafBlock_t *mb, mafLine_t *ml) {
    mb->headLine = ml;
}
void maf_mafBlock_setTailLine(mafBlock_t *mb, mafLine_t *ml) {
    mb->tailLine = ml;
}
void maf_mafBlock_setNumberOfSequences(mafBlock_t *mb, uint32_t n) {
    mb->numberOfSequences = n;
}
void maf_mafBlock_setNumberOfLines(mafBlock_t *mb, uint32_t n) {
    mb->numberOfLines = n;
}
void maf_mafBlock_setLineNumber(mafBlock_t *mb, uint32_t n) {
    mb->lineNumber = n;
}
void maf_mafLine_setType(mafLine_t *ml, char c) {
    ml->type = c;
}
void maf_mafLine_setSpecies(mafLine_t *ml, char *s) {
    ml->species = s;
}
void maf_mafLine_setStrand(mafLine_t *ml, char c) {
    ml->strand = c;
}
void maf_mafLine_setStart(mafLine_t *ml, uint32_t n) {
    ml->start = n;
}
void maf_mafLine_setLength(mafLine_t *ml, uint32_t n) {
    ml->length = n;
}
void maf_mafLine_setSourceLength(mafLine_t *ml, uint32_t n) {
    ml->sourceLength = n;
}
void maf_mafLine_setSequence(mafLine_t *ml, char *s) {
    ml->sequence = s;
}
void maf_mafLine_setNext(mafLine_t *ml, mafLine_t *next) {
    ml->next = next;
}
mafBlock_t* maf_readBlockHeader(mafFileApi_t *mfa) {
    extern const int kMaxStringLength;
    int32_t n = kMaxStringLength;
    char *line = (char*) de_malloc(n);
    mafBlock_t *header = maf_newMafBlock();
    int status = de_getline(&line, &n, mfa->mfp);
    bool validHeader = false;
    ++(mfa->lineNumber);
    maf_checkForPrematureMafEnd(status, line);
    if (strncmp(line, "track", 5) == 0) {
        // possible first line of a maf
        validHeader = true;
        mafLine_t *ml = maf_newMafLine();
        char *copy = (char *) de_malloc(n + 1); // freed in destroy lines
        strcpy(copy, line);
        ml->line = copy;
        ml->type = 'h';
        ml->lineNumber = mfa->lineNumber;
        header->headLine = ml;
        header->tailLine = ml;
        status = de_getline(&line, &n, mfa->mfp);
        ++(mfa->lineNumber);
        header->lineNumber = mfa->lineNumber;
        ++(header->numberOfLines);
        maf_checkForPrematureMafEnd(status, line);
    }
    if (strncmp(line, "##maf", 5) == 0) {
        // possible first or second line of maf
        validHeader = true;
        mafLine_t *ml = maf_newMafLine();
        char *copy = (char *) de_malloc(n + 1); // freed in destroy lines
        strcpy(copy, line);
        ml->line = copy;
        ml->type = 'h';
        ml->lineNumber = mfa->lineNumber;
        if (header->headLine == NULL) {
            header->headLine = ml;
            header->tailLine = ml;
        } else {
            header->headLine->next = ml;
            header->tailLine = ml;
        }
        status = de_getline(&line, &n, mfa->mfp);
        ++(mfa->lineNumber);
        header->lineNumber = mfa->lineNumber;
        ++(header->numberOfLines);
        maf_checkForPrematureMafEnd(status, line);
    }
    if (!validHeader) {
        fprintf(stderr, "Error, maf file %s does not contain a valid header!\n", mfa->filename);
        exit(EXIT_FAILURE);
    }
    mafLine_t *thisMl = header->tailLine;
    while(line[0] != 'a' && !maf_isBlankLine(line)) {
        // eat up the file until we hit the first alignment block
        mafLine_t *ml = maf_newMafLine();
        char *copy = (char *) de_malloc(n + 1); // freed in destroy lines
        strcpy(copy, line);
        ml->line = copy;
        ml->type = 'h';
        ml->lineNumber = mfa->lineNumber;
        thisMl->next = ml;
        thisMl = ml;
        header->tailLine = thisMl;
        status = de_getline(&line, &n, mfa->mfp);
        ++(mfa->lineNumber);
        header->lineNumber = mfa->lineNumber;
        ++(header->numberOfLines);
        maf_checkForPrematureMafEnd(status, line);
    }
    if (line[0] == 'a') {
        // stuff this line in ->lastLine for processesing
        char *copy = (char *) de_malloc(n + 1); // freed in destroy lines
        strcpy(copy, line);
        mfa->lastLine = copy;
    }
    free(line);
    return header;
}
mafBlock_t* maf_readBlockBody(mafFileApi_t *mfa) {
    extern const int kMaxStringLength;
    mafBlock_t *thisBlock = maf_newMafBlock();
    if (mfa->lastLine != NULL) {
        // this is only invoked when the header is not followed by a blank line
        mafLine_t *ml = maf_newMafLineFromString(mfa->lastLine, mfa->lineNumber);
        if (ml->type == 's') {
            ++(thisBlock->numberOfSequences);
        }
        ++(thisBlock->numberOfLines);
        thisBlock->headLine = ml;
        thisBlock->tailLine = ml;
        free(mfa->lastLine);
        mfa->lastLine = NULL;
    }
    int32_t n = kMaxStringLength;
    char *line = (char*) de_malloc(n);
    thisBlock->lineNumber = mfa->lineNumber;
    while(de_getline(&line, &n, mfa->mfp) != -1) {
        ++(mfa->lineNumber);
        if (maf_isBlankLine(line)) {
            if (thisBlock->headLine == NULL) {
                // this handles multiple blank lines in a row
                continue;
            } else {
                break;
            }
        }
        mafLine_t *ml = maf_newMafLineFromString(line, mfa->lineNumber);
        if (thisBlock->headLine == NULL) {
            thisBlock->headLine = ml;
            thisBlock->tailLine = ml;
        } else {
            thisBlock->tailLine->next = ml;
            thisBlock->tailLine = ml;
        }
        if (ml->type == 's') {
            ++(thisBlock->numberOfSequences);
        }
        ++(thisBlock->numberOfLines);
    }
    free(line);
    return thisBlock;
}
mafBlock_t* maf_readBlock(mafFileApi_t *mfa) {
    // either returns a pointer to the next mafBlock in the maf file,
    // or a NULL pointer if the end of the file has been reached.
    if (mfa->lineNumber == 0) {
        // header
        mafBlock_t *header = maf_readBlockHeader(mfa);
        if (header->headLine != NULL) {
            return header;
        } else {
            maf_destroyMafBlockList(header);
            return NULL;
        }
    } else {
        // body
        mafBlock_t *mb = maf_readBlockBody(mfa);
        if (mb->headLine != NULL) {
            return mb;
        } else {
            maf_destroyMafBlockList(mb);
            return NULL;
        }
    }
}
mafBlock_t* maf_readAll(mafFileApi_t *mfa) {
    // read an entire mfa, creating a linked list of mafBlock_t, returning the head.
    mafBlock_t *head = maf_readBlock(mfa);
    mafBlock_t *mb = head;
    mafBlock_t *tmp = NULL;
    while ((tmp = maf_readBlock(mfa)) != NULL) {
        mb->next = tmp;
        mb = tmp;
    }
    return head;
}
void maf_writeAll(mafFileApi_t *mfa, mafBlock_t *mb) {
    // write an entire mfa, creating a linked list of mafBlock_t, returning the head.
    while (mb != NULL) {
        maf_writeBlock(mfa, mb);
        mb = mb->next;
    }
    fprintf(mfa->mfp, "\n");
    ++(mfa->lineNumber);
    fclose(mfa->mfp);
    mfa->mfp = NULL;
}
void maf_writeBlock(mafFileApi_t *mfa, mafBlock_t *mb) {
    mafLine_t *ml = mb->headLine;
    while (ml != NULL) {
        fprintf(mfa->mfp, "%s\n", ml->line);
        ++(mfa->lineNumber);
        ml = ml->next;
    }
    fprintf(mfa->mfp, "\n");
    ++(mfa->lineNumber);
}
void maf_mafBlock_print(mafBlock_t *m) {
    mafLine_t* ml = maf_mafBlock_getHeadLine(m);
    while (ml != NULL) {
        printf("%s\n", maf_mafLine_getLine(ml));
        ml = maf_mafLine_getNext(ml);
    }
    printf("\n");
}