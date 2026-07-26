#include "core/base64.hpp"

#include <array>
#include <cstdint>

namespace arterm
{
	namespace
	{

		constexpr std::string_view ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		constexpr std::array<std::int8_t, 256> make_reverse_table(){
			std::array<std::int8_t, 256> table{};
			for( auto& entry : table )
				entry = -1;
			for( std::size_t i = 0; i < ALPHABET.size(); ++i )
				table[static_cast<unsigned char>( ALPHABET[i] )] = static_cast<std::int8_t>( i );
			return table;
		}

		constexpr std::array<std::int8_t, 256> REVERSE = make_reverse_table();

	} // namespace

	std::string base64_encode( std::string_view data ){
		std::string out;
		out.reserve( ( data.size() + 2 ) / 3 * 4 );

		std::size_t index = 0;
		while( index + 2 < data.size() ){
			std::uint32_t const group = ( static_cast<unsigned char>( data[index] ) << 16 ) |
										( static_cast<unsigned char>( data[index + 1] ) << 8 ) |
										static_cast<unsigned char>( data[index + 2] );
			out.push_back( ALPHABET[( group >> 18 ) & 0x3F] );
			out.push_back( ALPHABET[( group >> 12 ) & 0x3F] );
			out.push_back( ALPHABET[( group >> 6 ) & 0x3F] );
			out.push_back( ALPHABET[group & 0x3F] );
			index += 3;
		}

		std::size_t const remaining = data.size() - index;
		if( remaining == 1 ){
			std::uint32_t const group = static_cast<unsigned char>( data[index] ) << 16;
			out.push_back( ALPHABET[( group >> 18 ) & 0x3F] );
			out.push_back( ALPHABET[( group >> 12 ) & 0x3F] );
			out.append( "==" );
		}
		else if( remaining == 2 ){
			std::uint32_t const group = ( static_cast<unsigned char>( data[index] ) << 16 ) |
										( static_cast<unsigned char>( data[index + 1] ) << 8 );
			out.push_back( ALPHABET[( group >> 18 ) & 0x3F] );
			out.push_back( ALPHABET[( group >> 12 ) & 0x3F] );
			out.push_back( ALPHABET[( group >> 6 ) & 0x3F] );
			out.push_back( '=' );
		}

		return out;
	}

	std::optional<std::string> base64_decode( std::string_view text ){
		if( text.size() % 4 != 0 )
			return std::nullopt;

		std::size_t padding = 0;
		while( padding < 2 && padding < text.size() && text[text.size() - 1 - padding] == '=' )
			++padding;

		std::string out;
		out.reserve( text.size() / 4 * 3 );

		for( std::size_t index = 0; index < text.size(); index += 4 ){
			std::uint32_t group = 0;
			for( std::size_t offset = 0; offset < 4; ++offset ){
				char const character = text[index + offset];
				if( character == '=' ){
					// Padding is only legal in the final group's last two positions.
					if( index + 4 != text.size() || offset + padding < 4 )
						return std::nullopt;
					group <<= 6;
					continue;
				}

				std::int8_t const value = REVERSE[static_cast<unsigned char>( character )];
				if( value < 0 )
					return std::nullopt;
				group = ( group << 6 ) | static_cast<std::uint32_t>( value );
			}

			out.push_back( static_cast<char>( ( group >> 16 ) & 0xFF ) );
			if( index + 4 != text.size() || padding < 2 )
				out.push_back( static_cast<char>( ( group >> 8 ) & 0xFF ) );
			if( index + 4 != text.size() || padding < 1 )
				out.push_back( static_cast<char>( group & 0xFF ) );
		}

		return out;
	}

} // namespace arterm
