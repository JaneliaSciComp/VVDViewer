#ifndef VVD_QT_RIGIDCELLSREPORTDIALOG_H
#define VVD_QT_RIGIDCELLSREPORTDIALOG_H

// Results of the Measurement "Cells: shape preserving" transform: the
// plugin's "RigidCells objects" and "RigidCells landmarks" tables (same
// column names, row label first as "Label"), each with Save CSV (full
// precision), and the log (warnings included).

#include <QDialog>
#include "RigidCellsWarp.h"

class QTableWidget;
class QPlainTextEdit;
class QLabel;
class QPushButton;

class RigidCellsReportDialog : public QDialog
{
	Q_OBJECT
public:
	explicit RigidCellsReportDialog(QWidget* parent = nullptr);
	// shows the tables of one run; title = the result volume's name
	void SetReport(const rigidcells::Report& report, const QString& title);
	const rigidcells::Report& GetReport() const { return m_report; }
	// write a table as CSV (the Save CSV buttons without the file dialog)
	bool SaveObjectsCsv(const QString& path) const;
	bool SaveLandmarksCsv(const QString& path) const;

private Q_SLOTS:
	void OnSaveObjects();
	void OnSaveLandmarks();

private:
	rigidcells::Report m_report;
	QString m_title;
	QLabel* m_summary;
	QTableWidget* m_objects;
	QTableWidget* m_landmarks;
	QPushButton* m_save_lm;
	QPlainTextEdit* m_log;
};

#endif // VVD_QT_RIGIDCELLSREPORTDIALOG_H
