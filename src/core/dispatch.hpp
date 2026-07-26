#pragma once

#include "core/non_copyable.hpp"

#include <dispatch/dispatch.h>

#include <chrono>
#include <functional>
#include <string>

namespace arterm
{

	using Work = std::function<void()>;

	/// Hands `work` to the main queue. Everything that touches AppKit goes through
	/// here; nothing else may.
	void on_main( Work work );

	/// True when the caller is already on the main queue, so `on_main` would only
	/// add a hop.
	[[nodiscard]] bool is_main_queue() noexcept;

	/// A serial queue owning one unit of work - an SSH session, a transfer.
	///
	/// Serial means the work it guards needs no further locking: libssh2 sessions
	/// in particular are not safe to touch from two threads.
	class Queue : NonCopyable
	{
	public:
		explicit Queue( std::string const& label );
		~Queue();

		Queue( Queue&& other ) noexcept;
		Queue& operator=( Queue&& other ) noexcept;

		void async( Work work ) const;

		/// Blocks until `work` has run. Never call it from the queue itself.
		void sync( Work work ) const;

		[[nodiscard]] dispatch_queue_t handle() const noexcept { return _queue; }

	private:
		dispatch_queue_t _queue{ nullptr };
	};

	/// Fires `handler` on `queue` whenever the descriptor has bytes to read - the
	/// replacement for QSocketNotifier.
	///
	/// The descriptor stays owned by the caller and must outlive the source.
	class ReadSource : NonCopyable
	{
	public:
		ReadSource() = default;
		ReadSource( int descriptor, Queue const& queue, Work handler );
		~ReadSource();

		ReadSource( ReadSource&& other ) noexcept;
		ReadSource& operator=( ReadSource&& other ) noexcept;

		void resume();
		void suspend();

		/// Idempotent, and safe to call from the source's own queue.
		void cancel();

		[[nodiscard]] bool is_active() const noexcept { return _source != nullptr; }

	private:
		dispatch_source_t _source{ nullptr };
		bool              _running{ false };
	};

	/// Repeating timer on a queue - the replacement for QTimer.
	class Timer : NonCopyable
	{
	public:
		Timer() = default;
		Timer( std::chrono::milliseconds interval, Queue const& queue, Work handler );
		~Timer();

		Timer( Timer&& other ) noexcept;
		Timer& operator=( Timer&& other ) noexcept;

		void set_interval( std::chrono::milliseconds interval );
		void start();
		void stop();
		void cancel();

		[[nodiscard]] bool is_running() const noexcept { return _running; }

	private:
		dispatch_source_t         _source{ nullptr };
		std::chrono::milliseconds _interval{ 0 };
		bool                      _running{ false };
	};

	/// Runs `work` on `queue` once, after `delay`.
	void after( std::chrono::milliseconds delay, Queue const& queue, Work work );

} // namespace arterm
