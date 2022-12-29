#include "sm.H"

#include <map>

#include <limits>

///////////////////////////////////////////////////////////////////////////
namespace
{
auto constexpr INVALID = std::numeric_limits<unsigned>::max ();
}

///////////////////////////////////////////////////////////////////////////
/// \brief The state_t::privateData_t class
class state_t::privateData_t
{
public:
	std::map<unsigned, unsigned> transitionMap; ///< transition function

	std::vector<state_t::callback> onEnterFn; ///< entry callbacks
	std::vector<state_t::callback> onExitFn;  ///< exit callbacks
};

///////////////////////////////////////////////////////////////////////////
state_t::~state_t () = default;

state_t::state_t (QObject *parent_) : QObject (parent_), m_d (new privateData_t{})
{
}

state_t::state_t (state_t const &o_) : state_t (o_.parent ())
{
	*m_d = *o_.m_d;
}

state_t &state_t::onEnter (state_t::callback const &callback_)
{
	m_d->onEnterFn.emplace_back (callback_);

	return *this;
}

state_t &state_t::onExit (state_t::callback const &callback_)
{
	m_d->onExitFn.emplace_back (callback_);

	return *this;
}

state_t &state_t::addTransition (unsigned const stateId_, unsigned const eventId_)
{
	m_d->transitionMap[eventId_] = stateId_;

	return *this;
}

void state_t::enter () const
{
	for (auto const &callback : m_d->onEnterFn)
		callback ();
}

void state_t::exit () const
{
	for (auto const &callback : m_d->onExitFn)
		callback ();
}

void state_t::postEvent (unsigned const eventId_) const
{
	auto const itr = m_d->transitionMap.find (eventId_);

	if (itr == m_d->transitionMap.end ())
		emit fail ();

	emit transition (itr->second);
}

///////////////////////////////////////////////////////////////////////////
/// \brief The sm_t::privateData_t class
class sm_t::privateData_t
{
public:
	/// \brief State list
	std::map<unsigned, state_t> stateList;

	/// \brief state to be in on begin
	unsigned initialState = INVALID;
	/// \brief state to emit complete from
	unsigned finalState = INVALID;
	/// \brief current state
	unsigned currentState = INVALID;
};

///////////////////////////////////////////////////////////////////////////

sm_t::~sm_t () = default;

sm_t::sm_t (QObject *const parent_) : QObject (parent_), m_d (new privateData_t{})
{
}

sm_t &sm_t::initialState (unsigned const stateId_, state_t const &state_)
{
	m_d->initialState         = stateId_;
	auto const [itr, success] = m_d->stateList.emplace (stateId_, state_);

	connect (&itr->second, &state_t::transition, this, &sm_t::handleTransition);
	connect (&itr->second, &state_t::fail, this, &sm_t::fail);

	return *this;
}

sm_t &sm_t::addState (unsigned const stateId_, state_t const &state_)
{
	auto const [itr, success] = m_d->stateList.emplace (stateId_, state_);

	connect (&itr->second, &state_t::transition, this, &sm_t::handleTransition);
	connect (&itr->second, &state_t::fail, this, &sm_t::fail);

	return *this;
}

sm_t &sm_t::finalState (unsigned const stateId_, state_t const &state_)
{
	m_d->finalState           = stateId_;
	auto const [itr, success] = m_d->stateList.emplace (stateId_, state_);

	connect (&itr->second, &state_t::transition, this, &sm_t::handleTransition);
	connect (&itr->second, &state_t::fail, this, &sm_t::fail);

	return *this;
}

void sm_t::postEvent (unsigned const eventId_)
{
	if (m_d->currentState == INVALID)
		return;

	auto const &state = m_d->stateList[m_d->currentState];
	state.postEvent (eventId_);
}

void sm_t::begin ()
{
	m_d->currentState = m_d->initialState;
	if (m_d->currentState == INVALID)
		emit fail ();

	auto const &state = m_d->stateList[m_d->currentState];
	state.enter ();

	emit reportTransition (m_d->currentState);
}

void sm_t::handleTransition (unsigned const stateId_)
{
	if (m_d->currentState == INVALID)
	{
		emit fail ();
		return;
	}

	if (m_d->stateList.find (stateId_) == m_d->stateList.end ())
	{
		emit fail ();
		m_d->currentState = INVALID;
		return;
	}

	auto const &oldState = m_d->stateList[m_d->currentState];
	oldState.exit ();

	m_d->currentState = stateId_;

	emit reportTransition (m_d->currentState);

	auto const &newState = m_d->stateList[m_d->currentState];
	newState.enter ();

	if (m_d->currentState == m_d->finalState)
		emit complete ();
}
