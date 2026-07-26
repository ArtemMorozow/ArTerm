#include "core/dispatch.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using namespace arterm;

namespace
{

	/// Mirrors the ShellSession/SftpSession lifetime: a queue-owning object whose
	/// last shared_ptr reference can be released from inside a block running on
	/// its own queue, so the destructor runs on that queue. A destructor that
	/// sync'd onto the queue unconditionally would deadlock (libdispatch traps);
	/// `is_current` is what makes it tear down inline instead.
	class QueueOwned : public std::enable_shared_from_this<QueueOwned>
	{
	public:
		static std::shared_ptr<QueueOwned> create( std::atomic<bool>& destroyed ){
			return std::shared_ptr<QueueOwned>( new QueueOwned( destroyed ) );
		}

		~QueueOwned(){
			if( _queue.is_current() )
				teardown();
			else
				_queue.sync( [this] { teardown(); } );
			_destroyed.store( true );
		}

		/// Enqueues a block that carries the last reference, so releasing it on
		/// the queue destroys the object there.
		void kick(){
			_queue.async( [self = shared_from_this()]{
				// self drops at the end of this block, on the queue thread.
			} );
		}

	private:
		explicit QueueOwned( std::atomic<bool>& destroyed )
			: _queue( "test.queue-owned" )
			, _destroyed( destroyed )
		{}

		void teardown() {}

		Queue              _queue;
		std::atomic<bool>& _destroyed;
	};

} // namespace

TEST_CASE( "is_current reflects the running queue", "[dispatch]" ){
	Queue queue( "test.is-current" );

	CHECK_FALSE( queue.is_current() );

	std::atomic<bool> seen_inside{ false };
	queue.sync( [&] { seen_inside.store( queue.is_current() ); } );
	CHECK( seen_inside.load() );

	// A different queue is not "current" while the first one runs.
	Queue             other( "test.is-current.other" );
	std::atomic<bool> other_inside{ true };
	queue.sync( [&] { other_inside.store( other.is_current() ); } );
	CHECK_FALSE( other_inside.load() );
}

TEST_CASE( "destroying an object from inside its own queue does not deadlock", "[dispatch]" ){
	std::atomic<bool> destroyed{ false };

	{
		auto object = QueueOwned::create( destroyed );
		object->kick();
		// Main drops its reference; the queued block now holds the last one and
		// will release it - and destroy the object - on the queue thread.
	}

	for( int i = 0; i < 400 && !destroyed.load(); ++i )
		std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );

	CHECK( destroyed.load() );
}
