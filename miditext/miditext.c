#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdint.h>
#include <string.h>

uint8_t *FindStringInBuf(uint8_t *buf, uint16_t len, const uint8_t *s, uint16_t slen);

int main(int argc, char *argv[]) {
	FILE *infile;//, *outfile;
	char infilename[512];//, outfilename[512];
	int filesize;
	char *inbuf;
	char *p;
	uint32_t offset;
	unsigned int EntryLen, EntryLenNew, fieldlen;

	if (argc != 4) {
		printf("Usage: filename miditext <keyphrase> <new entry>\n");
		return -1;
	}

	strncpy(infilename, argv[1], 512);
//	strncpy(outfilename, argv[1], 512);
//	p = strstr(outfilename, ".mid");
//	if (p == NULL) {
//		printf("Is this a midi file?\n");
//		return -1;
//	}
//	strcpy(p, "1.mid");

	infile = fopen(infilename, "rb+");
	if (infile == NULL) {
		printf("Couldn't open %s.\n", infilename);
		return -1;
	}
	fseek (infile, 0, SEEK_END);
	filesize = ftell(infile);
//	printf("Filesize is %d\n", filesize);

	inbuf = malloc(filesize+1024);
	if (inbuf == NULL) {
		printf("Couldn't malloc input buffer.\n");
		return -1;
	}

	EntryLenNew = strlen(argv[3]);
	fseek(infile, 0, SEEK_SET);
	fread(inbuf, sizeof(char), filesize, infile);
	p = (char *)FindStringInBuf((uint8_t *)inbuf, filesize, (uint8_t *)argv[2], strlen(argv[2]));
	if (p == NULL) {
		printf("Keyphrase not found.\n");
		return -2;
	} else {
		if ((*(p-4) != 0x00) || (*(p-3) != -1) || (*(p-2) != 0x01)) {
			printf("Bad file contents: 0x%02x 0x%02x 0x%02x\n", *(p-4), *(p-3), *(p-2));
		} else {
			EntryLen = (unsigned int)*(p-1) - strlen(argv[2]);
			if (EntryLen == EntryLenNew) {
				memcpy(p+strlen(argv[2]), argv[3], EntryLenNew);
			} else {
				memmove(p+EntryLenNew+strlen(argv[2]),
						p+EntryLen+strlen(argv[2]),
						filesize-(int)(p+EntryLen-inbuf));
				memcpy(p+strlen(argv[2]), argv[3], EntryLenNew);
				filesize = filesize + EntryLenNew - EntryLen;
				fieldlen = (unsigned int) *(p-1);
				*(p-1) = fieldlen + EntryLenNew - EntryLen;
				memcpy((char *)&offset, inbuf+18, 4);
				offset = __builtin_bswap32(__builtin_bswap32(offset) + EntryLenNew - EntryLen);
				memcpy(inbuf+18, (char *)&offset, 4);
			}
		}

//		outfile = fopen(outfilename, "wb+");
		fseek(infile, 0, SEEK_SET);
		fwrite(inbuf, sizeof(char), filesize, infile);
		fclose(infile);
//		fclose(outfile);
	}

    return 0;
}

/**
 * \fn uint8_t *FindStringInBuf(uint8_t *buf, uint16_t len, const uint8_t *s, uint16_t slen)
 *
 * \brief	Tries to find substring s, of length slen, within
 *			string buf of length len. Both strings may have NULs in them.
 *
 * \param buf Incoming buffer
 * \param len Length of buf
 * \param s Substring to be searched for
 * \param slen Length of s
 *
 * \return Pointer to beginning of match if found, NULL pointer otherwise.
 */
uint8_t *FindStringInBuf(uint8_t *buf, uint16_t len, const uint8_t *s, uint16_t slen) {
	uint16_t i, j;
	uint16_t imax = len - slen - 1;
	uint8_t *ret = NULL;
	uint8_t  match;

	for (i=0; i<imax; i++) {
		match = 1;
		for (j=0; j<slen; j++) {
			if (buf[i+j] != s[j]) {
				match = 0;
				break;
			}
		}
		if (match) {
			ret = buf+i;
			break;
		}
	}
	return ret;
}
