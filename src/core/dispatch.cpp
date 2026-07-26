#include "core/dispatch.hpp"

#include <cstdint>
#include <memory>
#include <pthread.h>
#include <utility>

namespace arterm
{
	namespace
	{

		/// libdispatch takes a plain function plus a context pointer. Heap-allocating
		/// the closure keeps this file free of Objective-C blocks, so it compiles as
		/// ordinary C++ rather than Objective-C++.
		void run_once( void* context ){
			std::unique_ptr<Work> const work( static_cast<Work*>( context ) );
			if( *work )
				( *work )();
		}

		void run_repeating( void* context ){
			auto* work = static_cast<Work*>( context );
			if( work != nullptr && *work )
				( *work )();
		}

		void destroy_context( void* context ){
			delete static_cast<Work*>( context );
		}

		dispatch_time_t deadline( std::chrono::milliseconds delay ){
			auto const nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>( delay ).count();
			return dispatch_time( DISPATCH_TIME_NOW, static_cast<std::int64_t>( nanoseconds ) );
		}

		/// A source must be running before it can be cancelled, otherwise the cancel
		/// handler never fires and the context leaks.
		void cancel_source( dispatch_source_t& source, bool& running ){
			if( source == nullptr )
				return;

			if( !running ){
				dispatch_resume( source );
				running = true;
			}
			dispatch_source_cancel( source );
			dispatch_release( source );
			source  = nullptr;
			running = false;
		}

	} // namespace

	void on_main( Work work ){
		dispatch_async_f( dispatch_get_main_queue(), new Work( std::move( work ) ), &run_once );
	}

	bool is_main_queue() noexcept{
		// dispatch_get_current_queue() is deprecated and unreliable; the main queue
		// is by definition the one bound to the main thread.
		return pthread_main_np() != 0;
	}

	void on_main_sync( Work work ){
		if( is_main_queue() ){
			if( work )
				work();
			return;
		}

		dispatch_sync_f( dispatch_get_main_queue(), &work, []( void* context ){
			auto* held = static_cast<Work*>( context );
			if( *held )
				( *held )();
		} );
	}

	// -- Queue -----------------------------------------------------------------

	Queue::Queue( std::string const& label )
		: _queue( dispatch_queue_create( label.c_str(), DISPATCH_QUEUE_SERIAL ) )
	{}

	Queue::~Queue(){
		if( _queue != nullptr )
			dispatch_release( _queue );
	}

	Queue::Queue( Queue&& other ) noexcept
		: _queue( std::exchange( other._queue, nullptr ) )
	{}

	Queue& Queue::operator=( Queue&& other ) noexcept{
		if( this != &other ){
			if( _queue != nullptr )
				dispatch_release( _queue );
			_queue = std::exchange( other._queue, nullptr );
		}
		return *this;
	}

	void Queue::async( Work work ) const{
		dispatch_async_f( _queue, new Work( std::move( work ) ), &run_once );
	}

	void Queue::sync( Work work ) const{
		dispatch_sync_f( _queue, &work, []( void* context ){
			auto* held = static_cast<Work*>( context );
			if( *held )
				( *held )();
		} );
	}

	// -- ReadSource ------------------------------------------------------------

	ReadSource::ReadSource( int descriptor, Queue const& queue, Work handler )
		: _source( dispatch_source_create( DISPATCH_SOURCE_TYPE_READ, static_cast<uintptr_t>( descriptor ), 0,
										   queue.handle() ) ){
		if( _source == nullptr )
			return;

		dispatch_set_context( _source, new Work( std::move( handler ) ) );
		dispatch_source_set_event_handler_f( _source, &run_repeating );
		dispatch_source_set_cancel_handler_f( _source, &destroy_context );
	}

	ReadSource::~ReadSource(){
		cancel_source( _source, _running );
	}

	ReadSource::ReadSource( ReadSource&& other ) noexcept
		: _source( std::exchange( other._source, nullptr ) )
		, _running( std::exchange( other._running, false ) )
	{}

	ReadSource& ReadSource::operator=( ReadSource&& other ) noexcept{
		if( this != &other ){
			cancel_source( _source, _running );
			_source  = std::exchange( other._source, nullptr );
			_running = std::exchange( other._running, false );
		}
		return *this;
	}

	void ReadSource::resume(){
		if( _source != nullptr && !_running ){
			dispatch_resume( _source );
			_running = true;
		}
	}

	void ReadSource::suspend(){
		if( _source != nullptr && _running ){
			dispatch_suspend( _source );
			_running = false;
		}
	}

	void ReadSource::cancel(){
		cancel_source( _source, _running );
	}

	// -- Timer -----------------------------------------------------------------

	Timer::Timer( std::chrono::milliseconds interval, Queue const& queue, Work handler )
		: _source( dispatch_source_create( DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue.handle() ) )
		, _interval( interval )
	{
		if( _source == nullptr )
			return;

		dispatch_set_context( _source, new Work( std::move( handler ) ) );
		dispatch_source_set_event_handler_f( _source, &run_repeating );
		dispatch_source_set_cancel_handler_f( _source, &destroy_context );
		set_interval( interval );
	}

	Timer::~Timer(){
		cancel_source( _source, _running );
	}

	Timer::Timer( Timer&& other ) noexcept
		: _source( std::exchange( other._source, nullptr ) )
		, _interval( other._interval )
		, _running( std::exchange( other._running, false ) )
	{}

	Timer& Timer::operator=( Timer&& other ) noexcept{
		if( this != &other ){
			cancel_source( _source, _running );
			_source   = std::exchange( other._source, nullptr );
			_interval = other._interval;
			_running  = std::exchange( other._running, false );
		}
		return *this;
	}

	void Timer::set_interval( std::chrono::milliseconds interval ){
		_interval = interval;
		if( _source == nullptr )
			return;

		auto const nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>( interval ).count();
		// A tenth of the interval of leeway lets the kernel coalesce wake-ups, which
		// matters for the keep-alive and poll timers running on every session.
		dispatch_source_set_timer( _source, deadline( interval ), static_cast<std::uint64_t>( nanoseconds ),
								   static_cast<std::uint64_t>( nanoseconds / 10 ) );
	}

	void Timer::start(){
		if( _source != nullptr && !_running ){
			dispatch_resume( _source );
			_running = true;
		}
	}

	void Timer::stop(){
		if( _source != nullptr && _running ){
			dispatch_suspend( _source );
			_running = false;
		}
	}

	void Timer::cancel(){
		cancel_source( _source, _running );
	}

	void after( std::chrono::milliseconds delay, Queue const& queue, Work work ){
		dispatch_after_f( deadline( delay ), queue.handle(), new Work( std::move( work ) ), &run_once );
	}

} // namespace arterm
