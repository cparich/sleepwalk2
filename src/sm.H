#pragma once

#include <QObject>

#include <functional>
#include <memory>

/// \brief Provides for state machine states
class state_t : public QObject
{
	Q_OBJECT
public:
	/// \brief callback prototype
	using callback = std::function<void ()>;

	~state_t ();

	/// \brief Constructor
	/// \param parent_ QObject parent reference
	explicit state_t (QObject *parent_ = nullptr);

	/// \brief Copy
	/// \param o_ other state
	state_t (state_t const &o_);

	/// \brief Add a callback when the state machine enters this state
	/// \param callback_ the callback
	state_t &onEnter (callback const &callback_);
	/// \brief Add a callback when the state machine exits this state
	/// \param callback_ the callback
	/// \note onExit for a final state is never called
	state_t &onExit (callback const &callback_);
	/// \brief Add a transition to a new state on an event
	/// \param stateId_ the state to transition to
	/// \param eventId_ the event to transition on
	/// \note multiple transitions from the same event is undefined, assume a DFA
	state_t &addTransition (unsigned stateId_, unsigned eventId_);

	/// \brief Execute the onEnter callback
	void enter () const;
	/// \brief Execute the onExit callback
	void exit () const;
	/// \brief Transition
	/// \param eventId_ the posted event
	void postEvent (unsigned eventId_) const;

signals:
	/// \brief Emitted when a transition occurs
	/// \param stateId_ the state to transition to
	void transition (unsigned stateId_) const;
	/// \brief Emitted when an unhandled event occurs
	void fail () const;

private:
	class privateData_t;
	std::unique_ptr<privateData_t> m_d; ///< private data
};

/// \brief A state machine. Provides for a DFA with signals for transition, complete, and fail
class sm_t : public QObject
{
	Q_OBJECT
public:
	~sm_t ();
	/// \brief Constructor
	/// \param parent_ QObject parent reference
	explicit sm_t (QObject *parent_ = nullptr);

	/// \brief The state entered when \ref sm_t::begin is called
	/// \param stateId_ the state identifier
	/// \param state_ a state object
	/// \note this interface supports fluent syntax
	/// \note multiple calls to this method is undefined
	/// \note must be called at least once
	sm_t &initialState (unsigned stateId_, state_t const &state_);
	/// \brief Add a state to the state machine
	/// \param stateId_ the state identifier
	/// \param state_ a state object
	/// \note this interface supports fluent syntax
	sm_t &addState (unsigned stateId_, state_t const &state_);
	/// \brief The state to use for completion (optional)
	/// \param stateId_ the state identifier
	/// \param state_ a state object
	/// \note this interface supports fluent syntax
	/// \note multiple calls to this method is undefined
	sm_t &finalState (unsigned stateId_, state_t const &state_);

	/// \brief Post an event to the state machine
	/// \param eventId_ the event id to post
	void postEvent (unsigned eventId_);

	/// \brief Start (or restart) the state machine at the initial state
	void begin ();

signals:
	/// \brief Emitted when a transition happens
	/// \param to_ the state id being transitioned to
	void reportTransition (unsigned to_);
	/// \brief Emitted when a state has no transition for the event
	void fail ();
	/// \brief Emitted when the final state is reached
	void complete ();

private:
	/// \brief Handle a state transition
	/// \param stateId_ the state id to transition to
	void handleTransition (unsigned stateId_);

	class privateData_t;
	std::unique_ptr<privateData_t> m_d; ///< private data
};
