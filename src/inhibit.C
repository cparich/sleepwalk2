#include "common.H"
#include "inhibit.H"
#include "signalHandler.H"

#include <QLocalServer>
#include <QtDBus>

#include <fmt/format.h>

#include <signal.h>

namespace
{

void writeSocket (QIODevice *const socket_, QString const message_)
{
	fmt::print (stderr, "writing '{}' to socket\n", message_.toStdString ());
	if (socket_ && socket_->isOpen ())
	{
		fmt::print (stderr, "wrote '{}' to socket\n", message_.toStdString ());
		socket_->write (QString (message_ + "\n").toLocal8Bit ());
	}
}
}

inhibit_t::~inhibit_t ()
{
}

inhibit_t::inhibit_t (QObject *const parent_)
    : QObject (parent_),
      m_privConn (QDBusConnection::connectToBus (QDBusConnection::SessionBus, "priv")),
      m_sessionBus (QDBusConnection::sessionBus ())
{
	if (!m_privConn.isConnected ())
	{
		fmt::print (
		    stderr, "Failed to connect to listener bus: {}\n", m_privConn.lastError ().message ().toStdString ());
		return;
	}

	if (!m_sessionBus.isConnected ())
	{
		fmt::print (
		    stderr, "Failed to connect to listener bus: {}\n", m_sessionBus.lastError ().message ().toStdString ());
		return;
	}

	connect (signalHandler_t::instance (SIGTERM), &signalHandler_t::raised, this, [] { qApp->exit (EXIT_SUCCESS); });

	connect (qApp, &QCoreApplication::aboutToQuit, this, [] { QDBusConnection::disconnectFromBus ("priv"); });

	new inhibitAdapter_t (this);
	if (!m_privConn.registerObject ("/org/freedesktop/Notifications",
	        "org.freedesktop.Notifications",
	        this,
	        QDBusConnection::ExportAdaptors | QDBusConnection::ExportAllContents))
		fmt::print (stderr, "failed to register object\n");
#if 0
	m_privConn.registerService ("org.sleepwalk.Notifications");
#endif

	m_notificationInterface = new QDBusInterface (
	    "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus.Monitoring", m_privConn, this);

	m_notificationInterface->call ("BecomeMonitor",
	    QStringList{"type='method_call',"
	                "member='Notify',"
	                "path='/org/freedesktop/Notifications',"
	                "interface='org.freedesktop.Notifications'"},
	    0u);

	m_inhibitInterface = new QDBusInterface ("org.gnome.SessionManager",
	    "/org/gnome/SessionManager",
	    "org.gnome.SessionManager",
	    QDBusConnection::sessionBus (),
	    this);

	// clang-format off
	connect (m_inhibitInterface,
	    SIGNAL (InhibitorAdded(QDBusObjectPath)),
	    this,
	    SLOT (handleInhibitAdded(QDBusObjectPath)));
	connect (m_inhibitInterface,
	    SIGNAL (InhibitorRemoved(QDBusObjectPath)),
	    this,
	    SLOT (handleInhibitRemoved(QDBusObjectPath)));
	// clang-format on

	QDBusReply<QList<QDBusObjectPath>> const reply = m_inhibitInterface->call (QDBus::Block, "GetInhibitors");

	if (reply.isValid ())
	{
		auto const value = reply.value ();
		for (auto const &p : value)
			m_inhibitors.insert (p.path ());
	}

	m_socket = new QLocalSocket (this);
	connect (m_socket, &QLocalSocket::stateChanged, this, &inhibit_t::handleStateChanged);
	m_socket->connectToServer (SOCKET_NAME);

	connect (&m_messageTimer, &QTimer::timeout, this, &inhibit_t::handleMessageTimer);
	m_messageTimer.setInterval (1000);
	m_messageTimer.start ();
}

void inhibit_t::Notify (QString arg_0,
    unsigned arg_1,
    QString arg_2,
    QString arg_3,
    QString arg_4,
    QStringList arg_5,
    QVariantMap arg_6,
    int arg_7,
    const QDBusMessage &m_,
    unsigned &arg_8)
{
	Q_UNUSED (arg_0);
	Q_UNUSED (arg_1);
	Q_UNUSED (arg_2);
	Q_UNUSED (arg_3);
	Q_UNUSED (arg_4);
	Q_UNUSED (arg_5);
	Q_UNUSED (arg_6);
	Q_UNUSED (arg_7);
	Q_UNUSED (arg_8);

	writeSocket (m_socket, NOTIFY_STRING);

	m_.setDelayedReply (true);
}

void inhibit_t::handleInhibitAdded (QDBusObjectPath const path_)
{
	m_inhibitors.insert (path_.path ());
	handleMessageTimer ();
	m_messageTimer.start ();
}

void inhibit_t::handleInhibitRemoved (QDBusObjectPath const path_)
{
	m_inhibitors.remove (path_.path ());
	handleMessageTimer ();
	m_messageTimer.start ();
}

void inhibit_t::handleStateChanged ()
{
	fmt::print (stderr, "Socket state changed: {}\n", static_cast<unsigned> (m_socket->state ()));
	if (m_socket->state () == QLocalSocket::UnconnectedState)
	{
		fmt::print (stderr, "Socket error: {}\n", m_socket->errorString ().toStdString ());
		QTimer::singleShot (1000, this, [this] { m_socket->connectToServer (SOCKET_NAME); });
	}
}

void inhibit_t::handleMessageTimer ()
{
	if (!m_inhibitors.empty ())
		writeSocket (m_socket, INHIBIT_STRING);
}
