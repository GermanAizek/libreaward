#include <cstdio>
#include <cstring>
#include "lzh.h"
#include "lzhEngine/lzhEngine.h"

static uchar buffer[DICSIZ];

void lzhInit(void)
{
    make_crctable();
}

uchar lzhCalcSum(uchar *ptr, ulong len)
{
    uchar val = 0;

    while (len--)
    {
        val = (uchar) (val + *ptr);
        ptr++;
    }

    return val;
}

lzhErr lzhCompress(void *fname, ulong fnamelen, void *inbuf, ulong inbufsize, void *outbuf, ulong outbufsize, ulong *usedsize)
{
    lzhHeader *lzhhdr = (lzhHeader *)outbuf;
    lzhHeaderAfterFilename *lzhhdra;
    uchar *dataptr;

    memset(lzhhdr, 0, sizeof(lzhHeader));
    lzhhdr->filenameLen = (uchar)fnamelen;
    lzhhdr->headerSize  = 25 + lzhhdr->filenameLen;
    memcpy(lzhhdr->method, "-lh5-", 5);
    memcpy(lzhhdr->filename, fname, lzhhdr->filenameLen);
    lzhhdr->originalSize = inbufsize;
    lzhhdr->_0x01 = 0x01;
    lzhhdr->_0x20 = 0x20;

    lzhhdra = (lzhHeaderAfterFilename *) ((lzhhdr->filename) + lzhhdr->filenameLen);
    memset(lzhhdra, 0, sizeof(lzhHeaderAfterFilename));
    lzhhdra->_0x20 = 0x20;

    dataptr = (uchar *) ((lzhhdra->extendedHeader) + lzhhdra->extendedHeaderSize);
    ioInit(inbuf, inbufsize, dataptr, outbufsize);

    encode();
    if (unpackable) {
        /*
        header[3] = '0';  // store
        rewind(infile);
        fseek(outfile, arcpos, SEEK_SET);
        store();
*/
    }

    lzhhdr->compressedSize = ioGetOutSizeUsed();
    lzhhdra->crc = ioGetCRC();

    *usedsize = (ulong) ((dataptr + lzhhdr->compressedSize) - (uchar *)outbuf);

    //	r = ratio(compsize, origsize);
    //	printf(" %d.%d%%\n", r / 10, r % 10);

    return LZHERR_OK;
}

lzhErr lzhExpand(lzhHeader *lzhptr, void *outbuf, ulong outbufsize, ushort *retcrc)
{
    lzhHeaderAfterFilename *lzhptra;
    int n, method;
    ushort ext_headersize;
    uchar header[5], *dataptr;
    ulong origsize, compsize;

    lzhptra = (lzhHeaderAfterFilename *) ((lzhptr->filename) + lzhptr->filenameLen);
    origsize = lzhptr->originalSize;
    compsize = lzhptr->compressedSize;

    dataptr = (uchar *) ((lzhptra->extendedHeader) + lzhptra->extendedHeaderSize);

    ioInit(dataptr, compsize, outbuf, outbufsize);

    memcpy(header, lzhptr->method, 5);
    method = header[3]; header[3] = ' ';
    if (! strchr("045", method) || memcmp("-lh -", header, 5)) {
        return LZHERR_UNKNOWN_METHOD;
    } else {
        ext_headersize = lzhptra->extendedHeaderSize;
        while (ext_headersize != 0) {
            return LZHERR_EXTHEADER_EXISTS;
            /*
            fprintf(stderr, "There's an extended header of size %u.\n",
                ext_headersize);
            compsize -= ext_headersize;
            if (fseek(arcfile, ext_headersize - 2, SEEK_CUR))
                error("Can't read");
            ext_headersize = fgetc(arcfile);
            ext_headersize += (ushort)fgetc(arcfile) << 8;
*/
        }

        if (method != '0')
            decode_start();

        while (origsize != 0)
        {
            n = (ushort)((origsize > DICSIZ) ? DICSIZ : origsize);

            if (method != '0')
                decode(n, buffer);
            else
            {
                memcpy(buffer, dataptr, n);
                dataptr += n;
            }

            memwrite_crc(buffer, n);
            origsize -= n;
        }
    }

    *retcrc = ioGetCRC();
    return LZHERR_OK;
}
