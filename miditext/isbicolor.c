#include <stdio.h>
#include <stdint.h>

int main(int argc, char *argv[]) {
	FILE *infile;
	uint16_t statusword;

	if (argc != 2) {
		printf("Usage: isbicolor filename\n");
		return -1;
	}

	infile = fopen(argv[1], "rb");
	if (infile == NULL) {
		printf("Can't open %s\n", argv[1]);
		return -1;
	}
	fseek(infile, 34, SEEK_SET);
	fread((char *)&statusword, sizeof(uint16_t), 1, infile);
	if (statusword & 0x0040) {
		printf("1");
	} else {
		printf("0");
	}
	return 0;
}
