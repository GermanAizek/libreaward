#ifndef TYPES_H
#define TYPES_H

typedef unsigned char		uchar;
typedef unsigned short		ushort;
typedef unsigned int		uint;
typedef unsigned long		ulong;

#ifndef I_IMAGENONE
#define I_IMAGENONE -2
#endif

#define AWDBEDIT_MAIN_CLASSNAME			"AwardEditClass"
#define AWDBEDIT_POPUP_CLASSNAME		"AwardEditPopupClass"
#define AWDBEDIT_SPLITTER_CLASSNAME		"AwardEditSplitterClass"

#define OFFSETOF(a, b)		( (ulong) &( ((a *)0)->b) )

#endif // TYPES_H
