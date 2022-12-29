#include "application.H"
#include "common.H"
#include "signalHandler.H"

#include <fmt/format.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QtDBus>

#include <array>

#include <fcntl.h>
#include <linux/rtc.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <time.h>

namespace
{
static constexpr std::array<char const *, application_t::COUNT> PPP{
	"/sys/class/power_supply/rk818-usb/present",
	"/sys/class/backlight/backlight/bl_power",
	"/sys/class/power_supply/ip5xxx-usb/present",
	"/sys/class/leds/red:indicator/brightness",
	"/sys/class/leds/blue:indicator/brightness",
	"/sys/class/leds/green:indicator/brightness",
	"/sys/power/state",
};
static constexpr std::array<char const *, application_t::COUNT> PPOG{
	"/sys/class/power_supply/axp20x-usb/online",
	"/sys/class/backlight/backlight/bl_power",
	"/sys/class/power_supply/ip5xxx-usb/present",
	"/sys/class/leds/red:indicator/brightness",
	"/sys/class/leds/blue:indicator/brightness",
	"/sys/class/leds/green:indicator/brightness",
	"/sys/power/state",
};

auto constexpr POLL_INTERVAL      = 100;
auto constexpr LED_INTERVAL       = 1000;
auto constexpr DEFAULT_SLEEP_TIME = 600000;
auto constexpr DEFAULT_WAKE_TIME  = 60000;
auto constexpr SLEEP_TIME         = "sleep_time";
auto constexpr WAKE_TIME          = "wake_time";
auto constexpr DEVICE_CLASS_FILE  = "/sys/firmware/devicetree/base/compatible";
auto constexpr CONFIG_FILE        = "/etc/default/sleepwalk2";

enum stateType_t
{
	STATE_AWAKE,
	STATE_NOTIFY,
	STATE_SLEEP,
	STATE_SLEEPWALK,
};

enum eventType_t
{
	EVENT_NOTIFY,
	EVENT_SLEEP,
	EVENT_SLEEPWALK,
	EVENT_WAKEUP,
};

std::array<char const *, application_t::COUNT> const &pickDevice ()
{
	{
		QFile f (DEVICE_CLASS_FILE);
		if (f.open (QFile::ReadOnly))
		{
			auto const data = f.readAll ();
			if (data.contains ("pinephone-pro"))
			{
				fmt::print (stderr, "Loading configs for PinePhone Pro\n");
				return PPP;
			}
			if (data.contains ("pinephone"))
			{
				fmt::print (stderr, "Loading configs for PinePhone\n");
				return PPOG;
			}
		}
	}
	{
		QFileInfo fi (PPP[application_t::PSU]);
		if (fi.exists ())
		{
			fmt::print (stderr, "Loading configs for PinePhone Pro\n");
			return PPP;
		}
	}
	{
		QFileInfo fi (PPOG[application_t::PSU]);
		if (fi.exists ())
		{
			fmt::print (stderr, "Loading configs for PinePhone\n");
			return PPOG;
		}
	}

	throw fmt::format ("Failed to pick device");
}

QSettings &prepareSettings ()
{
	static QSettings s (CONFIG_FILE, QSettings::IniFormat);

	auto const keys = s.allKeys ();

	if (!keys.contains (SLEEP_TIME))
		s.setValue (SLEEP_TIME, DEFAULT_SLEEP_TIME);

	if (!keys.contains (WAKE_TIME))
		s.setValue (WAKE_TIME, DEFAULT_WAKE_TIME);

	return s;
}

/// \brief Setup wake alarm on rtc0
/// \param wakeTime_
/// \return true on alarm set, false otherwise
/// \note adapted from https://github.com/util-linux/util-linux/blob/master/sys-utils/rtcwake.c
bool setupWakeAlarm (QDateTime const wakeTime_)
{
	auto constexpr RTC0_PATH = "/dev/rtc0";
	auto const rtc0          = open (RTC0_PATH, O_RDONLY | O_CLOEXEC);

	if (rtc0 < 0)
	{
		fmt::print (stderr, "failed to open rtc0\n");
		return false;
	}

	struct tm tm;
	struct rtc_wkalrm wake = {};

	time_t const time = wakeTime_.toUTC ().toSecsSinceEpoch ();

	localtime_r (&time, &tm);

	wake.time.tm_sec   = tm.tm_sec;
	wake.time.tm_min   = tm.tm_min;
	wake.time.tm_hour  = tm.tm_hour;
	wake.time.tm_mday  = tm.tm_mday;
	wake.time.tm_mon   = tm.tm_mon;
	wake.time.tm_year  = tm.tm_year;
	wake.time.tm_wday  = -1;
	wake.time.tm_yday  = -1;
	wake.time.tm_isdst = -1;
	wake.enabled       = 1;

	if (ioctl (rtc0, RTC_WKALM_SET, &wake) < 0)
	{
		fmt::print (stderr, "failed to set wake alarm\n");
		close (rtc0);
		return false;
	}
	fmt::print (stderr, "Set alarm for {}\n", wakeTime_.toString (Qt::ISODate).toStdString ());

	close (rtc0);
	return true;
}

}

