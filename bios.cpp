#include "bios.h"
#include "lzh.h"
#include "award_exports.h"

#include <ctime>
#include <QMessageBox>

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
void biosClearUpdateList(void);


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

bool biosHandleModified(char *text)
{
    // update any leftover data
    // FIXME: maybe removed
    // biosUpdateCurrentDialog();

    // check the modified flag
    if (biosdata.modified == false)
        return true;

    //res = MessageBox(hwnd, text, "Notice", MB_YESNOCANCEL);

    QMessageBox msgBox;
    msgBox.setText(text);
    msgBox.setInformativeText("Notice");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Yes);
    int ret = msgBox.exec();

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

bool biosOpenFile(char *fname)
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
    if (biosHandleModified(biosChangedText) == FALSE)
        return FALSE;

    // stop update checking for this image
    biosClearUpdateList();

    // open the image
    fopen_s(&fp, fname, "rb");
    if (fp == NULL)
    {
        MessageBox(hwnd, "Unable to open BIOS image!", "Error", MB_OK);
        return FALSE;
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
        if (MessageBox(hwnd, "This image does not appear to be a valid Award BIOS image.\n\n"
                             "Do you wish to attempt to continue to loading anyway?", "Notice", MB_YESNO) == IDNO)
        {
            fclose(fp);
            return FALSE;
        }
    }

    // looks okay from here...
    count = biosdata.imageSize / 1024;
    strcpy_s(biosdata.fname, sizeof(biosdata.fname), fname);

    // free any already allocated memory
    biosFreeMemory();

    // put up our loading dialog and initialize it
    loaddlg = CreateDialog(hinst, MAKEINTRESOURCE(IDD_WORKING), hwnd, (DLGPROC)LoadSaveProc);

    SetWindowText(loaddlg, "Loading Image...");
    hwnd_loadtext = GetDlgItem(loaddlg, IDC_LOADING_TEXT);
    hwnd_loadprog = GetDlgItem(loaddlg, IDC_LOADING_PROGRESS);

    SetWindowText(hwnd_loadtext, "Loading image into memory...");
    SendMessage(hwnd_loadprog, PBM_SETRANGE, 0, MAKELPARAM(0, count));
    SendMessage(hwnd_loadprog, PBM_SETSTEP, 1, 0);

    // allocate space and load the image into memory
    biosdata.imageData = new uchar[biosdata.imageSize];
    ptr	= biosdata.imageData;

    fseek(fp, 0, SEEK_SET);

    while (count--)
    {
        SendMessage(hwnd_loadprog, PBM_STEPIT, 0, 0);

        fread(ptr, 1024, 1, fp);
        ptr += 1024;
    }

    // close the file
    fclose(fp);

    // scan for the boot and decompression blocks, and extract them
    SetWindowText(hwnd_loadtext, "Scanning for Boot Block...");

    ptr	  = biosdata.imageData;
    count = biosdata.imageSize;

    const char *bootBlockString = "Award BootBlock Bios";

    while (count--)
    {
        if (!_memicmp(ptr, bootBlockString, 20))
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
        MessageBox(hwnd, "Unable to locate the Boot Block within the BIOS Image!\n\n"
                         "The editor will still be able to modify this image, but this component will be\n"
                         "unaccessable.  Re-flashing with a saved version of this BIOS is NOT RECOMMENDED!", "Notice", MB_OK);
    }

    // next, decompression block...
    SetWindowText(hwnd_loadtext, "Scanning for Decompression Block...");

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
        MessageBox(hwnd, "Unable to locate the Decompression Block within the BIOS Image!\n\n"
                         "The editor will still be able to modify this image, but this component will be\n"
                         "unaccessable.  Re-flashing with a saved version of this BIOS is NOT RECOMMENDED!", "Notice", MB_OK);
    }

    // load the file table
    biosdata.layout		 = LAYOUT_UNKNOWN;
    biosdata.fileCount	 = 0;
    biosdata.tableOffset = 0xDEADBEEF;

    SetWindowText(hwnd_loadtext, "Parsing File Table...");
    SendMessage(hwnd_loadprog, PBM_SETRANGE32, 0, biosdata.imageSize);
    SendMessage(hwnd_loadprog, PBM_SETPOS, 0, 0);

    // first, determine the offset of the file table
    ptr  = biosdata.imageData;
    done = FALSE;

    nextUpdate = ptr + 1024;

    while (!done)
    {
        if (!memcmp(ptr + 2, "-lh", 3))
        {
            biosdata.tableOffset = (ptr - biosdata.imageData);
            done = TRUE;
        }
        else
        {
            if ((ulong)(ptr - biosdata.imageData) >= biosdata.imageSize)
                done = TRUE;
        }

        ptr++;

        if (ptr >= nextUpdate)
        {
            SendMessage(hwnd_loadprog, PBM_SETPOS, (WPARAM)(ptr - biosdata.imageData), 0);
            nextUpdate = ptr + 1024;
        }
    }

    if (biosdata.tableOffset == 0xDEADBEEF)
    {
        MessageBox(hwnd, "Unable to locate a file table within the BIOS image!\n"
                         "It is possible that this version of the editor simply does not support this type.\n\n"
                         "Please check the homepage listed under Help->About and see if a new version is\n"
                         "available for download.", "Error", MB_OK);

        DestroyWindow(loaddlg);
        return TRUE;
    }

    SendMessage(hwnd_loadprog, PBM_SETPOS, 0, 0);

    // next, determine the total size of the file table and file count, and try to determine the layout
    ptr  = biosdata.imageData + biosdata.tableOffset;
    done = FALSE;
    while (!done)
    {
        lzhhdr  = (lzhHeader *)ptr;
        lzhhdra = (lzhHeaderAfterFilename *) ((lzhhdr->filename) + lzhhdr->filenameLen);

        if ((lzhhdr->headerSize == 0) || (lzhhdr->headerSize == 0xFF))
            done = TRUE;
        else
        {
            if (lzhCalcSum(ptr + 2, lzhhdr->headerSize) != lzhhdr->headerSum)
            {
                MessageBox(hwnd, "BIOS Image Checksum failed!\n\nThis BIOS Image may be corrupted or damaged.  The editor will still continue to load\n"
                                 "the image, but certain components may not be editable.", "Notice", MB_OK);
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

        SendMessage(hwnd_loadprog, PBM_SETPOS, (WPARAM)(ptr - biosdata.imageData), 0);
    }

    // check for a valid layout
    if (biosdata.layout == LAYOUT_UNKNOWN)
    {
        MessageBox(hwnd, "Unable to determine the layout of the file table within the BIOS Image!\n"
                         "It is possible that this version of the editor simply does not support this type.\n\n"
                         "Please check the homepage listed under Help->About and see if a new version is\n"
                         "available for download.", "Error", MB_OK);

        DestroyWindow(loaddlg);
        return TRUE;
    }

    // allocate our file table space...
    SetWindowText(hwnd_loadtext, "Loading File Table...");
    SendMessage(hwnd_loadprog, PBM_SETRANGE32, 0, biosdata.imageSize);
    SendMessage(hwnd_loadprog, PBM_SETPOS, 0, 0);

    biosdata.fileTable = new fileEntry[biosdata.fileCount];

    // decompress and load the file table into memory...
    ptr		= biosdata.imageData + biosdata.tableOffset;
    curFile = 0;
    done	= FALSE;

    while (!done)
    {
        lzhhdr  = (lzhHeader *)ptr;
        lzhhdra = (lzhHeaderAfterFilename *) ((lzhhdr->filename) + lzhhdr->filenameLen);

        if ((lzhhdr->headerSize == 0) || (lzhhdr->headerSize == 0xFF))
        {
            done = TRUE;
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

        SendMessage(hwnd_loadprog, PBM_SETPOS, (WPARAM)(ptr - biosdata.imageData), 0);
    }

    // calculate available table space
    biosdata.maxTableSize = (biosdata.imageSize - biosdata.tableOffset) - (decompBlockSize + bootBlockSize);

    // scan for fixed-offset components
    SetWindowText(hwnd_loadtext, "Scanning for fixed components...");

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
            SendMessage(hwnd_loadprog, PBM_SETPOS, (WPARAM)(ptr - biosdata.imageData), 0);
            nextUpdate = ptr + 1024;
        }
    }

    // insert the decompression and boot blocks
    if (decompBlockData != NULL)
    {
        fe = biosExpandTable();
        fe->nameLen = strlen("decomp_blk.bin");
        fe->name = new char[max(fe->nameLen + 1, 4)];
        strcpy_s(fe->name, max(fe->nameLen + 1, 4), "decomp_blk.bin");

        fe->size	 = decompBlockSize;
        fe->compSize = 0;
        fe->type	 = TYPEID_DECOMPBLOCK;
        fe->crc		 = 0;
        fe->crcOK	 = TRUE;
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
        fe->name = new char[max(fe->nameLen + 1, 4)];
        strcpy_s(fe->name, max(fe->nameLen + 1, 4), "boot_blk.bin");

        fe->size	 = bootBlockSize;
        fe->compSize = 0;
        fe->type	 = TYPEID_BOOTBLOCK;
        fe->crc		 = 0;
        fe->crcOK	 = TRUE;
        fe->data	 = (void *)new uchar[fe->size];
        fe->offset	 = 0;
        fe->flags	 = FEFLAGS_BOOT_BLOCK;

        memcpy(fe->data, bootBlockData, bootBlockSize);
        delete []bootBlockData;
    }

    // kill our window
    DestroyWindow(loaddlg);

    // enable all editing controls
    enableControls(TRUE, TRUE);

    // call all plugins' onLoad functions
    pluginCallOnLoad(biosdata.fileTable, biosdata.fileCount);

    // populate the component list with the files in our table
    biosUpdateComponents();

    // zap the modified flag and update our main window's title and status bar...
    biosSetModified(FALSE);

    // and cleanup our temp directory!
    cleanTempPath();

    return TRUE;
}

