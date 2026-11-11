#ifndef VVD_QT_STYLESHEETS_H
#define VVD_QT_STYLESHEETS_H

#include <QApplication>
#include <QPalette>
#include <QString>

// For a style sheet rule that gives QComboBox a background: the colors of
// the item under the pointer in the combo's list. QStyleSheetStyle paints
// those items (QComboMenuDelegate, CE_MenuItem) with the combo's palette and
// replaces its Highlight with the background of the rule, so that item was
// white on white (with a "color" in the rule: no highlight at all). These
// are the menu colors, as in the list of an unstyled combo box (macOS: the
// application palette has the light text-selection colors).
inline QString VVDQtComboSelectionColors()
{
	const QPalette pal = QApplication::palette("QMenu");
	return QStringLiteral(" selection-background-color: %1; selection-color: %2;")
		.arg(pal.color(QPalette::Active, QPalette::Highlight).name(),
			pal.color(QPalette::Active, QPalette::HighlightedText).name());
}

#endif
