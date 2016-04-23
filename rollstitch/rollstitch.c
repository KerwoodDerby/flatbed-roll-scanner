#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "tiffio.h"


struct pixel_t {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t alpha;
};

struct image_vars_t {
	TIFF *handle;
	uint32_t *buffer;
	 int32_t width;
	 int32_t height;
	 int32_t res;
};

uint32_t SumSquares(struct image_vars_t *i1,
                    struct image_vars_t *i2,
					  int32_t xoff,
					  int32_t yoff,
					  int32_t nx,
					  int32_t ny);

int EncodeBufferToCIS(struct image_vars_t *im, int32_t xoff1,  int32_t xoff2, int32_t yoff, int nlines, int BiColor, FILE *outfile);

//
//	This is the header of the CIS file. Quantities within are little-endian.
//
//	The StatusFlags value is broken down as follows:
//	  15  14  13  12  11  10  9   8   7   6   5   4   3   2   1   0
//	+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
//	|   |   |   |   |   |   |   |   |   |   |   |   |               |
//	+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
//	          |   |                   |   |   |   |   <------------> Scanner type code (0-15)
//	          |   |                   |   |   |   +----------------> Speed doubling applied
//	          |   |                   |   |   +--------------------> Twin array in use
//	          |   |                   |   +------------------------> File is bi-color
//	          |   |                   +----------------------------> Division
//	          |   +------------------------------------------------> Horizontal (array axis) is reversed
//	          +----------------------------------------------------> Vertical (roll motion) is reversed
//
struct cis_header_s {
	char Description[32];			// Description of digitized work
	uint16_t Spare1;
	uint16_t StatusFlags;			// Flags indicating reversals, etc.
	uint16_t TwinArraySep;			// Twin array vertical separation
	uint16_t ArrayDPI;				// Array sensor pixels/inch
	uint16_t ScannerWidth;			// # of pixels in scan line
	uint16_t TwinArrayChangeover;	// ???
	uint16_t Tempo;				// Tempo marking on roll
	uint16_t VerticalDPI;			// Lines per inch resolution along the roll
	uint32_t LineCount;			// # of lines in this file
} cis_header;

#define STATFLAG_TYPECODE_MASK  0x000f
#define STATFLAG_SPEED_DOUBLED  0x0010
#define STATFLAG_TWIN_ARRAY     0x0020
#define STATFLAG_BICOLOR        0x0040
#define STATFLAG_DIVISION       0x0080
#define STATFLAG_HORIZ_REVERSED 0x1000
#define STATFLAG_VERT_REVERSED  0x2000

#define XMARGIN 800
#define COARSESTEP 5/*20*/
int NumRowsSkipped = 0;
//FILE *binfile;
float thres_trim = 0.;