bool biosOpen(void)
{
    OPENFILENAME ofn;
    char fname[256], *sptr, *dptr;

    // warn if the current bios has been modified
    if (biosHandleModified(biosChangedText) == FALSE)
        return FALSE;

    // display the open dialog
    fname[0] = 0;
    ZeroMemory(&ofn, sizeof(OPENFILENAME));
    ofn.lStructSize			= sizeof(OPENFILENAME);
    ofn.hwndOwner			= hwnd;
    ofn.hInstance			= hinst;
    ofn.lpstrFilter			= "Award BIOS Image (*.awd,*.bin)\0*.awd;*.bin\0All Files (*.*)\0*.*\0\0";
    ofn.lpstrCustomFilter	= NULL;
    ofn.nMaxCustFilter		= 0;
    ofn.nFilterIndex		= 1;
    ofn.lpstrFile			= fname;
    ofn.nMaxFile			= 256;
    ofn.lpstrFileTitle		= NULL;
    ofn.nMaxFileTitle		= 0;
    ofn.lpstrInitialDir		= (config.lastPath[0] == 0) ? NULL : config.lastPath;
    ofn.lpstrTitle			= NULL;
    ofn.Flags				= OFN_FILEMUSTEXIST | OFN_ENABLESIZING | OFN_EXPLORER;
    ofn.nFileOffset			= 0;
    ofn.nFileExtension		= 0;
    ofn.lpstrDefExt			= NULL;
    ofn.lCustData			= NULL;
    ofn.lpfnHook			= NULL;
    ofn.lpTemplateName		= NULL;

    if (GetOpenFileName(&ofn) == FALSE)
        return FALSE;

    // strip out path from returned filename
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

    // zap the modified flag
    biosdata.modified = FALSE;

    // and call the open file handler...
    return biosOpenFile(fname);
}
