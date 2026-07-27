#include "ssh/endpoint.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace arterm::ssh;

TEST_CASE( "a bare hostname defaults the port and leaves the user empty", "[endpoint]" ){
	auto const endpoint = parse_endpoint( "example.com" );

	REQUIRE( endpoint.has_value() );
	CHECK( endpoint->hostname == "example.com" );
	CHECK( endpoint->port == 22 );
	CHECK( endpoint->username.empty() );
}

TEST_CASE( "the user part is taken from before the at sign", "[endpoint]" ){
	auto const endpoint = parse_endpoint( "deploy@build.internal" );

	REQUIRE( endpoint.has_value() );
	CHECK( endpoint->username == "deploy" );
	CHECK( endpoint->hostname == "build.internal" );
	CHECK( endpoint->port == 22 );
}

TEST_CASE( "an explicit port is honoured", "[endpoint]" ){
	auto const endpoint = parse_endpoint( "ci@build.internal:2222" );

	REQUIRE( endpoint.has_value() );
	CHECK( endpoint->username == "ci" );
	CHECK( endpoint->hostname == "build.internal" );
	CHECK( endpoint->port == 2222 );
}

TEST_CASE( "surrounding whitespace is ignored", "[endpoint]" ){
	auto const endpoint = parse_endpoint( "   root@10.0.0.5:22\t " );

	REQUIRE( endpoint.has_value() );
	CHECK( endpoint->username == "root" );
	CHECK( endpoint->hostname == "10.0.0.5" );
}

TEST_CASE( "an ssh URL is accepted and its scheme dropped", "[endpoint]" ){
	auto const endpoint = parse_endpoint( "ssh://admin@host.example:2200" );

	REQUIRE( endpoint.has_value() );
	CHECK( endpoint->username == "admin" );
	CHECK( endpoint->hostname == "host.example" );
	CHECK( endpoint->port == 2200 );

	// A pasted URL often ends in a slash.
	auto const trailing = parse_endpoint( "ssh://host.example/" );
	REQUIRE( trailing.has_value() );
	CHECK( trailing->hostname == "host.example" );
}

TEST_CASE( "IPv6 literals keep their colons", "[endpoint]" ){
	auto const plain = parse_endpoint( "[2001:db8::1]" );
	REQUIRE( plain.has_value() );
	CHECK( plain->hostname == "2001:db8::1" );
	CHECK( plain->port == 22 );

	auto const with_port = parse_endpoint( "user@[2001:db8::1]:2222" );
	REQUIRE( with_port.has_value() );
	CHECK( with_port->username == "user" );
	CHECK( with_port->hostname == "2001:db8::1" );
	CHECK( with_port->port == 2222 );

	auto const loopback = parse_endpoint( "[::1]:22" );
	REQUIRE( loopback.has_value() );
	CHECK( loopback->hostname == "::1" );
}

TEST_CASE( "a username may contain an at sign", "[endpoint]" ){
	// Azure and some managed hosts use user@domain as the login name; the last
	// at sign is the one that separates it from the host.
	auto const endpoint = parse_endpoint( "user@corp.example@jump.internal" );

	REQUIRE( endpoint.has_value() );
	CHECK( endpoint->username == "user@corp.example" );
	CHECK( endpoint->hostname == "jump.internal" );
}

TEST_CASE( "nonsense is rejected rather than half-parsed", "[endpoint]" ){
	CHECK_FALSE( parse_endpoint( "" ).has_value() );
	CHECK_FALSE( parse_endpoint( "   " ).has_value() );
	CHECK_FALSE( parse_endpoint( "@host" ).has_value() );        // No user.
	CHECK_FALSE( parse_endpoint( "user@" ).has_value() );        // No host.
	CHECK_FALSE( parse_endpoint( "host:" ).has_value() );        // No port digits.
	CHECK_FALSE( parse_endpoint( "host:0" ).has_value() );       // Port zero.
	CHECK_FALSE( parse_endpoint( "host:70000" ).has_value() );   // Out of range.
	CHECK_FALSE( parse_endpoint( "host:ssh" ).has_value() );     // Not a number.
	CHECK_FALSE( parse_endpoint( "host:22x" ).has_value() );     // Trailing junk.
	CHECK_FALSE( parse_endpoint( "host/path" ).has_value() );    // A path, not a host.
	CHECK_FALSE( parse_endpoint( "two hosts" ).has_value() );    // Whitespace inside.
	CHECK_FALSE( parse_endpoint( "[2001:db8::1" ).has_value() ); // Unterminated bracket.
	CHECK_FALSE( parse_endpoint( "[]" ).has_value() );           // Empty literal.
	CHECK_FALSE( parse_endpoint( "[::1]x" ).has_value() );       // Junk after the bracket.
}