int main(int argc, char* argv[]) {
	struct image_vars_t Image1, Image2;
	size_t npixels;
	int i;
	int retval;
	int ciswidth, cisheight;
	uint32_t sum, minsum, IndexOfMinY, IndexOfMinYCoarse, IndexOfMinX;
	uint32_t r = 0, RowsWritten = 0;
	FILE *cisfile;
	char TiffFileRoot[256];
	char TiffFilename[256];
	char cisfilename[256];
	char Title[33];
	int FileNumber = 1;
	int NotDone = 1;
	FILE *testfile;
	int PreviousXOffset = 0;
	int BiColorOption = 0;
	int tempo = 70;

	if (argc != 6) {
		printf("Usage: rollstitch bicolor thresh_trim tempo filenameroot title\n");
		return -1;
	}
	BiColorOption = atoi(argv[1]);
	thres_trim = (float)atoi(argv[2]);
	tempo = atoi(argv[3]);
	strncpy(TiffFileRoot, argv[4], 256);
	memset(Title, 0, 33);
	strncpy(Title, argv[5], 33);

	snprintf(TiffFilename, 256, "%s-%04d.tif", TiffFileRoot, FileNumber++);
	Image1.buffer = NULL;
    Image1.handle = TIFFOpen(TiffFilename, "r");
    if (Image1.handle == 0) {
		printf("Problem opening file %s.\n", TiffFilename);
		return -1;
    }
	snprintf(TiffFilename, 256, "%s-%04d.tif", TiffFileRoot, FileNumber++);
	Image2.buffer = NULL;
    Image2.handle = TIFFOpen(TiffFilename, "r");
    if (Image2.handle == 0) {
		printf("Problem opening file %s.\n", TiffFilename);
		return -1;
    }

    TIFFGetField(Image1.handle, TIFFTAG_IMAGEWIDTH,  &ciswidth);
    TIFFGetField(Image1.handle, TIFFTAG_IMAGELENGTH,  &cisheight);
    cis_header.LineCount = 0;
	cis_header.ScannerWidth = ciswidth + XMARGIN;
	cis_header.VerticalDPI =  cisheight * 10 / 85;
	cis_header.ArrayDPI = cisheight * 10 / 85;
	if (BiColorOption & 1)
		cis_header.StatusFlags = 4 + STATFLAG_BICOLOR + STATFLAG_HORIZ_REVERSED;
	else
		cis_header.StatusFlags = 4 + STATFLAG_HORIZ_REVERSED;

	cis_header.Tempo = tempo;
//	memcpy(&cis_header.Description, "Test File 01234567890123456789012", 32);
	memcpy(cis_header.Description, Title, 32);
	NumRowsSkipped = cis_header.VerticalDPI / 5;

	snprintf(cisfilename, 256, "%s.cis", TiffFileRoot);
	cisfile = fopen (cisfilename, "wb+");
	fwrite((void *)&cis_header, sizeof(struct cis_header_s), 1, cisfile);

    TIFFGetField(Image1.handle, TIFFTAG_IMAGEWIDTH,  &Image1.width);
    TIFFGetField(Image1.handle, TIFFTAG_IMAGELENGTH, &Image1.height);
    Image1.res = Image1.height * 10 / 85;
    npixels = Image1.width * Image1.height;
    Image1.buffer = (uint32 *)_TIFFmalloc(npixels * sizeof (uint32));
    if (Image1.buffer == NULL) return -2;
	retval = TIFFReadRGBAImage(Image1.handle, Image1.width, Image1.height, Image1.buffer, 0);
	if (retval == 0) return -4;

//binfile = fopen("mjj.gray", "wb+");

    while (NotDone) {

		TIFFGetField(Image2.handle, TIFFTAG_IMAGEWIDTH,  &Image2.width);
		TIFFGetField(Image2.handle, TIFFTAG_IMAGELENGTH, &Image2.height);
		Image2.res = Image2.height * 10 / 85;
		npixels = Image2.width * Image2.height;
		Image2.buffer = (uint32 *)_TIFFmalloc(npixels * sizeof (uint32));
		if (Image2.buffer == NULL) return -3;

		retval = TIFFReadRGBAImage(Image2.handle, Image2.width, Image2.height, Image2.buffer, 0);
		if (retval == 0) return -5;

		minsum = 0xffffffff;
		IndexOfMinYCoarse = 0;
//		for (i=35*Image1.res/10; i<(5*Image1.res); i+=COARSESTEP) {
		for (i=38*Image1.res/10; i<(5*Image1.res); i+=COARSESTEP) {
//		for (i=35*Image1.res/10; i<(4.5*Image1.res); i+=COARSESTEP) {
//		for (i=3*Image1.res; i<(45*Image1.res/10); i+=COARSESTEP) {
//		for (i=30*Image1.res/10; i<(6*Image1.res); i+=COARSESTEP) {
			sum = SumSquares(&Image1, &Image2, 0, i, Image1.width, 400);
			if (minsum > sum) {
				minsum = sum;
				IndexOfMinYCoarse = i;
			}
		}
		minsum = 0xffffffff;
		IndexOfMinY = 0;
		for (i=(IndexOfMinYCoarse-COARSESTEP); i<(IndexOfMinYCoarse+COARSESTEP); i++) {
			sum = SumSquares(&Image1, &Image2, 0, i, Image1.width, 400);
			if (minsum > sum) {
				minsum = sum;
				IndexOfMinY = i;
			}
		}
		minsum = 0xffffffff;
		IndexOfMinX = 0;
		for (i=-20; i<20; i++) {
//		for (i=-80; i<80; i++) {
			sum = SumSquares(&Image1, &Image2, i, IndexOfMinY, Image1.width-40, 400);
			if (minsum > sum) {
				minsum = sum;
				IndexOfMinX = i;
			}
		}
		printf("%d: y index = %d, x index = %d\n", FileNumber-1, IndexOfMinY, IndexOfMinX);
		//
		//  Check to see if the next file exists
		//
        snprintf(TiffFilename, 256, "%s-%04d.tif", TiffFileRoot, FileNumber++);
        testfile = fopen (TiffFilename, "r");
        if (testfile == NULL) {
            NotDone = 0;
        } else {
            fclose(testfile);
        }
        //
        //  Write out the CIS for this pass
        //
        NumRowsSkipped = 0;
        if (NotDone) {
            r = EncodeBufferToCIS(&Image1, PreviousXOffset, IndexOfMinX, NumRowsSkipped*2, IndexOfMinY+NumRowsSkipped, BiColorOption, cisfile);
        } else {
            r = EncodeBufferToCIS(&Image1, PreviousXOffset, IndexOfMinX, NumRowsSkipped*2, IndexOfMinY+NumRowsSkipped, BiColorOption, cisfile);
            RowsWritten += r;
            r = EncodeBufferToCIS(&Image2, IndexOfMinX, IndexOfMinX, NumRowsSkipped*2, Image2.height-2*NumRowsSkipped, BiColorOption, cisfile);
        }
		RowsWritten += r;
		PreviousXOffset = IndexOfMinX;// + PreviousXOffset;
//		PreviousXOffset = PreviousXOffset - IndexOfMinX;
        //
        //  Copy all 2nd image info to the 1st image's variables
        //
        if (NotDone) {
            if (Image1.buffer != NULL) {
                _TIFFfree(Image1.buffer);
                Image1.buffer = NULL;
            }
            if (Image1.handle != 0) {
                TIFFClose(Image1.handle);
                Image1.handle = NULL;
            }
            Image1.buffer = Image2.buffer;
            Image1.handle = Image2.handle;
            memcpy((void *)&Image1, (void *)&Image2, sizeof(struct image_vars_t));
            Image2.buffer = NULL;
            Image2.handle = TIFFOpen(TiffFilename, "r");
        }
    }
    if (Image2.buffer != NULL) {
        _TIFFfree(Image2.buffer);
        Image2.buffer = NULL;
    }
    if (Image2.handle != 0) {
        TIFFClose(Image2.handle);
        Image2.handle = NULL;
    }

    cis_header.LineCount = RowsWritten;
    fseek(cisfile, 0, SEEK_SET);
    fwrite((void *)&cis_header, sizeof(struct cis_header_s), 1, cisfile);
    exit(0);
}

