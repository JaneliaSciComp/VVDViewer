#ifndef VVD_QT_TESTRUN_H
#define VVD_QT_TESTRUN_H

#include <QtGlobal>
#include <QString>

// True for scripted test runs (VVD_QT_CAPTURE / _GRAB / _MOVIE / _FILEDLG_RUN
// / _TEST / _GRAB_LEGEND). Such runs must not write the settings file, the window state or
// the default_*.dft files.
inline bool VVDQtScriptedRun()
{
	return qEnvironmentVariableIsSet("VVD_QT_CAPTURE") || qEnvironmentVariableIsSet("VVD_QT_GRAB") ||
		qEnvironmentVariableIsSet("VVD_QT_MOVIE") || qEnvironmentVariableIsSet("VVD_QT_FILEDLG_RUN") ||
		qEnvironmentVariableIsSet("VVD_QT_TEST") || qEnvironmentVariableIsSet("VVD_QT_GRAB_LEGEND");
}

// Scripted tests of MainWindowPaintTest.cpp (VVD_QT_TEST=<kind>): they save
// their own captures and quit by themselves.
inline bool VVDQtPaintTestKind(const QString& kind)
{
	return kind == "paint" || kind == "calc" || kind == "seg" || kind == "index" || kind == "nr" ||
		kind == "hide" || kind == "ctx" || kind == "ui" || kind == "diffuse" || kind == "rulervol" || kind == "rulerauto" ||
		kind == "rulerlist" || kind == "warp" || kind == "warptarget" || kind == "cells" || kind == "view3d" || kind == "na" || kind == "legend2" ||
		kind == "layout" ||   // MainWindowLayout.cpp
		kind == "cdm" ||      // MainWindowCDM.cpp
		kind.startsWith("b3_") ||   // MainWindowBatch3Test.cpp
		kind.startsWith("b4_") ||   // MainWindowBatch4Test.cpp
		kind.startsWith("b4b_");    // MainWindowPluginTest.cpp
}

#endif
