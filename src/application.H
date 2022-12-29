#pragma once

#include "sm.H"

#include <QDBusInterface>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QSettings>
#include <QTimer>

class application_t : public QObject
{
	Q_OBJECT
public:
	enum hw_t
	{
		PSU,
		DISPLAY,
		KEYBOARD,
		LED_RED,
		LED_BLUE,
		LED_GREEN,
		SLEEP,
		COUNT,
	};

	~application_t () override;

	explicit application_t (QObject *parent = nullptr);

private slots:
	void handleJobRemoved (unsigned id_, QDBusObjectPath object_, QString unit_, QString result_);

private:
	/// \brief fork connected to socket
	void handleConnect ();
	void handleDisconnect ();
	void handleInhibitData ();

	void handleAwakeState ();
	void handleNotifyState ();
	void handleSleepState ();
	void handleSleepwalkState ();

	/// \brief got killed
	void handleKill ();
	/// \brief got terminated
	void handleTerm ();
	/// \brief reload config
	void handleHup ();

	/// \brief handle LED state
	void handleLED ();

	/// \brief read power status
	void readPower ();

	/// \brief read display status
	void readDisplay ();

	/// \brief calculate sleep seconds
	int sleepSeconds ();

	std::array<char const *, hw_t::COUNT> const &m_hw;

	sm_t m_sm;
	std::size_t m_state;

	QTimer m_pollTimer;
	QTimer m_ledTimer;
	QTimer m_sleepTimer;

	QSettings &m_settings;

	int m_sleepCycle = 0;

	QLocalServer *m_server = nullptr;
	QLocalSocket *m_child  = nullptr;

	QDBusInterface *m_unitbus    = nullptr;
	QDBusInterface *m_managerbus = nullptr;
	QDBusObjectPath m_lastJobPath;
};
