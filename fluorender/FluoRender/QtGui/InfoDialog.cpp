#include "InfoDialog.h"
#include "AnalysisContext.h"

#include "DataManager.h"

#include <QBoxLayout>
#include <QEvent>
#include <QLabel>
#include <QPlainTextEdit>

InfoDialog::InfoDialog(AnalysisContext* ctx, QWidget* parent) :
	QDialog(parent),
	m_ctx(ctx),
	m_data(nullptr)
{
	setWindowTitle(tr("Information"));
	setObjectName("VAnnoView");
	setModal(false);
	resize(400, 300);

	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(10, 10, 10, 10);
	root->setSpacing(4);

	root->addWidget(new QLabel(tr("Info:"), this), 0, Qt::AlignLeft);

	m_text = new QPlainTextEdit(this);
	m_text->setLineWrapMode(QPlainTextEdit::NoWrap);   // wxHSCROLL
	m_text->installEventFilter(this);
	root->addWidget(m_text, 1);
}

bool InfoDialog::eventFilter(QObject* obj, QEvent* ev)
{
	if (obj == m_text)
	{
		if (ev->type() == QEvent::FocusIn)
			Q_EMIT editingChanged(true);
		else if (ev->type() == QEvent::FocusOut)
		{
			SaveInfo();
			Q_EMIT editingChanged(false);
		}
	}
	return QDialog::eventFilter(obj, ev);
}

bool InfoDialog::IsEditing() const
{
	return m_text->hasFocus();
}

QString InfoDialog::GetText() const
{
	return m_text->toPlainText();
}

void InfoDialog::SaveInfo()
{
	if (!m_data) return;

	switch (m_data->IsA())
	{
	case 2: //volume
		if (BaseReader* reader = static_cast<VolumeData*>(m_data)->GetReader())
			reader->SetInfo(m_text->toPlainText().toStdWString());
		break;
	case 3: //mesh: no info (wx leaves this commented out)
		break;
	}
}

void InfoDialog::SetData(TreeLayer* data)
{
	SaveInfo();

	m_data = data;
	m_text->clear();

	if (!m_data) return;

	switch (m_data->IsA())
	{
	case 2: //volume
		if (BaseReader* reader = static_cast<VolumeData*>(m_data)->GetReader())
			m_text->setPlainText(QString::fromStdWString(reader->GetInfo()));
		break;
	case 3: //mesh
		break;
	}
}

void InfoDialog::SetVolume(VolumeData* vd)
{
	SetData(vd);
}

void InfoDialog::showEvent(QShowEvent* e)
{
	// pick up the current selection when opened from the Analyze menu
	if (m_ctx)
	{
		VolumeData* vd = m_ctx->CurrentVolume();
		if (vd != m_data)
			SetData(vd);
	}
	QDialog::showEvent(e);
}

void InfoDialog::hideEvent(QHideEvent* e)
{
	SaveInfo();
	Q_EMIT editingChanged(false);
	QDialog::hideEvent(e);
}
