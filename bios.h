#include <stdint.h>
#include <cstdio>

#ifndef BIOS_H
#define BIOS_H

#define APP_NAME                    "LibreAward BIOS"
#define APP_REV                     "1.0.3"
#define APP_VERSION                 APP_NAME " " APP_REV


#define HASH_UNKNOWN_ITEM_MAX		0x000000F8

#define HASH_SUBMENU_ITEM			0x000000FB
#define HASH_RECOGNIZED_ROOT		0x000000FC
#define HASH_UNKNOWN_ROOT			0x000000FD
#define HASH_INCLUDABLE_ROOT		0x000000FE

#define HASH_RESERVED				0x000000FF


class Bios
{
public:
    Bios();
    void biosUpdateCurrentDialog(void);
    void biosRefreshCurrentDialog(void);
    bool biosHandleModified(char *text);

    //void biosInit(HINSTANCE hi, HWND hw, HWND statwnd, HWND treewnd, HTREEITEM recgitem, HTREEITEM inclitem, HTREEITEM unkitem, RECT *dlgrect);
    void biosFreeMemory(void);
    void biosTitleUpdate(void);

    char *biosGetFilename(void);
    //HWND biosGetDialog(void);

    bool biosOpenFile(char *fname);
    bool biosOpen(void);
    bool biosSave(void);
    bool biosSaveAs(void);
    void biosProperties(void);
    void biosRevert(void);

    void biosInsert(uint16_t typeID);
    void biosReplace(void);
    void biosExtract(void);
    void biosExtractAll(void);
    void biosRemove(void);
    void biosHexEdit(void);

    //void biosItemChanged(LPNMTREEVIEW lpnmtv);

    //void biosGetDialogSize(SIZE *sz);
    //void biosResizeDialog(SIZE sz);

    //fileEntry *biosScanForID(uint16_t id);

    void biosSetModified(bool val);
    //awdbeBIOSVersion biosGetVersion(void);

    //void biosResizeCurrentDialog(HWND hwnd, RECT *rc);
};

#endif // BIOS_H
