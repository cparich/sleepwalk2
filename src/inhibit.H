#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QLocalSocket>
#include <QObject>
#include <QSet>
#include <QtDBus>

class inhibit_t : public QObject
{
	Q_OBJECT

public:
	~inhibit_t () override;

	inhibit_t (QObject *parent_ = nullptr);

    void Notify (QString sender_,
        unsigned a_,
        QString b_,
        QString c_,
        QString d_,
        QStringList e_,
        QVariantMap arg_6,
        int g_,
        QDBusMessage const &m_,
        unsigned &arg_8);

private slots:
	void handleInhibitAdded (QDBusObjectPath path_);
	void handleInhibitRemoved (QDBusObjectPath path_);

private:
	void handleStateChanged ();
	void handleMessageTimer ();

	QTimer m_messageTimer;
	QSet<QString> m_inhibitors;
	QLocalSocket *m_socket                  = nullptr;
	QDBusInterface *m_inhibitInterface      = nullptr;
	QDBusInterface *m_notificationInterface = nullptr;
	QDBusConnection m_privConn;
	QDBusConnection m_sessionBus;
};

class inhibitAdapter_t : public QDBusAbstractAdaptor
{
	Q_OBJECT
	Q_CLASSINFO ("D-Bus Interface", "org.freedesktop.Notifications")

public:
	~inhibitAdapter_t () override = default;

	inhibitAdapter_t (inhibit_t *const parent_) : QDBusAbstractAdaptor (parent_)
	{
	}

public slots:
	Q_NOREPLY
	void Notify (QString arg_0,
	    unsigned arg_1,
	    QString arg_2,
	    QString arg_3,
	    QString arg_4,
	    QStringList arg_5,
        QVariantMap arg_6,
	    int arg_7,
	    QDBusMessage m_,
	    unsigned &arg_8)
	{
        parent ()->Notify (arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, m_, arg_8);
	}

private:
	inhibit_t *parent () const
	{
		return qobject_cast<inhibit_t *> (QObject::parent ());
	}
};
