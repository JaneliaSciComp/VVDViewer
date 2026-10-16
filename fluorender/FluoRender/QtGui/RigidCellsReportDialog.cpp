// See RigidCellsReportDialog.h.

#include "RigidCellsReportDialog.h"

#include <QFileDialog>
#include <QFile>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>
#include <QFontMetrics>

namespace
{
	// numbers with 6 significant digits on screen (the CSV keeps full precision)
	QString Display(const std::string& cell)
	{
		bool ok = false;
		const QString s = QString::fromStdString(cell);
		const double v = s.toDouble(&ok);
		if (!ok || s.contains(QLatin1Char('"')))
			return s;
		if (v == double((long long)v) && std::abs(v) < 1e15 && !s.contains(QLatin1Char('.')) && !s.contains(QLatin1Char('e')))
			return s;
		return QString::number(v, 'g', 6);
	}

	void Fill(QTableWidget* t, const std::vector<std::string>& cols, const std::vector<std::vector<std::string>>& rows)
	{
		t->clear();
		t->setColumnCount((int)cols.size());
		t->setRowCount((int)rows.size());
		QStringList hdr;
		for (const std::string& c : cols) hdr << QString::fromStdString(c);
		t->setHorizontalHeaderLabels(hdr);
		for (int r = 0; r < (int)rows.size(); ++r)
			for (int c = 0; c < (int)rows[r].size(); ++c)
			{
				auto* it = new QTableWidgetItem(c == 0 ? QString::fromStdString(rows[r][c]) : Display(rows[r][c]));
				if (c > 0) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
				if (c == 0) it->setToolTip(QString::fromStdString(rows[r][c]));
				t->setItem(r, c, it);
			}
		t->resizeColumnsToContents();
		// the column names must stay readable (the header font can be larger than the cells')
		const QFontMetrics fm(t->horizontalHeader()->font());
		for (int c = 0; c < t->columnCount(); ++c)
			t->setColumnWidth(c, std::max(t->columnWidth(c), fm.horizontalAdvance(hdr[c]) + 24));
		if (t->columnCount() > 0 && t->columnWidth(0) > 260)
			t->setColumnWidth(0, 260);
	}

	QTableWidget* MakeTable(QWidget* parent)
	{
		auto* t = new QTableWidget(parent);
		t->setEditTriggers(QAbstractItemView::NoEditTriggers);
		t->setSelectionBehavior(QAbstractItemView::SelectRows);
		t->setAlternatingRowColors(true);
		t->verticalHeader()->setDefaultSectionSize(20);
		return t;
	}
}

RigidCellsReportDialog::RigidCellsReportDialog(QWidget* parent)
	: QDialog(parent)
{
	setWindowTitle("RigidCells results");
	m_summary = new QLabel(this);
	m_summary->setWordWrap(true);

	auto* objBox = new QGroupBox("RigidCells objects", this);
	m_objects = MakeTable(objBox);
	auto* saveObj = new QPushButton("Save CSV...", objBox);
	saveObj->setObjectName("rc_save_objects");
	auto* objLay = new QVBoxLayout(objBox);
	objLay->addWidget(m_objects, 1);
	auto* objBtn = new QHBoxLayout();
	objBtn->addStretch(1);
	objBtn->addWidget(saveObj);
	objLay->addLayout(objBtn);

	auto* lmBox = new QGroupBox("RigidCells landmarks (leave-one-out)", this);
	m_landmarks = MakeTable(lmBox);
	m_save_lm = new QPushButton("Save CSV...", lmBox);
	m_save_lm->setObjectName("rc_save_landmarks");
	auto* lmLay = new QVBoxLayout(lmBox);
	lmLay->addWidget(m_landmarks, 1);
	auto* lmBtn = new QHBoxLayout();
	lmBtn->addStretch(1);
	lmBtn->addWidget(m_save_lm);
	lmLay->addLayout(lmBtn);

	auto* logBox = new QGroupBox("Log", this);
	m_log = new QPlainTextEdit(logBox);
	m_log->setReadOnly(true);
	m_log->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	auto* logLay = new QVBoxLayout(logBox);
	logLay->addWidget(m_log);

	auto* split = new QSplitter(Qt::Vertical, this);
	split->addWidget(objBox);
	split->addWidget(lmBox);
	split->addWidget(logBox);
	split->setStretchFactor(0, 3);
	split->setStretchFactor(1, 3);
	split->setStretchFactor(2, 1);

	auto* lay = new QVBoxLayout(this);
	lay->addWidget(m_summary);
	lay->addWidget(split, 1);

	connect(saveObj, &QPushButton::clicked, this, &RigidCellsReportDialog::OnSaveObjects);
	connect(m_save_lm, &QPushButton::clicked, this, &RigidCellsReportDialog::OnSaveLandmarks);
	resize(1000, 700);
}

void RigidCellsReportDialog::SetReport(const rigidcells::Report& report, const QString& title)
{
	m_report = report;
	m_title = title;
	setWindowTitle("RigidCells results - " + title);
	m_summary->setText(QString("%1: %2 object(s) in %3 runs, TPS lambda %4, forward landmark RMS %5, %6 warning(s), %7 s%8")
		.arg(title).arg(report.components).arg(report.runs).arg(report.lambda, 0, 'g', 6).arg(report.forward_rms, 0, 'g', 4)
		.arg((int)report.warnings.size()).arg(report.seconds, 0, 'f', 2)
		.arg(report.device.empty() ? QString() : QString(", adaptive field: ") + QString::fromStdString(report.device)));
	std::vector<std::vector<std::string>> rows;
	for (const rigidcells::ObjectRow& r : report.objects) rows.push_back(rigidcells::Report::ObjectCells(r));
	Fill(m_objects, rigidcells::Report::ObjectColumns(), rows);
	rows.clear();
	for (const rigidcells::LandmarkRow& r : report.landmarks) rows.push_back(rigidcells::Report::LandmarkCells(r));
	Fill(m_landmarks, rigidcells::Report::LandmarkColumns(), rows);
	m_landmarks->setEnabled(report.has_landmarks);
	m_save_lm->setEnabled(report.has_landmarks);
	QStringList log;
	for (const std::string& l : report.log) log << QString::fromStdString(l);
	m_log->setPlainText(log.join("\n"));
}

static bool WriteText(const QString& path, const std::string& text)
{
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return false;
	return f.write(text.data(), (qint64)text.size()) == (qint64)text.size();
}

bool RigidCellsReportDialog::SaveObjectsCsv(const QString& path) const
{
	return WriteText(path, m_report.ObjectsCsv());
}

bool RigidCellsReportDialog::SaveLandmarksCsv(const QString& path) const
{
	return m_report.has_landmarks && WriteText(path, m_report.LandmarksCsv());
}

void RigidCellsReportDialog::OnSaveObjects()
{
	QString f = QFileDialog::getSaveFileName(this, "Save objects table", m_title + "_objects.csv", "CSV (*.csv)");
	if (!f.isEmpty()) SaveObjectsCsv(f);
}

void RigidCellsReportDialog::OnSaveLandmarks()
{
	QString f = QFileDialog::getSaveFileName(this, "Save landmarks table", "landmarks_leave_one_out.csv", "CSV (*.csv)");
	if (!f.isEmpty()) SaveLandmarksCsv(f);
}
