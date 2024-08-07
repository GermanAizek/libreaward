#include "bios.h"

#include <ctime>
#include <QFile>

typedef enum
{
    LAYOUT_UNKNOWN = 0,
    LAYOUT_1_1_1,
    LAYOUT_2_1_1,
    LAYOUT_2_2_2
} biosLayout;

typedef struct
{
    bool		 modified;				// has this image been modified?
    char		 fname[256];			// full path/name of image loaded
    biosLayout	 layout;				// type of layout of the file table

    uchar		*imageData;				// the loaded image
    ulong		 imageSize;				// size of the loaded image (in bytes)

    fileEntry	*fileTable;				// uncompressed data of all files
    int			 fileCount;				// number of files in the file table
    ulong		 tableOffset;			// offset of dynamic file table from start of image

    ulong		 maxTableSize;			// maximum compressed size allowed in the file table
} biosStruct;

typedef struct updateEntry
{
    char	*path;
    char	*fname;
    time_t	lastWrite;

    struct updateEntry	*next;
} updateEntry;

static updateEntry *updateList = NULL;
static uint updateTimerID = 0;
static bool updateIgnoreTimer = false;

void biosAddToUpdateList(char *fname);
//void biosClearUpdateList(void);

char exePath[256], fullTempPath[256];

static char *mainChangedText = "This BIOS image has been changed.  Do you want to save your changes before exiting?";

//static HINSTANCE hinst;
//static HWND hwnd, statusWnd, treeView, hPropDlgListWnd, hModDlgWnd;
//static HTREEITEM recgItem, inclItem, unkItem;
//static RECT *dlgrc;
static biosStruct biosdata;
static ulong curHash;
static ushort insertID;
static fileEntry *curFileEntry;
static bool ignoreNotify;
static char *biosChangedText = "This BIOS image has been changed.  Do you want to save your changes before loading a new one?";
static ulong biosFreeSpace;

// More info about hack: https://forum.qt.io/topic/66906/qt5-convert-qstring-into-char-or-char/11
char* QStrToCharArr(const QString& str)
{
    return str.toLatin1().data();
}

int SetWindowText(const QString& text)
{
    qDebug() << text;
}

int MessageBox(const QString& text, const QString& titleText, QMessageBox::StandardButtons standartButtons, QMessageBox::StandardButtons defaultButtons)
{
    QMessageBox msgBox;
    msgBox.setText(text);
    msgBox.setInformativeText("Notice");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Yes);
    return msgBox.exec();
}

void biosRemoveEntry(fileEntry *toRemove);


Bios::Bios() {}

/*
void biosUpdateCurrentDialog(void)
{
    //HWND hedit;
    char buf[257];
    ulong len, data;
    ushort data16;
    awdbeItem *item;

    // first, update the data from our shared controls
    if ((curFileEntry != NULL) && (hModDlgWnd != NULL))
    {
        hedit = GetDlgItem(hModDlgWnd, IDC_FILENAME);
        if (IsWindow(hedit))
        {
            GetWindowText(hedit, buf, 256);
            len = strlen(buf);

            // compare strings
            if ( (len != curFileEntry->nameLen) || memcmp(buf, curFileEntry->name, len) )
            {
                delete []curFileEntry->name;

                curFileEntry->nameLen	= len;
                curFileEntry->name		= new char[curFileEntry->nameLen + 1];
                strcpy_s(curFileEntry->name, sizeof(curFileEntry->name), buf);

                biosSetModified(TRUE);
            }
        }

        hedit = GetDlgItem(hModDlgWnd, IDC_FILE_ID);
        if (IsWindow(hedit))
        {

            GetWindowText(hedit, buf, 256);
            sscanf_s(buf, "%hX", &data16);

            // compare data
            if (data16 != curFileEntry->type)
            {
                curFileEntry->type = data16;
                biosSetModified(TRUE);

                // since we changed the type ID of a component, we need to rebuild our list
                biosUpdateComponents();
            }
        }

        hedit = GetDlgItem(hModDlgWnd, IDC_FILE_OFFSET);
        if (IsWindow(hedit))
        {
            GetWindowText(hedit, buf, 256);
            sscanf_s(buf, "%08X", &data);

            // compare data
            if (data != curFileEntry->offset)
            {
                curFileEntry->offset = data;
                biosSetModified(TRUE);
            }
        }
    }

    // next, call the plugin to update its own data
    if (curHash > HASH_UNKNOWN_ITEM_MAX)
    {
        switch (curHash)
        {
        case HASH_SUBMENU_ITEM:
        case HASH_RECOGNIZED_ROOT:
        case HASH_UNKNOWN_ROOT:
        case HASH_INCLUDABLE_ROOT:
            // do nothing
            break;

        default:
            // find the item that responds to this hash
            item = pluginFindHash(curHash);
            if (item == NULL)
            {
                snprintf(buf, sizeof(buf), "pluginFindHash() returned NULL for hash=%08Xh.  This hash value should be in the switch()...", curHash);
                MessageBox(hwnd, buf, "Internal Error", MB_OK);
                return;
            }

            // update only if the old item was *not* under the "includable" tree (curFileEntry will be NULL if under an includable item)
            if (curFileEntry != NULL)
            {
                // call this plugin's update function
                if (pluginCallUpdateDialog(item, curFileEntry, hModDlgWnd) == TRUE)
                    biosSetModified(TRUE);
            }
            break;
        }
    }
}
*/

