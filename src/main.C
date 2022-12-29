#include "application.H"
#include "inhibit.H"

#include <QCoreApplication>
#include <QFile>
#include <QProcess>

#include <fmt/format.h>

#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

int main (int argc, char *argv[])
{
	qputenv("TZ", "UTC");

	uid_t uid = getuid ();
	gid_t gid = getgid ();
	QString dbusSession;
	QString user;
	QString display;

	{
		// switch to gnome-session user and open dbus
		QProcess p;
		p.setProgram ("pidof");
		p.setArguments ({"-s", "gnome-session-binary"});
		p.start ();
		if (!p.waitForFinished (1000))
		{
			fmt::print (stderr, "Failed to read session pid\n");
			return EXIT_FAILURE;
		}

		bool ok        = false;
		auto const pid = p.readAll ().toInt (&ok);
		if (!ok)
		{
			fmt::print (stderr, "Failed to parse session pid\n");
			return EXIT_FAILURE;
		}

		QFile f (QString ("/proc/%1/environ").arg (pid));
		if (!f.open (QFile::ReadOnly))
		{
			fmt::print (stderr, "Failed to read session environment\n");
			return EXIT_FAILURE;
		}

		auto const e = f.readAll ().split (0);
		QMap<QString, QString> env;
		for (int i = 0; i < e.size (); ++i)
		{
			auto const kvp = e[i].split ('=');
			if (kvp.size () < 2)
				continue;
			QStringList end;
			for (int j = 1; j < kvp.size (); ++j)
				end << kvp[j];
			env[kvp[0]] = end.join ('=');
		}
		dbusSession = env["DBUS_SESSION_BUS_ADDRESS"];
		user        = env["USER"];
		display     = env["DISPLAY"];

		auto const pwd = getpwnam (user.toLocal8Bit ());
		if (!pwd)
		{
			fmt::print (stderr, "Failed to get uid for user={}\n", user.toStdString ());
			return EXIT_FAILURE;
		}
		uid = pwd->pw_uid;
		gid = pwd->pw_gid;
	}

	auto const subProcess = fork ();
	if (subProcess < 0)
	{
		fmt::print (stderr, "Failed to fork process\n");
		return EXIT_FAILURE;
	}

	QCoreApplication a (argc, argv);

	if (subProcess > 0)
	{
		QObject::connect (&a, &QCoreApplication::aboutToQuit, [subProcess] {
			kill (subProcess, SIGTERM);
			int status;
			waitpid (subProcess, &status, WNOHANG);
		});

		try
		{
			application_t app;

			return a.exec ();
		}
		catch (std::string const &ex_)
		{
			fmt::print (stderr, "Startup failed: {}\n", ex_);

			return EXIT_FAILURE;
		}
	}
	else
	{
		if (setgid (gid) != 0)
		{
			fmt::print (stderr, "Failed to drop group privileges\n");
			return EXIT_FAILURE;
		}
		if (setuid (uid) != 0)
		{
			fmt::print (stderr, "Failed to drop user privileges\n");
			return EXIT_FAILURE;
		}
		if (initgroups (user.toLocal8Bit (), gid) != 0)
		{
			fmt::print (stderr, "Failed to initialize groups\n");
			// return EXIT_FAILURE;
		}

		qputenv ("DBUS_SESSION_BUS_ADDRESS", dbusSession.toLocal8Bit ());
		qputenv ("DISPLAY", display.toLocal8Bit ());

		fmt::print (
		    stderr, "DBUS_SESSION_BUS_ADDRESS={}\n", qEnvironmentVariable ("DBUS_SESSION_BUS_ADDRESS").toStdString ());
		fmt::print (stderr, "DISPLAY={}\n", qEnvironmentVariable ("DISPLAY").toStdString ());

		inhibit_t app;

		return a.exec ();
	}
}
