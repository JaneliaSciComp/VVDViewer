#ifndef _ANNOTATIONDB_H_
#define _ANNOTATIONDB_H_

// Plain data record shared by the data layer (Ruler / RulerBalloon) and the
// database dialog. Deliberately toolkit-neutral so DataManager.h does not
// have to pull in the wx-based DatabaseDlg.h.

#include "DLLExport.h"
#include "Core/VVDString.h"

class EXPORT_API AnnotationDB
{
public:
	VVDString url;
	vvd::StringArray contents;

	bool operator==(const AnnotationDB &rhs) const
	{
		return (url == rhs.url) && (contents == rhs.contents);
	}
};

#endif//_ANNOTATIONDB_H_