fileEntry* Bios::biosScanForID(ushort id)
{
    fileEntry *fe = &biosdata.fileTable[0];
    ulong count = biosdata.fileCount;

    while (count--)
    {
        if (fe->type == id)
            return fe;

        fe++;
    }

    return NULL;
}

ulong biosWriteComponent(fileEntry* fe, FILE* fp, int fileIdx, bool compress)
{
    uchar* tempbuf = NULL;
    ulong tempbufsize = 0, usedsize = 0, bytes_written = 0; // bw only counts lzh data
    lzhErr err;
    lzhHeader* lzhhdr = NULL;
    uchar csum = 0, * cptr = NULL, ebcount = 0;
    ulong clen = 0;

    // alloc a temp buffer for compression (assume file can't be compressed at all)
    tempbufsize = fe->size;
    tempbuf = new uchar[tempbufsize + sizeof(lzhHeader) + sizeof(lzhHeaderAfterFilename) + 256];

    if (compress) {
        // compress this file
        err = lzhCompress(fe->name, fe->nameLen, fe->data, fe->size, tempbuf, tempbufsize, &usedsize);
    } else {
        memcpy(tempbuf, fe->data, fe->size);
        usedsize = fe->size;
    }

    // update the type and then fix the header sum
    lzhhdr = (lzhHeader *)tempbuf;
    lzhhdr->fileType  = fe->type;
    lzhhdr->headerSum = lzhCalcSum(tempbuf + 2, lzhhdr->headerSize);

    // write it to the output
    fwrite(tempbuf, 1, usedsize, fp);
    bytes_written += usedsize;


    // calculate checksum over LZH header and compressed data
    cptr = tempbuf;
    clen = usedsize;
    csum = 0x00;
    while (clen--)
        csum += *cptr++;

    // write extra bytes, depending on the layout
    if (fileIdx != -1)
    {
        switch (biosdata.layout)
        {
        case LAYOUT_2_2_2:
            ebcount = 2;
            break;

        case LAYOUT_2_1_1:
            if (fileIdx == 0)
                ebcount = 2;
            else
                ebcount = 1;
            break;

        case LAYOUT_1_1_1:
            ebcount = 1;
            break;
        }
    }
    else
    {
        // always write 2 extra bytes
        ebcount = 2;
    }

    if (ebcount > 0)
    {
        fputc(0x00, fp);
        if (ebcount > 1) {
            fputc(csum, fp);
        }
    }

    ulong bs = lzhhdr->headerSize + lzhhdr->compressedSize;

    // free our buffer
    delete[] tempbuf;

    return bs;
}

fileEntry *biosScanForID(ushort id)
{
    fileEntry *fe = &biosdata.fileTable[0];
    ulong count = biosdata.fileCount;

    while (count--)
    {
        if (fe->type == id)
            return fe;

        fe++;
    }

    return NULL;
}

awdbeBIOSVersion biosGetVersion(void)
{
    awdbeBIOSVersion vers = awdbeBIOSVerUnknown;
    fileEntry *fe;
    uchar *sptr;
    int len;

    fe = biosScanForID(0x5000);
    if (fe == NULL)
        return vers;

    // get the bios's version
    sptr = ((uchar *)fe->data) + 0x1E060;
    len  = (*sptr++) - 1;

    while (len--)
    {
        if (!strncmp((const char*)sptr, "v4.50PG", 7) || !strncmp((const char*)sptr, "v4.51PG", 7))
        {
            vers = awdbeBIOSVer451PG;
            len  = 0;
        }
        else if (!strncmp((const char*)sptr, "v6.00PG", 7))
        {
            vers = awdbeBIOSVer600PG;
            len  = 0;
        }
        else if (!strncmp((const char*)sptr, "v6.0", 4))
        {
            vers = awdbeBIOSVer60;
            len  = 0;
        }
        else
        {
            sptr++;
        }
    }

    return vers;
}

#define DEBUG(blob)

