#ifndef LZH_H
#define LZH_H

#include <cstdint>

#include "types.h"

//#pragma warning(disable: 4200)			// zero-sized array warning

#pragma pack(push, 1)
typedef struct
{
    uchar	headerSize;
    uchar	headerSum;

    uchar	method[5];
    ulong	compressedSize;
    ulong	originalSize;
    ushort	_unknown;
    ushort	fileType;
    uchar	_0x20;
    uchar	_0x01;
    uchar	filenameLen;
    uchar	filename[0];
} lzhHeader;

typedef struct
{
    ushort	crc;
    uchar	_0x20;
    ushort	extendedHeaderSize;
    uchar	extendedHeader[0];
} lzhHeaderAfterFilename;
#pragma pack(pop)

typedef enum
{
    LZHERR_OK = 0,
    LZHERR_UNKNOWN_METHOD,
    LZHERR_EXTHEADER_EXISTS,
} lzhErr;


void	lzhInit(void);
uchar	lzhCalcSum(uchar *ptr, ulong len);
lzhErr	lzhCompress(void *fname, ulong fnamelen, void *inbuf, ulong inbufsize, void *outbuf, ulong outbufsize, ulong *usedsize);
lzhErr	lzhExpand(lzhHeader *lzhptr, void *outbuf, ulong outbufsize, ushort *crc);

#endif