application_t::~application_t ()
{
	m_server->close ();
	m_server->removeServer (SOCKET_NAME);
}

application_t::application_t (QObject *parent)
    : QObject (parent), m_hw (pickDevice ()), m_settings (prepareSettings ()), m_server (new QLocalServer (this))
{
	m_sm.initialState (STATE_AWAKE,
	        state_t{}
	            .onEnter (std::bind (&application_t::handleAwakeState, this))
	            .addTransition (STATE_AWAKE, EVENT_WAKEUP)
	            .addTransition (STATE_AWAKE, EVENT_NOTIFY)
	            .addTransition (STATE_SLEEP, EVENT_SLEEP))
	    .addState (STATE_NOTIFY,
	        state_t{}
	            .onEnter (std::bind (&application_t::handleNotifyState, this))
	            .addTransition (STATE_AWAKE, EVENT_WAKEUP)
	            .addTransition (STATE_NOTIFY, EVENT_NOTIFY)
	            .addTransition (STATE_SLEEP, EVENT_SLEEP))
	    .addState (STATE_SLEEPWALK,
	        state_t{}
	            .onEnter (std::bind (&application_t::handleSleepwalkState, this))
	            .addTransition (STATE_AWAKE, EVENT_WAKEUP)
	            .addTransition (STATE_NOTIFY, EVENT_NOTIFY)
	            .addTransition (STATE_SLEEP, EVENT_SLEEP))
	    .addState (STATE_SLEEP,
	        state_t{}
	            .onEnter (std::bind (&application_t::handleSleepState, this))
	            .addTransition (STATE_AWAKE, EVENT_WAKEUP)
	            .addTransition (STATE_NOTIFY, EVENT_NOTIFY)
	            .addTransition (STATE_SLEEPWALK, EVENT_SLEEPWALK));

	connect (&m_sm, &sm_t::reportTransition, this, [this] (int const state_) { m_state = state_; });

	connect (signalHandler_t::instance (SIGTERM), &signalHandler_t::raised, this, &application_t::handleTerm);
	connect (signalHandler_t::instance (SIGHUP), &signalHandler_t::raised, this, &application_t::handleHup);

	connect (&m_ledTimer, &QTimer::timeout, this, &application_t::handleLED);

	m_ledTimer.setInterval (LED_INTERVAL);

	connect (&m_pollTimer, &QTimer::timeout, this, &application_t::readDisplay);
	connect (&m_pollTimer, &QTimer::timeout, this, &application_t::readPower);

	m_pollTimer.setInterval (POLL_INTERVAL);
	m_pollTimer.setSingleShot (false);
	m_pollTimer.start ();

	connect (&m_sleepTimer, &QTimer::timeout, this, [this] { m_sm.postEvent (EVENT_SLEEP); });

	m_sleepTimer.setInterval (m_settings.value (WAKE_TIME).value<int> ());
	m_sleepTimer.setSingleShot (true);

	connect (m_server, &QLocalServer::newConnection, this, &application_t::handleConnect);

	m_server->removeServer (SOCKET_NAME);
	m_server->setSocketOptions (QLocalServer::WorldAccessOption);
	if (!m_server->listen (SOCKET_NAME))
		throw fmt::format ("failed to open socket");

	m_unitbus    = new QDBusInterface ("org.freedesktop.systemd1",
	    "/org/freedesktop/systemd1/unit/suspend_2etarget",
	    "org.freedesktop.systemd1.Unit",
	    QDBusConnection::systemBus (),
	    this);
	m_managerbus = new QDBusInterface ("org.freedesktop.systemd1",
	    "/org/freedesktop/systemd1",
	    "org.freedesktop.systemd1.Manager",
	    QDBusConnection::systemBus (),
	    this);
	if (!m_unitbus->connection ().isConnected ())
		throw fmt::format ("failed to connect to sdbus");

	// clang-format off
	connect (m_managerbus,
	    SIGNAL (JobRemoved(uint,QDBusObjectPath,QString,QString)),
	    this,
	    SLOT (handleJobRemoved(uint,QDBusObjectPath,QString,QString)));
	// clang-format on

	m_sm.begin ();
}

