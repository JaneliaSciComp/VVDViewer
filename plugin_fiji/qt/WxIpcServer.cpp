#include "WxIpcServer.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QEventLoop>
#include <cstring>
#include <memory>

using namespace wxipc;

namespace
{
	// wxDataOutputStream / wxDataInputStream: little-endian by default
	void Put32(QByteArray& b, quint32 v)
	{
		char c[4] = { char(v & 0xff), char((v >> 8) & 0xff), char((v >> 16) & 0xff), char((v >> 24) & 0xff) };
		b.append(c, 4);
	}
	bool Get32(const QByteArray& b, qsizetype& pos, quint32& v)
	{
		if (b.size() - pos < 4) return false;
		const unsigned char* p = (const unsigned char*)b.constData() + pos;
		v = quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
		pos += 4;
		return true;
	}
	void PutString(QByteArray& b, const QString& s)
	{
		const QByteArray u = s.toUtf8();
		Put32(b, (quint32)u.size());
		b.append(u);
	}
	// wxDataInputStream::ReadString: int32 length + bytes (UTF-8)
	bool GetString(const QByteArray& b, qsizetype& pos, QString& s)
	{
		quint32 len = 0;
		if (!Get32(b, pos, len)) return false;
		if (b.size() - pos < (qsizetype)len) return false;
		s = QString::fromUtf8(b.constData() + pos, (qsizetype)len);
		pos += len;
		return true;
	}
	bool GetData(const QByteArray& b, qsizetype& pos, QByteArray& data, quint32& size)
	{
		if (!Get32(b, pos, size)) return false;
		if (b.size() - pos < (qsizetype)size) return false;
		data = b.mid(pos, size);
		pos += size;
		return true;
	}
}

// ---------------------------------------------------------------------------
// WxIpcConnection
// ---------------------------------------------------------------------------

WxIpcConnection::WxIpcConnection(QTcpSocket* sock, const QString& topic, QObject* parent)
	: QObject(parent), m_sock(sock), m_topic(topic)
{
	if (m_sock)
	{
		m_sock->setParent(this);
		connect(m_sock, &QTcpSocket::readyRead, this, &WxIpcConnection::OnReadyRead);
		connect(m_sock, &QTcpSocket::disconnected, this, [this]() {
			// wx HandleDisconnect (wxSOCKET_LOST)
			m_connected = false;
			Q_EMIT disconnected();
		});
	}
}

WxIpcConnection::~WxIpcConnection()
{
	if (m_sock)
	{
		m_sock->disconnect(this);
		m_sock->abort();
	}
}

bool WxIpcConnection::GetConnected() const
{
	return m_connected && m_sock && m_sock->state() == QAbstractSocket::ConnectedState;
}

bool WxIpcConnection::Poke(const QString& item, const QByteArray& data, unsigned char format)
{
	if (!GetConnected()) return false;
	// IPCOutput: Write(IPC_POKE, item, format) + WriteData(data, size)
	QByteArray b;
	b.reserve(1 + 4 + item.size() + 1 + 4 + data.size());
	b.append(char(IPC_POKE));
	PutString(b, item);
	b.append(char(format));
	Put32(b, (quint32)data.size());
	b.append(data);
	return m_sock->write(b) == b.size();
}

bool WxIpcConnection::PokeString(const QString& item, const QString& s)
{
	// wx Poke(item, const wxString& s): DoPoke(item, s.utf8_str(),
	// strlen(buf) + 1, wxIPC_UTF8TEXT) -- the terminating NUL is sent
	QByteArray u = s.toUtf8();
	const int nul = u.indexOf('\0');
	if (nul >= 0) u.truncate(nul);
	u.append('\0');
	return Poke(item, u, wxIPC_UTF8TEXT);
}

bool WxIpcConnection::Disconnect()
{
	if (!GetConnected()) return true;
	WriteRaw(QByteArray(1, char(IPC_DISCONNECT)));
	m_sock->flush();
	m_sock->disconnectFromHost();
	m_connected = false;
	return true;
}

void WxIpcConnection::WriteRaw(const QByteArray& b)
{
	if (m_sock && m_sock->state() == QAbstractSocket::ConnectedState)
		m_sock->write(b);
}

bool WxIpcConnection::Flush(int msec)
{
	if (!m_sock) return false;
	QElapsedTimer t;
	t.start();
	while (m_sock && m_sock->bytesToWrite() > 0 && m_sock->state() == QAbstractSocket::ConnectedState)
	{
		if (t.elapsed() > msec) return false;
		m_sock->flush();
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
	}
	return true;
}

void WxIpcConnection::SetSndBufSize(int size)
{
	if (m_sock && size > 0)
		m_sock->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, size);
}

void WxIpcConnection::SetRcvBufSize(int size)
{
	if (m_sock && size > 0)
		m_sock->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, size);
}

void WxIpcConnection::OnReadyRead()
{
	if (!m_sock) return;
	Feed(m_sock->readAll());
}

void WxIpcConnection::Feed(const QByteArray& bytes)
{
	m_buf.append(bytes);
	while (!m_buf.isEmpty() && ParseOne())
		;
}

