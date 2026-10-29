#ifndef VVD_QT_WXIPCSERVER_H
#define VVD_QT_WXIPCSERVER_H

// The server side of wxWidgets' TCP IPC protocol (wxTCPServer /
// wxTCPConnection, src/common/sckipc.cpp of wx 3.x), on QTcpServer.
//
// The Fiji side (plugin_fiji/fiji/plugins/vvd_listener.java) is a client of
// that protocol written by hand, so the server must speak it byte for byte:
//
//   integers: 32-bit little-endian (wxDataOutputStream's default byte
//   order; the Java side uses the native order), strings: int32 byte count
//   + the UTF-8 bytes (no NUL), formats: one byte (wxIPCFormat)
//
//   client -> server, once:   IPC_CONNECT(10) string topic
//   server reply:             IPC_CONNECT(10) when accepted, else IPC_FAIL(9)
//                             and the socket is closed
//   then, either way:
//   IPC_POKE(3)          string item, byte format, int32 size, size bytes
//   IPC_EXECUTE(1)       byte format, int32 size, size bytes
//   IPC_ADVISE(6)        string item, byte format, int32 size, size bytes
//   IPC_ADVISE_START(4)  string item   -> reply IPC_ADVISE_START (accepted)
//   IPC_ADVISE_STOP(7)   string item   -> reply IPC_ADVISE_STOP
//   IPC_REQUEST(2)       string item, byte format -> reply IPC_FAIL (wx
//                        wxConnectionBase::OnRequest returns no data)
//   IPC_DISCONNECT(11)   the connection is closed
//   anything else, or a data block of size 0: reply IPC_FAIL
//
// Poke(item, data, format) sends IPC_POKE with the same layout;
// PokeString(item, s) is wx Poke(item, const wxString&): format
// wxIPC_UTF8TEXT (17) and the UTF-8 bytes *with* their terminating NUL
// (wxConnectionBase::GetTextLength). A whole message is written in one
// QTcpSocket::write (wx buffers it the same way, IPCOutput).

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QPointer>
#include <QTcpSocket>

class QTcpServer;

namespace wxipc
{
	enum Code : unsigned char
	{
		IPC_EXECUTE = 1, IPC_REQUEST = 2, IPC_POKE = 3, IPC_ADVISE_START = 4, IPC_ADVISE_REQUEST = 5,
		IPC_ADVISE = 6, IPC_ADVISE_STOP = 7, IPC_REQUEST_REPLY = 8, IPC_FAIL = 9, IPC_CONNECT = 10,
		IPC_DISCONNECT = 11
	};
	// wxIPCFormat values used here (wx/ipcbase.h)
	enum Format : unsigned char { wxIPC_TEXT = 1, wxIPC_UTF8TEXT = 17, wxIPC_PRIVATE = 20 };
}

// one accepted connection (wxTCPConnection)
class WxIpcConnection : public QObject
{
	Q_OBJECT
public:
	WxIpcConnection(QTcpSocket* sock, const QString& topic, QObject* parent = nullptr);
	~WxIpcConnection() override;

	bool GetConnected() const;
	QString GetTopic() const { return m_topic; }
	QTcpSocket* Socket() const { return m_sock; }

	// wx DoPoke / Poke(item, const wxString&)
	bool Poke(const QString& item, const QByteArray& data, unsigned char format = wxipc::wxIPC_PRIVATE);
	bool PokeString(const QString& item, const QString& s);
	// wx Disconnect: IPC_DISCONNECT, close
	bool Disconnect();
	// raw bytes (wx code wrote m_sock directly, e.g. the plugin connection's
	// destructor writes one IPC_DISCONNECT byte)
	void WriteRaw(const QByteArray& b);
	// bytes received (normally from the socket)
	void Feed(const QByteArray& bytes);
	// waits (event loop running, user input excluded) until the socket has
	// handed everything written to the OS or msec elapsed
	bool Flush(int msec = 30000);

	// wx socket options (SetTimeout: blocking-read timeout of the wx socket;
	// kept for GetTimeout, reads here never block)
	void SetTimeout(long seconds) { m_timeout = seconds; }
	long GetTimeout() const { return m_timeout; }
	void SetSndBufSize(int size);
	void SetRcvBufSize(int size);

Q_SIGNALS:
	// a complete IPC_POKE (wx OnPoke)
	void poked(const QString& topic, const QString& item, const QByteArray& data, int format);
	void executed(const QString& topic, const QByteArray& data, int format);
	void advised(const QString& topic, const QString& item, const QByteArray& data, int format);
	void startAdvise(const QString& topic, const QString& item);
	void stopAdvise(const QString& topic, const QString& item);
	void disconnected();

private:
	void OnReadyRead();
	bool ParseOne();   // false: need more bytes
	QPointer<QTcpSocket> m_sock;
	QString m_topic;
	QByteArray m_buf;
	long m_timeout = 600;   // wxSocketBase default
	bool m_connected = true;
};

// wxTCPServer
class WxIpcServer : public QObject
{
	Q_OBJECT
public:
	explicit WxIpcServer(QObject* parent = nullptr);
	~WxIpcServer() override;
	// wx Create(serverName): a port number (wx: "8002"); listens on the
	// loopback interface (wx: every interface)
	bool Create(const QString& service);
	bool IsListening() const;
	quint16 Port() const;
	// topic accepted (wx OnAcceptConnection returning a connection)
	void SetAcceptedTopic(const QString& topic) { m_topic = topic; }

Q_SIGNALS:
	// wx OnAcceptConnection succeeded; the connection is a child of the server
	void connectionAccepted(WxIpcConnection* connection);

private:
	void OnNewConnection();
	QTcpServer* m_server = nullptr;
	QString m_topic;
};

#endif // VVD_QT_WXIPCSERVER_H
