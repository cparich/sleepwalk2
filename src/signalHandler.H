#pragma once

#include <QObject>
#include <QPointer>

#include <array>

// A UnixSignalHandler catches a particular Unix signal (e.g. SIGTERM) and emits
// a Qt signal which can be connected to a slot.  Note that a process cannot
// catch SIGKILL - a handler for SIGKILL will never emit.
class signalHandler_t : public QObject
{
	Q_OBJECT

public:
	~signalHandler_t () override;

	static signalHandler_t *instance (int signal_);

	int write ();

signals:
	// This gives no indication of which signal has been caught; you may achieve
	// that by connecting to a QSignalMapper if required.
	void raised () const;

private slots:
	void consumeInput (int fd_) const;

private:
	signalHandler_t (int signal_);

	int m_fd[2]{};
};