uint32_t SumSquares(struct image_vars_t *i1,
					 struct image_vars_t *i2,
					  int32_t xoff,
					  int32_t yoff,
					  int32_t nx,
					  int32_t ny) {
	struct pixel_t *px1, *px2;
	int ix, iy;
	uint64_t sum = 0;
	int sumsq;
	int xoff_pos = 0;
	int xoff_neg = 0;

	if (xoff > 0) xoff_pos = xoff;
	if (xoff < 0) xoff_neg = -xoff;

	for (iy=0; iy<ny; iy++) {
		px1 = (struct pixel_t *)&(i1->buffer[(yoff + iy)*i1->width + xoff_pos]);
		px2 = (struct pixel_t *)&(i2->buffer[(iy)*i2->width + xoff_neg]);
		for (ix=0; ix<nx; ix++) {
			sumsq = (int)(px1->red-px2->red)    *(int)(px1->red-px2->red) +
					(int)(px1->green-px2->green)*(int)(px1->green-px2->green) +
					(int)(px1->blue-px2->blue)  *(int)(px1->blue-px2->blue);
			sum += sumsq;
			px1++;
			px2++;
		}
	}
	return (uint32_t)(sum >> 16);
}

#define BGND_R 255
#define BGND_G 54
#define BGND_B 18
#define COL1 0
#define COL2 10 /*50*/
#define LUMIN_THRESH 100.
#define MAXRGB(R,G,B) (R > ((G>B) ? G : B) ? R : ((G>B) ? G : B))
#define MINRGB(R,G,B) (R < ((G<B) ? G : B) ? R : ((G<B) ? G : B))
#define VELLUM_MIN 40. /*60.*/
#define VELLUM_SAT 0.2 /*0.3*/