void application_t::handleJobRemoved (unsigned id_, QDBusObjectPath object_, QString unit_, QString result_)
{
	Q_UNUSED (unit_);
	Q_UNUSED (result_);

	if (object_.path () != m_lastJobPath.path ())
		return;

	fmt::print (stderr, "{}: Suspend job removed {}, waking up\n", __func__, id_);
	m_sleepCycle++;
	m_sm.postEvent (EVENT_SLEEPWALK);
}

void application_t::handleConnect ()
{
	fmt::print (stderr, "{}: new socket connection\n", __func__);

	if (m_child)
	{
		disconnect (m_child, nullptr, this, nullptr);
		m_child->close ();
		m_child->deleteLater ();
	}

	m_child = m_server->nextPendingConnection ();

	connect (m_child, &QLocalSocket::readyRead, this, &application_t::handleInhibitData);
	connect (m_child, &QLocalSocket::disconnected, this, &application_t::handleDisconnect);
}

void application_t::handleDisconnect ()
{
	disconnect (m_child, nullptr, this, nullptr);
	m_child->close ();
	m_child->deleteLater ();
	m_child = nullptr;
}

void application_t::handleInhibitData ()
{
	fmt::print (stderr, "new socket data\n");
	while (m_child->canReadLine ())
	{
		QString const data = QString::fromLocal8Bit (m_child->readLine ().trimmed ());
		fmt::print (stderr, "socket: {}\n", data.toStdString ());
		if (data == INHIBIT_STRING)
			m_sm.postEvent (EVENT_WAKEUP);
		if (data == NOTIFY_STRING && m_state != STATE_AWAKE)
			m_sm.postEvent (EVENT_NOTIFY);
	}
}

void application_t::handleAwakeState ()
{
	if (!m_sleepTimer.isActive ())
		fmt::print (stderr, "{}: cycle: {}\n", __func__, m_sleepCycle);

	if (m_ledTimer.isActive ())
	{
		m_ledTimer.stop ();
		handleLED ();
	}

	m_sleepTimer.start ();
	m_sleepCycle = 0;
}

void application_t::handleNotifyState ()
{
	fmt::print (stderr, "{}\n", __func__);
	m_sleepCycle = 0;
	handleSleepwalkState ();
}

void application_t::handleSleepwalkState ()
{
	fmt::print (stderr, "{}: cycle: {}\n", __func__, m_sleepCycle);
	if (!m_ledTimer.isActive ())
	{
		m_ledTimer.start ();
		handleLED ();
	}

	if (m_sleepTimer.isActive ())
		return;

	m_sleepTimer.start ();
}

