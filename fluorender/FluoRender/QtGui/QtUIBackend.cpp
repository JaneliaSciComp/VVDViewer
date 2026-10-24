#include "QtUIBackend.h"

#include <QApplication>
#include <QMessageBox>
#include <QProgressDialog>
#include <QThread>

namespace
{
	class QtProgress : public vvd::Progress
	{
	public:
		QtProgress(const QString& title, const QString& message, int maximum, int flags)
			: m_dlg(message, (flags & vvd::PROGRESS_CAN_ABORT) ? QObject::tr("Cancel") : QString(), 0, maximum)
		{
			m_dlg.setWindowTitle(title);
			m_dlg.setWindowModality((flags & vvd::PROGRESS_APP_MODAL) ? Qt::ApplicationModal : Qt::NonModal);
			m_dlg.setMinimumDuration(0);
			m_dlg.setAutoClose(false);
			m_dlg.setAutoReset(false);
			m_dlg.setValue(0);
			QApplication::processEvents();
		}
		bool Update(int value, const VVDString& message) override
		{
			if (!message.empty()) m_dlg.setLabelText(message.ToQt());
			m_dlg.setValue(value);
			QApplication::processEvents();
			return !m_dlg.wasCanceled();
		}
		bool Pulse(const VVDString& message) override
		{
			if (!message.empty()) m_dlg.setLabelText(message.ToQt());
			if (m_dlg.maximum() != 0) m_dlg.setRange(0, 0);   // busy indicator
			QApplication::processEvents();
			return !m_dlg.wasCanceled();
		}
		void SetRange(int maximum) override { m_dlg.setRange(0, maximum); }
		bool WasCancelled() const override { return m_dlg.wasCanceled(); }
	private:
		QProgressDialog m_dlg;
	};

	bool OnGuiThread()
	{
		return QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread();
	}
}

int QtUIBackend::MessageBox(const VVDString& message, const VVDString& caption, int flags)
{
	if (!OnGuiThread())
	{
		// Never create dialogs from worker threads.
		qWarning("%s: %s", caption.ToStdString().c_str(), message.ToStdString().c_str());
		return (flags & vvd::MSG_YES_NO) ? vvd::MSG_RESULT_NO : vvd::MSG_RESULT_OK;
	}

	QMessageBox::Icon icon = QMessageBox::Information;
	if (flags & vvd::MSG_ICON_ERROR) icon = QMessageBox::Critical;
	else if (flags & vvd::MSG_ICON_WARNING) icon = QMessageBox::Warning;
	else if (flags & vvd::MSG_ICON_QUESTION) icon = QMessageBox::Question;

	QMessageBox::StandardButtons buttons = QMessageBox::Ok;
	if (flags & vvd::MSG_YES_NO) buttons = QMessageBox::Yes | QMessageBox::No;
	if (flags & vvd::MSG_CANCEL) buttons |= QMessageBox::Cancel;

	QMessageBox box(icon, caption.ToQt(), message.ToQt(), buttons, QApplication::activeWindow());
	if ((flags & vvd::MSG_YES_NO) && (flags & vvd::MSG_NO_DEFAULT))
		box.setDefaultButton(QMessageBox::No);
	int r = box.exec();
	switch (r)
	{
	case QMessageBox::Yes: return vvd::MSG_RESULT_YES;
	case QMessageBox::No: return vvd::MSG_RESULT_NO;
	case QMessageBox::Cancel: return vvd::MSG_RESULT_CANCEL;
	default: return vvd::MSG_RESULT_OK;
	}
}

std::unique_ptr<vvd::Progress> QtUIBackend::CreateProgress(const VVDString& title, const VVDString& message,
	int maximum, int flags)
{
	if (!OnGuiThread())
		return nullptr;   // vvd::UI substitutes a silent progress object
	return std::unique_ptr<vvd::Progress>(new QtProgress(title.ToQt(), message.ToQt(), maximum, flags));
}

void QtUIBackend::Yield()
{
	if (OnGuiThread()) QApplication::processEvents();
}

void QtUIBackend::Install()
{
	static QtUIBackend backend;
	vvd::UI::SetBackend(&backend);
}