int EncodeBufferToCIS(struct image_vars_t *im, int32_t xoff1,  int32_t xoff2,  int32_t yoff, int nlines, int BiColor, FILE *outfile) {
	uint8_t r, g, b;
	double dr, dg, db, mag, col_dist;
	double ThresholdRadius = 0.25;//0.30;
	double AdaptThresh;
	double Hysteresis = 0.07;
	int CurrentState = 0;
	int PreviouState = 0;
	int CurrentMarkState = 0;
	int PreviousMarkState = 0;
	int LastTransitionCol = 0;
	int LastMarkCol = 0;
	int col;//, colmin, colmax;
	int16_t EdgeCol, MarkCol;
	int16_t zero = 0;
	int row, ix;
	int RowsEmitted = 0;
	uint16_t *punchlist, *marklist;
	int NumPunchEdges, NumMarkEdges;
	double R1, G1, B1, RGBMAG;
	double MinMag = 70.;//0.10;
	int debug_runtotal = 0;
	int i;
//uint8_t lum;
	uint8_t Bgnd_R, Bgnd_G, Bgnd_B;
	int Sum_R = 0, Sum_G = 0, Sum_B = 0, SumNum = 0;
	int VellumFlag;
	double sat = 0.;
	int maxcomp, mincomp;

    union {
        struct pixel_t s;
        uint32_t i;
    } px;
    uint32_t *bufi;
    int32_t xoff;
    double xoff_f;

//#define COLGUTTER 90 /* For normal rolls */
#define COLGUTTER 60 /* For Ampico rolls */
#define HIGHEXCLUDE 40/*100*/
//#define HIGHEXCLUDE 120 /* For Supertone sync punches */

	VellumFlag = (BiColor & 2) ? 1 : 0;
	BiColor = BiColor & 1;

	punchlist = malloc(im->width * sizeof(uint16_t));
	if (!punchlist) return -1;
	memset(&punchlist[0], 0, im->width * sizeof(uint16_t));
	if (BiColor) {
		marklist = malloc(im->width * sizeof(uint16_t));
		if (!marklist) return -1;
		memset(&marklist[0], 0, im->width * sizeof(uint16_t));
	}

	for (row=yoff; row<(yoff+nlines); row++) {
		ix = row * im->width;
		for (col=COL1; col<COL2; col++) {
            px.i = im->buffer[ix+col];
			Sum_R += px.s.red;
			Sum_G += px.s.green;
			Sum_B += px.s.blue;
			SumNum++;
		}
	}
	Bgnd_R = Sum_R/SumNum;
	Bgnd_G = Sum_G/SumNum;
	Bgnd_B = Sum_B/SumNum;
//printf("r/g/b = %d/%d/%d\n", Bgnd_R, Bgnd_G, Bgnd_B);

	RGBMAG = sqrt(Bgnd_R*Bgnd_R + Bgnd_G*Bgnd_G + Bgnd_B*Bgnd_B);
	R1 = Bgnd_R/RGBMAG;
	G1 = Bgnd_G/RGBMAG;
	B1 = Bgnd_B/RGBMAG;

	row = 0;

	xoff_f = ((double)xoff1 + 0.5);
	for (row=yoff; row<(yoff+nlines); row++) {
        ix = row * im->width;
		bufi = &im->buffer[ix+COLGUTTER];
		PreviouState = 0;
		PreviousMarkState = 0;
		debug_runtotal = 0;
		NumPunchEdges = 0;
		NumMarkEdges = 0;
		//
		//	Set up to do a horizontal scan line.
		//
		xoff = (int32_t)(floor(xoff_f) + 0.5);
		for (col=0; col<im->width-COLGUTTER; col++) {
			px.i = *bufi++;
			b = px.s.blue;
			g = px.s.green;
			r = px.s.red;
			mag = sqrt((double)r * (double)r +
					   (double)g * (double)g +
					   (double)b * (double)b);

			if (mag == 0.) mag = 1.;
			dr = (((double)r)/mag) - R1;
			dg = (((double)g)/mag) - G1;
			db = (((double)b)/mag) - B1;
			col_dist = sqrt(dr*dr + dg*dg + db*db);
			//
			//		First, handle the punched holes.
			//
			AdaptThresh = ThresholdRadius * (1. + Hysteresis*fabs((double)PreviouState-(double)CurrentState));
			if (col == 0) {
				LastTransitionCol = -XMARGIN/2 - xoff - COLGUTTER;
				if (BiColor) LastMarkCol = -XMARGIN/2 - xoff - COLGUTTER;
				CurrentState = 0;
				CurrentMarkState = 0;
			} else {
//				if ((mag >= MinMag) && (col < (im->width-COLGUTTER-HIGHEXCLUDE))) {
				if ((col < (im->width-COLGUTTER-HIGHEXCLUDE))) {
					if (VellumFlag) {
						CurrentState = ((mag < VELLUM_MIN) ? 1 : 0);
					} else {
						CurrentState = ((col_dist <= AdaptThresh) ? 1 : 0);
					}
					if (PreviouState != CurrentState) {
						EdgeCol = col - LastTransitionCol;
//						fwrite((void *)&EdgeCol, sizeof(int16_t), 1, outfile);
						punchlist[NumPunchEdges++] = EdgeCol;
						debug_runtotal += EdgeCol;
						LastTransitionCol = col;
					}
				}
				//
				//		Second, handle the paper markings.
				//
				if (BiColor) {
//					CurrentMarkState = (px.s.red > LUMIN_THRESH ? 1 : 0);
					if (VellumFlag) {
						maxcomp = MAXRGB(r,g,b);
						mincomp = MINRGB(r,g,b);
						sat = (double)(maxcomp - mincomp) / (double)maxcomp;
						CurrentMarkState = ((sat < VELLUM_SAT + (float)thres_trim/10.) ? 1 : 0);
					} else {
						CurrentMarkState = (mag > (RGBMAG - LUMIN_THRESH + thres_trim) ? 1 : 0);
					}
					if (CurrentMarkState != PreviousMarkState) {
						MarkCol = col - LastMarkCol;
						marklist[NumMarkEdges++] = MarkCol;
						LastMarkCol = col;
					}
				}
			}
			PreviouState = CurrentState;
			if (BiColor) PreviousMarkState = CurrentMarkState;
		}
		if (LastTransitionCol < ((im->width)-COLGUTTER)) {
			EdgeCol = im->width + XMARGIN/2 - LastTransitionCol - xoff - COLGUTTER;
			debug_runtotal += EdgeCol;
			punchlist[NumPunchEdges++] = EdgeCol;
		}
		if (BiColor) {
			if (LastMarkCol < ((im->width)-COLGUTTER)) {
				MarkCol = im->width + XMARGIN/2 - LastMarkCol - xoff - COLGUTTER;
				marklist[NumMarkEdges++] = MarkCol;
			}
		}
		punchlist[NumPunchEdges++] = 0;
		if (BiColor) marklist[NumMarkEdges++] = 0;
		fwrite(punchlist, sizeof(int16_t), NumPunchEdges, outfile);
		if (BiColor) {
			fwrite(marklist, sizeof(int16_t), NumMarkEdges, outfile);
		}
		RowsEmitted++;
		xoff_f += ((double)xoff2 - (double)xoff1) / (double)(nlines);
	}
	if (punchlist) free(punchlist);
	if (BiColor) {
		if (marklist) free(marklist);
	}
	return RowsEmitted;
}