void application_t::handleSleepState ()
{
	fmt::print (stderr, "{}: cycle: {}, seconds: {}\n", __func__, m_sleepCycle, sleepSeconds ());
	// transitions happen first
	m_ledTimer.stop ();
	handleLED ();

	auto const sleepTime = QDateTime::currentDateTime ().addSecs (sleepSeconds ());
	if (!setupWakeAlarm (sleepTime))
	{
		fmt::print (stderr, "Failed to setup wake alarm\n");
		qApp->exit (EXIT_FAILURE);
		return;
	}

	QDBusReply<QDBusObjectPath> const reply = m_unitbus->call (QDBus::Block, "Start", "replace");
	if (!reply.isValid ())
	{
		fmt::print (stderr, "Failed to open sleep endpoint\n");
		qApp->exit (EXIT_FAILURE);
		return;
	}

	m_lastJobPath = reply.value ();

	// the process should go to sleep at this point, so when we come back, we're sleepwalking
}

void application_t::handleTerm ()
{
	fmt::print (stderr, "{}\n", __func__);

	qApp->exit (EXIT_SUCCESS);
}

void application_t::handleHup ()
{
	fmt::print (stderr, "{}\n", __func__);
}

void application_t::handleLED ()
{
	static bool toggle = false;
	static QByteArray const ON ("1");
	static QByteArray const OFF ("0");

	QFile r (m_hw[application_t::LED_RED]);
	QFile g (m_hw[application_t::LED_GREEN]);
	QFile b (m_hw[application_t::LED_BLUE]);

	if (!r.open (QFile::Append) || !g.open (QFile::Append) || !b.open (QFile::Append))
		return;

	switch (m_state)
	{
	case STATE_AWAKE:
		toggle = false;
		r.write (OFF);
		g.write (OFF);
		b.write (OFF);
		break;
	case STATE_SLEEPWALK:
		toggle = !toggle;
		r.write (toggle ? ON : OFF);
		g.write (OFF);
		b.write (OFF);
		break;
	case STATE_SLEEP:
		toggle = false;
		r.write (OFF);
		g.write (ON);
		b.write (OFF);
		break;
	case STATE_NOTIFY:
		toggle = !toggle;
		r.write (OFF);
		g.write (OFF);
		b.write (toggle ? ON : OFF);
		break;
	}
}

void application_t::readPower ()
{
	// read usb first
	{
		QFile f (m_hw[hw_t::PSU]);
		if (!f.open (QFile::ReadOnly))
		{
			fmt::print (stderr, "failed to open PSU\n");
			return;
		}

		bool ok       = false;
		auto const on = f.readAll ().toInt (&ok);
		if (!ok)
		{
			fmt::print (stderr, "failed to read PSU\n");
			return;
		}

		if (on)
			m_sm.postEvent (EVENT_WAKEUP);
	}

	// read keyboard second
	{
		static bool KbAvailable = true;

		if (!KbAvailable)
			return;

		QFile f (m_hw[hw_t::KEYBOARD]);
		if (!f.open (QFile::ReadOnly))
		{
			KbAvailable = false;
			fmt::print (stderr, "failed to open Keyboard\n");
			return;
		}

		bool ok       = false;
		auto const on = f.readAll ().toInt (&ok);
		if (!ok)
		{
			fmt::print (stderr, "failed to read Keyboard\n");
			return;
		}

		if (on)
			m_sm.postEvent (EVENT_WAKEUP);
	}
}

void application_t::readDisplay ()
{
	QFile f (m_hw[hw_t::DISPLAY]);
	if (!f.open (QFile::ReadOnly))
	{
		fmt::print (stderr, "failed to open Display\n");
		return;
	}
	bool ok        = false;
	auto const off = f.readAll ().toInt (&ok);
	if (!ok)
	{
		fmt::print (stderr, "failed to read Display\n");
		return;
	}

	if (!off)
		m_sm.postEvent (EVENT_WAKEUP);
}

int application_t::sleepSeconds ()
{
	return m_settings.value (SLEEP_TIME).value<int> () * std::clamp (m_sleepCycle, 1, 10) / 1000;
}