// wx wxTCPEventHandler::Client_OnRequest for one message
bool WxIpcConnection::ParseOne()
{
	qsizetype pos = 0;
	const unsigned char msg = (unsigned char)m_buf[0];
	pos = 1;
	bool error = false;
	QString item;
	QByteArray data;
	quint32 size = 0;
	switch (msg)
	{
	case IPC_EXECUTE:
	{
		if (m_buf.size() < pos + 1) return false;
		const int format = (unsigned char)m_buf[pos++];
		if (!GetData(m_buf, pos, data, size)) return false;
		m_buf.remove(0, pos);
		if (size == 0) error = true;   // wx ReadData: zero size -> no data
		else Q_EMIT executed(m_topic, data, format);
	}
	break;
	case IPC_ADVISE:
	{
		if (!GetString(m_buf, pos, item)) return false;
		if (m_buf.size() < pos + 1) return false;
		const int format = (unsigned char)m_buf[pos++];
		if (!GetData(m_buf, pos, data, size)) return false;
		m_buf.remove(0, pos);
		if (size == 0) error = true;
		else Q_EMIT advised(m_topic, item, data, format);
	}
	break;
	case IPC_ADVISE_START:
	case IPC_ADVISE_STOP:
	{
		if (!GetString(m_buf, pos, item)) return false;
		m_buf.remove(0, pos);
		if (msg == IPC_ADVISE_START) Q_EMIT startAdvise(m_topic, item);
		else Q_EMIT stopAdvise(m_topic, item);
		// the plugin connection's OnStartAdvise / OnStopAdvise return true
		WriteRaw(QByteArray(1, char(msg)));
	}
	break;
	case IPC_POKE:
	{
		if (!GetString(m_buf, pos, item)) return false;
		if (m_buf.size() < pos + 1) return false;
		const int format = (unsigned char)m_buf[pos++];
		if (!GetData(m_buf, pos, data, size)) return false;
		m_buf.remove(0, pos);
		if (size == 0) error = true;
		else Q_EMIT poked(m_topic, item, data, format);
	}
	break;
	case IPC_REQUEST:
	{
		if (!GetString(m_buf, pos, item)) return false;
		if (m_buf.size() < pos + 1) return false;
		pos++;   // format
		m_buf.remove(0, pos);
		// wxConnectionBase::OnRequest: no data -> IPC_FAIL
		WriteRaw(QByteArray(1, char(IPC_FAIL)));
	}
	break;
	case IPC_DISCONNECT:
		m_buf.clear();
		m_connected = false;
		if (m_sock) m_sock->disconnectFromHost();
		Q_EMIT disconnected();
		return false;
	default:
		// IPC_FAIL and unknown codes
		m_buf.remove(0, 1);
		error = true;
		break;
	}
	if (error)
		WriteRaw(QByteArray(1, char(IPC_FAIL)));
	return true;
}

// ---------------------------------------------------------------------------
// WxIpcServer
// ---------------------------------------------------------------------------

WxIpcServer::WxIpcServer(QObject* parent)
	: QObject(parent)
{
	m_server = new QTcpServer(this);
	connect(m_server, &QTcpServer::newConnection, this, &WxIpcServer::OnNewConnection);
}

WxIpcServer::~WxIpcServer()
{
	if (m_server) m_server->close();
}

bool WxIpcServer::Create(const QString& service)
{
	bool ok = false;
	const quint16 port = (quint16)service.toUInt(&ok);
	if (!ok) return false;
	return m_server->listen(QHostAddress::LocalHost, port);
}

bool WxIpcServer::IsListening() const
{
	return m_server && m_server->isListening();
}

quint16 WxIpcServer::Port() const
{
	return m_server ? m_server->serverPort() : 0;
}

// wx Server_OnRequest: IPC_CONNECT + topic -> IPC_CONNECT, else IPC_FAIL
void WxIpcServer::OnNewConnection()
{
	while (QTcpSocket* sock = m_server->nextPendingConnection())
	{
		sock->setParent(this);
		auto buf = std::make_shared<QByteArray>();
		auto handle = [this, sock, buf]() {
			buf->append(sock->readAll());
			if (buf->isEmpty()) return;
			const unsigned char msg = (unsigned char)(*buf)[0];
			QString topic;
			qsizetype pos = 1;
			if (msg == IPC_CONNECT && !GetString(*buf, pos, topic))
				return;   // wait for the rest of the topic
			const QByteArray rest = buf->mid(pos);
			sock->disconnect(this);
			if (msg == IPC_CONNECT && topic == m_topic)
			{
				sock->write(QByteArray(1, char(IPC_CONNECT)));
				auto* conn = new WxIpcConnection(sock, topic, this);
				Q_EMIT connectionAccepted(conn);
				// bytes that came in with the connect message
				if (!rest.isEmpty())
					conn->Feed(rest);
			}
			else
			{
				sock->write(QByteArray(1, char(IPC_FAIL)));
				sock->flush();
				sock->disconnectFromHost();
				sock->deleteLater();
			}
		};
		connect(sock, &QTcpSocket::readyRead, this, handle);
		connect(sock, &QTcpSocket::disconnected, this, [sock]() { sock->deleteLater(); });
		if (sock->bytesAvailable() > 0)
			handle();
	}
}
