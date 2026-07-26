#include "core/base64.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace arterm;

TEST_CASE( "RFC 4648 vectors round-trip", "[base64]" ){
	struct Case
	{
		std::string_view raw;
		std::string_view encoded;
	};

	static constexpr Case CASES[] = {
		{ "", "" },
		{ "f", "Zg==" },
		{ "fo", "Zm8=" },
		{ "foo", "Zm9v" },
		{ "foob", "Zm9vYg==" },
		{ "fooba", "Zm9vYmE=" },
		{ "foobar", "Zm9vYmFy" },
	};

	for( Case const& entry : CASES ){
		INFO( entry.raw );
		CHECK( base64_encode( entry.raw ) == entry.encoded );
		auto const decoded = base64_decode( entry.encoded );
		REQUIRE( decoded.has_value() );
		CHECK( *decoded == entry.raw );
	}
}

TEST_CASE( "every byte value survives a round-trip", "[base64]" ){
	std::string binary;
	for( int i = 0; i < 256; ++i )
		binary.push_back( static_cast<char>( i ) );

	auto const decoded = base64_decode( base64_encode( binary ) );
	REQUIRE( decoded.has_value() );
	CHECK( *decoded == binary );
}

TEST_CASE( "malformed input is rejected rather than guessed at", "[base64]" ){
	CHECK_FALSE( base64_decode( "abc" ).has_value() );       // Length not a multiple of four.
	CHECK_FALSE( base64_decode( "a===" ).has_value() );      // Three padding bytes.
	CHECK_FALSE( base64_decode( "!!!!" ).has_value() );      // Outside the alphabet.
	CHECK_FALSE( base64_decode( "Zg=a" ).has_value() );      // Padding in the middle.
	CHECK_FALSE( base64_decode( "Zm9v YmFy" ).has_value() ); // Whitespace is not skipped.
}
