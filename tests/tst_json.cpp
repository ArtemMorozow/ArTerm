#include "core/json.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace arterm;

TEST_CASE( "scalars parse", "[json]" ){
	CHECK( json::parse( "null" )->is_null() );
	CHECK( json::parse( "true" )->to_bool() );
	CHECK_FALSE( json::parse( "false" )->to_bool( true ) );
	CHECK( json::parse( "42" )->to_int() == 42 );
	CHECK( json::parse( "-3.5" )->to_number() == -3.5 );
	CHECK( json::parse( "1e3" )->to_number() == 1000.0 );
	CHECK( json::parse( "\"hi\"" )->to_string() == "hi" );
}

TEST_CASE( "structures parse and index", "[json]" ){
	auto const document = json::parse( R"({ "hosts": [ { "name": "web", "port": 22 } ], "version": 1 })" );
	REQUIRE( document.has_value() );

	CHECK( ( *document )["version"].to_int() == 1 );

	json::Array const& hosts = ( *document )["hosts"].array();
	REQUIRE( hosts.size() == 1 );
	CHECK( hosts[0]["name"].to_string() == "web" );
	CHECK( hosts[0]["port"].to_int() == 22 );

	// Missing keys and type mismatches fall back rather than throwing.
	CHECK( ( *document )["absent"].to_string( "fallback" ) == "fallback" );
	CHECK( ( *document )["version"].to_string( "fallback" ) == "fallback" );
}

TEST_CASE( "string escapes decode", "[json]" ){
	CHECK( json::parse( R"("a\"b\\c\/d\n\t")" )->to_string() == "a\"b\\c/d\n\t" );
	CHECK( json::parse( R"("A")" )->to_string() == "A" );
	CHECK( json::parse( R"("é")" )->to_string() == "é" );
	// A surrogate pair decodes to one astral code point.
	CHECK( json::parse( R"("😀")" )->to_string() == "😀" );
}

TEST_CASE( "malformed input is rejected", "[json]" ){
	CHECK_FALSE( json::parse( "" ).has_value() );
	CHECK_FALSE( json::parse( "{" ).has_value() );
	CHECK_FALSE( json::parse( "[1,]" ).has_value() );
	CHECK_FALSE( json::parse( "{\"a\":}" ).has_value() );
	CHECK_FALSE( json::parse( "\"unterminated" ).has_value() );
	CHECK_FALSE( json::parse( "tru" ).has_value() );
	CHECK_FALSE( json::parse( "1 2" ).has_value() );          // Trailing garbage.
	CHECK_FALSE( json::parse( R"("\ud800x")" ).has_value() ); // Lone high surrogate.
	CHECK_FALSE( json::parse( "\"raw\nnewline\"" ).has_value() );
}

TEST_CASE( "a document round-trips through dump and parse", "[json]" ){
	json::Object host;
	host.emplace( "name", "web-01" );
	host.emplace( "port", 2222 );
	host.emplace( "active", true );
	host.emplace( "note", "quotes \" and \\ and\nnewlines" );

	json::Object root;
	root.emplace( "version", 1 );
	root.emplace( "hosts", json::Array{ json::Value( std::move( host ) ) } );

	std::string const text     = json::dump( json::Value( std::move( root ) ) );
	auto const        reparsed = json::parse( text );

	REQUIRE( reparsed.has_value() );
	CHECK( ( *reparsed )["version"].to_int() == 1 );

	json::Value const& entry = ( *reparsed )["hosts"].array().at( 0 );
	CHECK( entry["name"].to_string() == "web-01" );
	CHECK( entry["port"].to_int() == 2222 );
	CHECK( entry["active"].to_bool() );
	CHECK( entry["note"].to_string() == "quotes \" and \\ and\nnewlines" );

	// Integers print as integers, not as "2222.0" or "2.222e3".
	CHECK( text.find( "2222" ) != std::string::npos );
	CHECK( text.find( "2222." ) == std::string::npos );
}