bool biosSaveFile(char* fname)
{
    //HWND loaddlg, hwnd_loadtext, hwnd_loadprog;
    int t, pos, count;
    fileEntry* fe;
    ulong decompSize, bootSize;
    uchar ch, csum1, csum2, rcs1, rcs2;
    ulong new_size = 0, old_size = 0;
    char buf[257];

    // open the file
    FILE* fp = fopen64(fname, "wb");
    FILE* tp;
    DEBUG(tp = fopen64("temp", "wb");)
    if (fp == NULL || tp == NULL)
    {

        MessageBox("Unable to write BIOS image!", "Error", QMessageBox::Ok, QMessageBox::Ok);
        return false;
    }

    // todo: rewrite to cli progress saving
    // put up our saving dialog and initialize it
    //loaddlg = CreateDialog(hinst, MAKEINTRESOURCE(IDD_WORKING), hwnd, (DLGPROC)LoadSaveProc);

    SetWindowText("Saving Image...");
    //hwnd_loadtext = GetDlgItem(loaddlg, IDC_LOADING_TEXT);
    //hwnd_loadprog = GetDlgItem(loaddlg, IDC_LOADING_PROGRESS);

    SetWindowText("Writing components...");
    //SendMessage(hwnd_loadprog, PBM_SETRANGE32, 0, biosdata.fileCount);

    // first, flat out write the loaded image to restore extra code/data segments we couldn't load
    // note: this leaves in place data not overwritten due to shorter files!
    fwrite(biosdata.imageData, 1, biosdata.imageSize, fp);
    DEBUG(fwrite(biosdata.imageData, 1, biosdata.imageSize, tp);)
    rewind(fp);
    DEBUG(rewind(tp);)

    // fill in FFh until we reach the start of the file table
    t = biosdata.tableOffset;
    while (t--) {
        fputc(0xFF, fp);
        DEBUG(fputc(0xFF, tp);)
    }


    // iterate through all files with no fixed offset and no special flags, compress them, and write them
    for (t = 0; t < biosdata.fileCount; t++)
    {
        //SendMessage(hwnd_loadprog, PBM_SETPOS, t, 0);

        fe = &biosdata.fileTable[t];
        if ((fe->offset == 0) && (fe->flags == 0))
        {
            old_size += fe->originalSize;
            new_size += biosWriteComponent(fe, fp, t, true);
            DEBUG(biosWriteComponent(fe, tp, t, false);)
        }
    }

    if (new_size < old_size) {
        // new file is smaller than original, can happen if more compressable
        // padd with 0xff
        size_t diff = old_size - new_size;
        unsigned char* padd = (unsigned char*)malloc(diff);
        if (NULL != padd) {
            memset(padd, 0xff, diff);
            fwrite(padd, 1, diff, fp);
            free(padd);
        } // ignore, we see what happens ...
    }

    // write the decompression and boot blocks...
    SetWindowText("Writing boot/decomp blocks...");

    fe = biosScanForID(TYPEID_DECOMPBLOCK);
    decompSize = ((fe == NULL) ? (0) : (fe->size));

    fe = biosScanForID(TYPEID_BOOTBLOCK);
    bootSize = ((fe == NULL) ? (0) : (fe->size));

    fseek(fp, biosdata.imageSize - (decompSize + bootSize), 0);
    DEBUG(fseek(tp, biosdata.imageSize - (decompSize + bootSize), 0);)

    // write the blocks
    fe = biosScanForID(TYPEID_DECOMPBLOCK);
    if (fe != NULL) {
        fwrite(fe->data, 1, fe->size, fp);
        DEBUG(fwrite(fe->data, 1, fe->size, tp);)
    }

    fe = biosScanForID(TYPEID_BOOTBLOCK);
    if (fe != NULL) {
        fwrite(fe->data, 1, fe->size, fp);
        DEBUG(fwrite(fe->data, 1, fe->size, tp);)
    }

    // now write components which have a fixed offset
    SetWindowText("Writing fixed components...");

    for (t = 0; t < biosdata.fileCount; t++)
    {
        //SendMessage(hwnd_loadprog, PBM_SETPOS, t, 0);

        fe = &biosdata.fileTable[t];
        if (fe->offset != 0)
        {
            fseek(fp, fe->offset, SEEK_SET);
            biosWriteComponent(fe, fp, -1, true);
            DEBUG(fseek(tp, fe->offset, SEEK_SET);)
            DEBUG(biosWriteComponent(fe, fp, -1, FALSE);)
        }
    }

    // finally, if the BIOS is version 6.00PG, update the internal checksum in the decompression block...
    if (biosGetVersion() == awdbeBIOSVer600PG)
    {
        fe = biosScanForID(TYPEID_DECOMPBLOCK);
        if (fe != NULL)
        {
            // re-open the file in read-only mode
            fclose(fp);
            fp = fopen64(fname, "rb");

            if (NULL == fp) {
                goto end;
            }
            // calculate the position of the checksum bytes
            pos = ((biosdata.imageSize - (decompSize + bootSize)) & 0xFFFFF000) + 0xFFE;

            //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
            // calculate the checksum
            csum1 = 0x00;
            csum2 = 0xF8; //0x6E;
            count = pos;

            while (count--)
            {
                ch = fgetc(fp);
                csum1 += ch;
                csum2 += ch;
            }

#if 0
            rcs1 = fgetc(fp);
            rcs2 = fgetc(fp);

            snprintf(buf, 256, "Current checksum: %02X %02X\nCalculated checksum: %02X %02X", rcs1, rcs2, csum1, csum2);
            MessageBox(hwnd, buf, "Notice", MB_OK);
#else

            // re-open the file in read-write mode
            fclose(fp);
            fp = fopen64(fname, "r+b");

            if (NULL == fp) goto end;
            // seek to the checksum position
            fseek(fp, pos, 0);

            // write the checksum bytes
            fputc(csum1, fp);
            fputc(csum2, fp);
#endif
            //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        }
    }

    // close the file
    fclose(fp);
    DEBUG(fclose(tp);)

end:
    // kill our window
    //DestroyWindow(loaddlg);

    return true;
}

void Bios::biosTitleUpdate(void)
{
    char buf[256];
    int t;
    ulong size;
    fileEntry *fe;

    snprintf(buf, 256, "%s - [%s%s", APP_VERSION, biosdata.fname, (biosdata.modified == true) ? " *]" : "]");
    SetWindowText(buf);

    // go through all files in the table with an offset of 0 and calculate a total size
    size = 0;

    for (t = 0; t < biosdata.fileCount; t++)
    {
        fe = &biosdata.fileTable[t];

        if (fe->offset == 0)
            size += fe->compSize;
    }

    snprintf(buf, 256, "Used: %dK/%dK", size / 1024, biosdata.maxTableSize / 1024);
    SetWindowText(buf);
    //SendMessage(statusWnd, SB_SETTEXT, 1, (LPARAM)buf);

    // set the global free size while we're at it...
    biosFreeSpace = biosdata.maxTableSize - size;
}

bool Bios::biosSave(void)
{
    bool ret;

    // update any current data
    // FIXME: maybe removed
    //biosUpdateCurrentDialog();

    // zap the modified flag
    biosdata.modified = false;

    // and call the save file handler...
    ret = biosSaveFile(biosdata.fname);

    // if successful, update the title bar (removes the * mark)
    if (ret == true)
        biosTitleUpdate();

    // return result
    return ret;
}

void Bios::biosSetModified(bool val)
{
    biosdata.modified = val;
    biosTitleUpdate();
}

bool Bios::biosHandleModified(char *text)
{
    // update any leftover data
    // FIXME: maybe removed
    // biosUpdateCurrentDialog();

    // check the modified flag
    if (biosdata.modified == false)
        return true;


    int ret = MessageBox(text, "Notice", QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);

    switch (ret)
    {
    case QMessageBox::Yes:
        // save first
        biosSave();
        break;

    case QMessageBox::No:
        // do nothing
        break;

    case QMessageBox::Cancel:
        return false;
    }

    return true;
}
/*
awdbeItem *pluginFindHash(ulong hash)
{
    awdbeItemEntry *ie = itemMenuList;
    awdbeItem *item;
    ulong count;

    // iterate through each item list, looking for a matching hash
    while (ie != NULL)
    {
        item  = ie->list;
        count = ie->count;

        while (count--)
        {
            if (item->hash == hash)
            {
                // found it!
                return item;
            }

            item++;
        }

        ie = ie->next;
    }

    return NULL;
}
*/
void Bios::biosFreeMemory(void)
{
    int t;
    fileEntry *fe;
    awdbeItem *item;

    // if the current hash points to a plugin, call it's "onDestroy" function to tell it it's about to be killed
    if ((curFileEntry != NULL) && (curHash > HASH_UNKNOWN_ITEM_MAX))
    {
        switch (curHash)
        {
        case HASH_SUBMENU_ITEM:
        case HASH_RECOGNIZED_ROOT:
        case HASH_UNKNOWN_ROOT:
        case HASH_INCLUDABLE_ROOT:
            break;

        default:
            // TODO: rewrite and detect functionality
            // find the item that responds to this hash
            /*
            item = pluginFindHash(curHash);
            if (item != NULL)
            {
                // call this plugin's create dialog function to show the window
                pluginCallOnDestroyDialog(item, hModDlgWnd);
            }
            */
            break;
        }
    }

    if (biosdata.fileTable != NULL)
    {
        for (t = 0; t < biosdata.fileCount; t++)
        {
            fe = &biosdata.fileTable[t];
            delete []fe->name;
            delete []fe->data;
        }

        delete []biosdata.fileTable;
        biosdata.fileTable = NULL;
    }

    if (biosdata.imageData != NULL)
    {
        delete []biosdata.imageData;
        biosdata.imageData = NULL;
    }
}

fileEntry* biosExpandTable(void)
{
    fileEntry *tempTable;

    // first, store the current table
    tempTable = biosdata.fileTable;

    // increase the file count and alloc a new one
    biosdata.fileCount++;
    biosdata.fileTable = new fileEntry[biosdata.fileCount];

    // copy the existing file table into the new one
    memcpy(biosdata.fileTable, tempTable, (biosdata.fileCount - 1) * sizeof(fileEntry));

    // delete the old table
    delete []tempTable;

    // return a pointer to the new entry, but zap its memory first...
    tempTable = &biosdata.fileTable[biosdata.fileCount - 1];
    memset(tempTable, 0, sizeof(fileEntry));

    return tempTable;
}

void biosWriteEntry(fileEntry *fe, lzhHeader *lzhhdr, ulong offset)
{
    ushort crc;
    lzhHeaderAfterFilename *lzhhdra;

    lzhhdra = (lzhHeaderAfterFilename *) ((lzhhdr->filename) + lzhhdr->filenameLen);

    fe->nameLen = lzhhdr->filenameLen;
    fe->name = new char[fe->nameLen + 1];
    memcpy(fe->name, lzhhdr->filename, fe->nameLen);
    fe->name[fe->nameLen] = 0;

    fe->size	 = lzhhdr->originalSize;
    fe->compSize = lzhhdr->compressedSize;
    fe->originalSize = lzhhdr->compressedSize + lzhhdr->headerSize;
    fe->type	 = lzhhdr->fileType;
    fe->crc		 = lzhhdra->crc;
    fe->data	 = (void *)new uchar[fe->size];
    fe->offset	 = offset;
    fe->flags	 = 0;

    // decompress file
    if (lzhExpand(lzhhdr, fe->data, fe->size, &crc) != LZHERR_OK)
    {
        // error extracting
        MessageBox("Error extracting component!\n\nThis BIOS Image may be corrupted or damaged.  The editor will still continue to load\nthe image, but certain components may not be editable.",
                   "Notice", QMessageBox::Ok, QMessageBox::Ok);
    }
    else
    {
        if (fe->crc != crc)
        {
            // CRC failed
            fe->crcOK = false;

            MessageBox("CRC check failed!\n\nThis BIOS Image may be corrupted or damaged.  The editor will still continue to load\nthe image, but certain components may not be editable.",
                       "Notice", QMessageBox::Ok, QMessageBox::Ok);
        }
        else
        {
            fe->crcOK = true;
        }
    }
}
/*
void cleanTempPath(void)
{
    char cwd[256];
    long hFile;
    struct _finddata_t fd;

    // save the current path
    _getcwd(cwd, 256);

    // change into the temp dir
    if (_chdir(fullTempPath) < 0)
    {
        // some error occured changing into our temp dir.  we don't want to destroy any files here!
        snprintf(cwd, 256, "Unable to clean temporary files dir: [%s]", fullTempPath);
        MessageBox(cwd, "Internal Error", QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    // iterate through all files, and delete them
    hFile = _findfirst("*.*", &fd);
    if (hFile != -1)
    {
        do
        {
            // ignore directories
            if (fd.attrib != _A_SUBDIR)
                _unlink(fd.name);
        } while (_findnext(hFile, &fd) == 0);

        _findclose(hFile);
    }

    // return back
    _chdir(cwd);
}

void makeTempPath(void)
{
    char *tmp = NULL;
    size_t tmp_len = 0;
    errno_t err;

    // try to get the system temp path

    err = _dupenv_s(&tmp, &tmp_len, "Temp");
    if (tmp == NULL || err) tmp = "C:\\TEMP";

    // make sure it exists (it really should...)
    _mkdir(tmp);

    // split off a directory for us
    snprintf(fullTempPath, 256, "%s\\Award BIOS Editor Temp Files", tmp);

    // make sure this exists too
    _mkdir(fullTempPath);

    // now cleanup the temp path...
    cleanTempPath();
}
*/
bool Bios::biosOpenFile(char *fname)
{
    FILE *fp;
    uchar *ptr;
    uchar _0xEA;
    ulong count, _MRB;
    //HWND loaddlg, hwnd_loadtext, hwnd_loadprog;
    lzhHeader *lzhhdr;
    lzhHeaderAfterFilename *lzhhdra;
    bool done;
    int curFile;
    uchar *nextUpdate, *bootBlockData = NULL, *decompBlockData = NULL;
    ulong bootBlockSize = 0, decompBlockSize = 0;
    fileEntry *fe;

    // warn if the current bios has been modified
    if (biosHandleModified(biosChangedText) == false)
        return false;

    // stop update checking for this image
    //biosClearUpdateList();

    // open the image
    fp = fopen64(fname, "rb");
    if (fp == NULL)
    {
        MessageBox("Unable to open BIOS image!", "Error", QMessageBox::Ok, QMessageBox::Ok);
        return false;
    }

    // check if this is a real bios image
    fseek(fp, -16, SEEK_END);		// 16th byte from the EOF is a jump instruction (EAh)
    _0xEA = fgetc(fp);

    fseek(fp, -11, SEEK_END);		// 11th byte from EOF contains "*MRB*", but we're only gonna check for the first 4 bytes
    fread(&_MRB, 1, 4, fp);

    fseek(fp, 0, SEEK_END);			// size is divisible by 1024 (but not greater than 1Mb)
    biosdata.imageSize = ftell(fp);

    if ((_0xEA != 0xEA) || (_MRB != 'BRM*') || ((biosdata.imageSize % 1024) != 0) || (biosdata.imageSize > (1024 * 1024)))
    {
        if (MessageBox("This image does not appear to be a valid Award BIOS image.\n\nDo you wish to attempt to continue to loading anyway?",
                       "Notice", QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::No)
        {
            fclose(fp);
            return false;
        }
    }

    // looks okay from here...
    count = biosdata.imageSize / 1024;
    strncpy(biosdata.fname, fname, sizeof(biosdata.fname));

    // free any already allocated memory
    biosFreeMemory();

    // put up our loading dialog and initialize it
    // TODO: create loading image bios progress
    //loaddlg = CreateDialog(hinst, MAKEINTRESOURCE(IDD_WORKING), hwnd, (DLGPROC)LoadSaveProc);

    SetWindowText("Loading Image...");
    //hwnd_loadtext = GetDlgItem(loaddlg, IDC_LOADING_TEXT);
    //hwnd_loadprog = GetDlgItem(loaddlg, IDC_LOADING_PROGRESS);

    SetWindowText("Loading image into memory...");
    //SendMessage(hwnd_loadprog, PBM_SETRANGE, 0, MAKELPARAM(0, count));
    //SendMessage(hwnd_loadprog, PBM_SETSTEP, 1, 0);

    // allocate space and load the image into memory
    biosdata.imageData = new uchar[biosdata.imageSize];
    ptr	= biosdata.imageData;

    fseek(fp, 0, SEEK_SET);

    while (count--)
    {
        //SendMessage(hwnd_loadprog, PBM_STEPIT, 0, 0);

        fread(ptr, 1024, 1, fp);
        ptr += 1024;
    }

    // close the file
    fclose(fp);

    // scan for the boot and decompression blocks, and extract them
    SetWindowText("Scanning for Boot Block...");

    ptr	  = biosdata.imageData;
    count = biosdata.imageSize;

    const char *bootBlockString = "Award BootBlock Bios";

    while (count--)
    {
        // FIXME: MAIN PROBLEM SUCCESSFUL BUILDING
        if (!_memicmp((const void*)ptr, (const void*)bootBlockString, (size_t)20))
        {
            bootBlockSize = biosdata.imageSize - (ptr - biosdata.imageData);
            bootBlockData = new uchar[bootBlockSize];
            memcpy(bootBlockData, ptr, bootBlockSize);

            count = 0;
        }

        ptr++;
    }

    if (bootBlockData == NULL)
    {
        MessageBox("Unable to locate the Boot Block within the BIOS Image!\n\nThe editor will still be able to modify this image, but this component will be\nunaccessable.  Re-flashing with a saved version of this BIOS is NOT RECOMMENDED!",
                   "Notice", QMessageBox::Ok, QMessageBox::Ok);
    }

    // next, decompression block...
    SetWindowText("Scanning for Decompression Block...");

    ptr	  = biosdata.imageData;
    count = biosdata.imageSize;
    while (count--)
    {
        if (!_memicmp((char*)ptr, "= Award Decompression Bios =", 28))
        {
            // copy the decompression block
            decompBlockSize = (biosdata.imageSize - (ptr - biosdata.imageData)) - bootBlockSize;
            decompBlockData = new uchar[decompBlockSize];
            memcpy(decompBlockData, ptr, decompBlockSize);

            count = 0;
        }

        ptr++;
    }

    if (decompBlockData == NULL)
    {
        MessageBox("Unable to locate the Decompression Block within the BIOS Image!\n\nThe editor will still be able to modify this image, but this component will be\nunaccessable.  Re-flashing with a saved version of this BIOS is NOT RECOMMENDED!",
                   "Notice", QMessageBox::Ok, QMessageBox::Ok);
    }

    // load the file table
    biosdata.layout		 = LAYOUT_UNKNOWN;
    biosdata.fileCount	 = 0;
    biosdata.tableOffset = 0xDEADBEEF;

    SetWindowText("Parsing File Table...");
    //SendMessage(hwnd_loadprog, PBM_SETRANGE32, 0, biosdata.imageSize);
    //SendMessage(hwnd_loadprog, PBM_SETPOS, 0, 0);

    // first, determine the offset of the file table
    ptr  = biosdata.imageData;
    done = false;

    nextUpdate = ptr + 1024;

    while (!done)
    {
        if (!memcmp(ptr + 2, "-lh", 3))
        {
            biosdata.tableOffset = (ptr - biosdata.imageData);
            done = true;
        }
        else
        {
            if ((ulong)(ptr - biosdata.imageData) >= biosdata.imageSize)
                done = true;
        }

        ptr++;

        if (ptr >= nextUpdate)
        {
            //SendMessage(hwnd_loadprog, PBM_SETPOS, (WPARAM)(ptr - biosdata.imageData), 0);
            nextUpdate = ptr + 1024;
        }
    }

    if (biosdata.tableOffset == 0xDEADBEEF)
    {
        MessageBox("Unable to locate a file table within the BIOS image!\nIt is possible that this version of the editor simply does not support this type.\n\nPlease check the homepage listed under Help->About and see if a new version is\navailable for download.",
                   "Error", QMessageBox::Ok, QMessageBox::Ok);

        return true;
    }

    //SendMessage(hwnd_loadprog, PBM_SETPOS, 0, 0);

    // next, determine the total size of the file table and file count, and try to determine the layout
    ptr  = biosdata.imageData + biosdata.tableOffset;
    done = false;
    while (!done)
    {
        lzhhdr  = (lzhHeader *)ptr;
        lzhhdra = (lzhHeaderAfterFilename *) ((lzhhdr->filename) + lzhhdr->filenameLen);

        if ((lzhhdr->headerSize == 0) || (lzhhdr->headerSize == 0xFF))
            done = true;
        else
        {
            if (lzhCalcSum(ptr + 2, lzhhdr->headerSize) != lzhhdr->headerSum)
            {
                MessageBox("BIOS Image Checksum failed!\n\nThis BIOS Image may be corrupted or damaged.  The editor will still continue to load\nthe image, but certain components may not be editable.",
                           "Notice", QMessageBox::Ok, QMessageBox::Ok);
            }

            // advance to next file
            biosdata.fileCount++;
            ptr += (2 + lzhhdr->headerSize + lzhhdr->compressedSize);

            // see how many bytes are needed to get to the next file, and adjust the type if necessary...
            if (biosdata.fileCount == 1)
            {
                // first file... could be anything...
                if (!memcmp(ptr + 4, "-lh", 3))
                {
                    biosdata.layout = LAYOUT_2_2_2;
                    ptr += 2;
                }
                else if (!memcmp(ptr + 3, "-lh", 3))
                {
                    biosdata.layout = LAYOUT_1_1_1;
                    ptr++;
                }
            }
            else
            {
                // next file, so we have some constraints to work with.
                if (!memcmp(ptr + 4, "-lh", 3))
                {
                    if (biosdata.layout == LAYOUT_2_2_2)
                    {
                        // continue with 2_2_2...
                        ptr += 2;
                    }
                    else
                    {
                        // uh-oh, this is a new one!
                        biosdata.layout = LAYOUT_UNKNOWN;
                    }
                }
                else if (!memcmp(ptr + 3, "-lh", 3))
                {
                    if (biosdata.layout == LAYOUT_2_2_2)
                    {
                        if (biosdata.fileCount == 2)
                        {
                            // ok, we can switch here.
                            biosdata.layout = LAYOUT_2_1_1;
                            ptr++;
                        }
                        else
                        {
                            // hmm... don't know this one either!
                            biosdata.layout = LAYOUT_UNKNOWN;
                        }
                    }
                    else if (biosdata.layout == LAYOUT_2_1_1)
                    {
                        // no problems...
                        ptr++;
                    }
                    else if (biosdata.layout == LAYOUT_1_1_1)
                    {
                        // no problems here either...
                        ptr++;
                    }
                }
                else
                {
                    switch (biosdata.layout)
                    {
                    case LAYOUT_2_2_2:
                        //							if ( (*((ulong *) (ptr + 2)) == 0xFFFFFFFF) || (*((ulong *) (ptr + 2)) == 0x00000000) )
                        if ( (*(ptr + 2) == 0xFF) || (*(ptr + 2) == 0x00) )
                        {
                            // ok, end of file table.
                            ptr += 2;
                        }
                        else
                        {
                            // not good!
                            biosdata.layout = LAYOUT_UNKNOWN;
                        }
                        break;

                    case LAYOUT_2_1_1:
                    case LAYOUT_1_1_1:
                        //							if ( (*((ulong *) (ptr + 1)) == 0xFFFFFFFF) || (*((ulong *) (ptr + 1)) == 0x00000000) )
                        if ( (*(ptr + 1) == 0xFF) || (*(ptr + 1) == 0x00) )
                        {
                            // ok, end of file table.
                            ptr++;
                        }
                        else
                        {
                            // not good!
                            biosdata.layout = LAYOUT_UNKNOWN;
                        }
                        break;
                    }
                }
            }
        }

        //SendMessage(hwnd_loadprog, PBM_SETPOS, (WPARAM)(ptr - biosdata.imageData), 0);
    }

    // check for a valid layout
    if (biosdata.layout == LAYOUT_UNKNOWN)
    {
        MessageBox("Unable to determine the layout of the file table within the BIOS Image!\nIt is possible that this version of the editor simply does not support this type.\n\nPlease check the homepage listed under Help->About and see if a new version is\navailable for download.",
                   "Error", QMessageBox::Ok, QMessageBox::Ok);

        return true;
    }

    // allocate our file table space...
    SetWindowText("Loading File Table...");
    //SendMessage(hwnd_loadprog, PBM_SETRANGE32, 0, biosdata.imageSize);
    //SendMessage(hwnd_loadprog, PBM_SETPOS, 0, 0);

    biosdata.fileTable = new fileEntry[biosdata.fileCount];

    // decompress and load the file table into memory...
    ptr		= biosdata.imageData + biosdata.tableOffset;
    curFile = 0;
    done	= false;

    while (!done)
    {
        lzhhdr  = (lzhHeader *)ptr;
        lzhhdra = (lzhHeaderAfterFilename *) ((lzhhdr->filename) + lzhhdr->filenameLen);

        if ((lzhhdr->headerSize == 0) || (lzhhdr->headerSize == 0xFF))
        {
            done = true;
        }
        else
        {
            // fill out fileentry for this file
            fe = &biosdata.fileTable[curFile];
            biosWriteEntry(fe, lzhhdr, 0);

            // advance to next file
            ptr += (2 + lzhhdr->headerSize + lzhhdr->compressedSize);

            // skip past extra data
            switch (biosdata.layout)
            {
            case LAYOUT_2_2_2:
                ptr += 2;
                break;

            case LAYOUT_2_1_1:
                if (curFile == 0)
                {
                    ptr += 2;
                }
                else
                {
                    ptr++;
                }
                break;

            case LAYOUT_1_1_1:
                ptr++;
                break;
            }

            curFile++;
        }

        //SendMessage(hwnd_loadprog, PBM_SETPOS, (WPARAM)(ptr - biosdata.imageData), 0);
    }

    // calculate available table space
    biosdata.maxTableSize = (biosdata.imageSize - biosdata.tableOffset) - (decompBlockSize + bootBlockSize);

    // scan for fixed-offset components
    SetWindowText("Scanning for fixed components...");

    // continue until we hit the end of the image
    nextUpdate = ptr + 1024;

    while (ptr < (biosdata.imageData + (biosdata.imageSize - 6)))
    {
        if (!memcmp(ptr + 2, "-lh", 3) && (*(ptr + 6) == '-'))
        {
            // found something... maybe...
            lzhhdr  = (lzhHeader *)ptr;
            lzhhdra = (lzhHeaderAfterFilename *) ((lzhhdr->filename) + lzhhdr->filenameLen);

            if ((lzhhdr->headerSize != 0) && (lzhhdr->headerSize != 0xFF))
            {
                // looks somewhat okay -- check the checksum
                if (lzhCalcSum(ptr + 2, lzhhdr->headerSize) == lzhhdr->headerSum)
                {
                    // we found something!  add it to our table
                    fe = biosExpandTable();
                    biosWriteEntry(fe, lzhhdr, (ulong)(ptr - biosdata.imageData));

                    // if this offset is less than our maximum table size, then adjust the size appropriately...
                    // (note: the dynamic file table cannot exceed the space occupied by any fixed components)
                    if (biosdata.maxTableSize > fe->offset)
                        biosdata.maxTableSize = fe->offset;

                    // advance pointer past this file
                    ptr += (2 + lzhhdr->headerSize + lzhhdr->compressedSize);
                }
            }
        }

        ptr++;

        if (ptr >= nextUpdate)
        {
            //SendMessage(hwnd_loadprog, PBM_SETPOS, (WPARAM)(ptr - biosdata.imageData), 0);
            nextUpdate = ptr + 1024;
        }
    }

    // insert the decompression and boot blocks
    if (decompBlockData != NULL)
    {
        fe = biosExpandTable();
        fe->nameLen = strlen("decomp_blk.bin");
        fe->name = new char[std::max(fe->nameLen + 1, (ulong)4)];
        strncpy(fe->name, "decomp_blk.bin", std::max(fe->nameLen + 1, (ulong)4));

        fe->size	 = decompBlockSize;
        fe->compSize = 0;
        fe->type	 = TYPEID_DECOMPBLOCK;
        fe->crc		 = 0;
        fe->crcOK	 = true;
        fe->data	 = (void *)new uchar[fe->size];
        fe->offset	 = 0;
        fe->flags	 = FEFLAGS_DECOMP_BLOCK;

        memcpy(fe->data, decompBlockData, decompBlockSize);
        delete []decompBlockData;
    }

    if (bootBlockData != NULL)
    {
        fe = biosExpandTable();
        fe->nameLen = strlen("boot_blk.bin");
        fe->name = new char[std::max(fe->nameLen + 1, (ulong)4)];
        strncpy(fe->name, "boot_blk.bin", std::max(fe->nameLen + 1, (ulong)4));

        fe->size	 = bootBlockSize;
        fe->compSize = 0;
        fe->type	 = TYPEID_BOOTBLOCK;
        fe->crc		 = 0;
        fe->crcOK	 = true;
        fe->data	 = (void *)new uchar[fe->size];
        fe->offset	 = 0;
        fe->flags	 = FEFLAGS_BOOT_BLOCK;

        memcpy(fe->data, bootBlockData, bootBlockSize);
        delete []bootBlockData;
    }

    // kill our window
    //DestroyWindow(loaddlg);

    // enable all editing controls
    //enableControls(true, true);

    // call all plugins' onLoad functions
    //pluginCallOnLoad(biosdata.fileTable, biosdata.fileCount);

    // populate the component list with the files in our table
    // FIXME: undetected functionality
    //biosUpdateComponents();

    // zap the modified flag and update our main window's title and status bar...
    biosSetModified(false);

    // and cleanup our temp directory!
    // TODO: fix for Linux
    //cleanTempPath();

    return true;
}

bool Bios::biosOpen(void)
{
    char fname[256]; //, *sptr, *dptr;

    // warn if the current bios has been modified
    if (biosHandleModified(biosChangedText) == false)
        return false;

    // display the open dialog
    strcpy(fname, QStrToCharArr(args.at(0)));

    if (!QFile(fname).exists())
        return false;

    // strip out path from returned filename
    /*
    sptr = strchr(fname, '\\');
    if (sptr != NULL)
    {
        sptr++;
        strcpy_s(config.lastPath, sizeof(config.lastPath), fname);

        sptr = config.lastPath;
        do
        {
            dptr = strchr(sptr, '\\');
            if (dptr) sptr = dptr + 1;
        } while (dptr != NULL);

        *sptr = 0;
    }
    */

    // zap the modified flag
    biosdata.modified = false;

    // and call the open file handler...
    return biosOpenFile(fname);
}
