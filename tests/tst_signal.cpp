#include "core/signal.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace arterm;

TEST_CASE( "every connected slot is called, in order", "[signal]" ){
	Signal<int>      signal;
	std::vector<int> seen;

	signal.connect( [&seen]( int value ){ seen.push_back( value ); } );
	signal.connect( [&seen]( int value ){ seen.push_back( value * 10 ); } );
	signal( 3 );

	CHECK( seen == std::vector<int>{ 3, 30 } );
}

TEST_CASE( "a disconnected slot stops firing", "[signal]" ){
	Signal<>  signal;
	int       calls = 0;
	SlotId const id = signal.connect( [&calls]{ ++calls; } );

	signal();
	signal.disconnect( id );
	signal();

	CHECK( calls == 1 );
	CHECK_FALSE( signal.has_slots() );
}

TEST_CASE( "a slot may disconnect itself while the signal is emitting", "[signal]" ){
	Signal<> signal;
	int      first  = 0;
	int      second = 0;

	SlotId id = 0;
	id        = signal.connect( [&]{
        ++first;
        signal.disconnect( id );
    } );
	signal.connect( [&second]{ ++second; } );

	signal();
	signal();

	// The self-removing slot ran once; the one behind it must not have been
	// skipped by the removal.
	CHECK( first == 1 );
	CHECK( second == 2 );
}

TEST_CASE( "slots connected during emission wait for the next one", "[signal]" ){
	Signal<> signal;
	int      late = 0;

	signal.connect( [&]{ signal.connect( [&late]{ ++late; } ); } );

	signal();
	CHECK( late == 0 );
	signal();
	CHECK( late == 1 );
}

TEST_CASE( "ScopedConnection unsubscribes when it goes out of scope", "[signal]" ){
	Signal<std::string const&> signal;
	std::string                seen;

	{
		ScopedConnection<Signal<std::string const&>> connection(
			signal, signal.connect( [&seen]( std::string const& text ){ seen = text; } ) );
		signal( "inside" );
		CHECK( seen == "inside" );
	}

	signal( "outside" );
	CHECK( seen == "inside" );
	CHECK_FALSE( signal.has_slots() );
}
