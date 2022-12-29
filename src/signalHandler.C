#include "signalHandler.H"

#include <QSocketNotifier>

#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fmt/format.h>

#include <mutex>

namespace
{
std::array<QPointer<signalHandler_t>, _NSIG> HANDLERS;
}

extern "C" {
void handleSignal (int);
}

void handleSignal (int const signal_)
{
	if (signal_ < 0 || static_cast<size_t> (signal_) >= HANDLERS.size ())
	{
		fmt::print (stderr, "ignored out-of-range signal {}\n", signal_);
		return;
	}

	auto const &h = HANDLERS[signal_];
	if (!h)
	{
		fmt::print (stderr, "ignored unhandled signal: {}\n", signal_);
		return;
	}

	char c = 0;
	if (::write (h->write (), &c, sizeof c) <= 0)
		fmt::print (stderr, "Error writing signal {} (ignored) - {}\n", signal_, strerror (errno));
}

signalHandler_t::~signalHandler_t ()
{
	::close (m_fd[0]);
	::close (m_fd[1]);
}

signalHandler_t *signalHandler_t::instance (int const signal_)
{
	static std::mutex lock;
	std::unique_lock<std::mutex> guard{lock};

	if (HANDLERS[signal_])
		return HANDLERS[signal_].data ();

	return new signalHandler_t (signal_);
}

int signalHandler_t::write ()
{
	return m_fd[0];
}

// This slot is connected to our socket notifier.  It reads the byte that the
// signal handler wrote (to reset the notifier) and emits a Qt signal.
void signalHandler_t::consumeInput (int const fd_) const
{
	char c;
	if (::read (fd_, &c, sizeof c) <= 0)
		fmt::print (stderr, "Error reading fd {} (ignored) - {}", fd_, strerror (errno));
	emit raised ();
}

signalHandler_t::signalHandler_t (int const signal_)
{
	if (::socketpair (AF_UNIX, SOCK_STREAM, 0, m_fd))
		throw fmt::format ("failed to create socket pair for {} - {}", signal_, strerror (errno));

	// There's not very much that a signal handler can legally do.  One thing
	// that is permitted is to write to an open file descriptor.  When our
	// handler is called, we'll write a single byte to a socket, and this socket
	// notifier will then learn of the signal outside of the signal handler
	// context.
	auto const notifier = new QSocketNotifier (m_fd[1], QSocketNotifier::Read, this);
	connect (notifier, &QSocketNotifier::activated, this, &signalHandler_t::consumeInput);

	struct sigaction action;
	action.sa_handler = &::handleSignal;
	sigemptyset (&action.sa_mask);
	action.sa_flags = SA_RESTART;

	if (::sigaction (signal_, &action, 0))
		throw fmt::format ("failed to add sigaction for {} - {}", signal_, strerror (errno));

	HANDLERS[signal_] = this;
}
