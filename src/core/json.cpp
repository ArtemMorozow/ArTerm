#include "core/json.hpp"

#include <cmath>
#include <cstdlib>
#include <format>

namespace arterm::json
{
	namespace
	{

		Value const  NULL_VALUE{};
		Array const  EMPTY_ARRAY{};
		Object const EMPTY_OBJECT{};

		// -- Parsing -----------------------------------------------------------

		class Parser
		{
		public:
			explicit Parser( std::string_view text )
				: _text( text )
			{}

			std::optional<Value> run(){
				skip_whitespace();
				auto value = parse_value();
				if( !value )
					return std::nullopt;
				skip_whitespace();
				if( _position != _text.size() ) // Trailing garbage.
					return std::nullopt;
				return value;
			}

		private:
			void skip_whitespace(){
				while( _position < _text.size() ){
					char const c = _text[_position];
					if( c != ' ' && c != '\t' && c != '\n' && c != '\r' )
						break;
					++_position;
				}
			}

			[[nodiscard]] bool consume( char expected ){
				if( _position < _text.size() && _text[_position] == expected ){
					++_position;
					return true;
				}
				return false;
			}

			[[nodiscard]] bool consume_word( std::string_view word ){
				if( _text.substr( _position, word.size() ) == word ){
					_position += word.size();
					return true;
				}
				return false;
			}

			std::optional<Value> parse_value(){
				if( _position >= _text.size() )
					return std::nullopt;

				switch( _text[_position] ){
					case '{':
						return parse_object();
					case '[':
						return parse_array();
					case '"':
						if( auto text = parse_string() )
							return Value( std::move( *text ) );
						return std::nullopt;
					case 't':
						return consume_word( "true" ) ? std::optional<Value>( Value( true ) ) : std::nullopt;
					case 'f':
						return consume_word( "false" ) ? std::optional<Value>( Value( false ) ) : std::nullopt;
					case 'n':
						return consume_word( "null" ) ? std::optional<Value>( Value( nullptr ) ) : std::nullopt;
					default:
						return parse_number();
				}
			}

			std::optional<Value> parse_object(){
				++_position; // '{'
				Object object;

				skip_whitespace();
				if( consume( '}' ) )
					return Value( std::move( object ) );

				while( true ){
					skip_whitespace();
					auto key = parse_string();
					if( !key )
						return std::nullopt;

					skip_whitespace();
					if( !consume( ':' ) )
						return std::nullopt;

					skip_whitespace();
					auto value = parse_value();
					if( !value )
						return std::nullopt;

					object.insert_or_assign( std::move( *key ), std::move( *value ) );

					skip_whitespace();
					if( consume( ',' ) )
						continue;
					if( consume( '}' ) )
						return Value( std::move( object ) );
					return std::nullopt;
				}
			}

			std::optional<Value> parse_array(){
				++_position; // '['
				Array array;

				skip_whitespace();
				if( consume( ']' ) )
					return Value( std::move( array ) );

				while( true ){
					skip_whitespace();
					auto value = parse_value();
					if( !value )
						return std::nullopt;
					array.push_back( std::move( *value ) );

					skip_whitespace();
					if( consume( ',' ) )
						continue;
					if( consume( ']' ) )
						return Value( std::move( array ) );
					return std::nullopt;
				}
			}

			std::optional<std::string> parse_string(){
				if( !consume( '"' ) )
					return std::nullopt;

				std::string out;
				while( _position < _text.size() ){
					char const c = _text[_position++];

					if( c == '"' )
						return out;

					if( static_cast<unsigned char>( c ) < 0x20 ) // Raw control byte.
						return std::nullopt;

					if( c != '\\' ){
						out.push_back( c );
						continue;
					}

					if( _position >= _text.size() )
						return std::nullopt;

					char const escape = _text[_position++];
					switch( escape ){
						case '"':
							out.push_back( '"' );
							break;
						case '\\':
							out.push_back( '\\' );
							break;
						case '/':
							out.push_back( '/' );
							break;
						case 'b':
							out.push_back( '\b' );
							break;
						case 'f':
							out.push_back( '\f' );
							break;
						case 'n':
							out.push_back( '\n' );
							break;
						case 'r':
							out.push_back( '\r' );
							break;
						case 't':
							out.push_back( '\t' );
							break;
						case 'u':{
							auto unit = parse_hex4();
							if( !unit )
								return std::nullopt;

							char32_t code_point = *unit;
							// Surrogate pair: a high surrogate must be followed by \uDC00..\uDFFF.
							if( code_point >= 0xD800 && code_point <= 0xDBFF ){
								if( !consume( '\\' ) || !consume( 'u' ) )
									return std::nullopt;
								auto low = parse_hex4();
								if( !low || *low < 0xDC00 || *low > 0xDFFF )
									return std::nullopt;
								code_point = 0x10000 + ( ( code_point - 0xD800 ) << 10 ) + ( *low - 0xDC00 );
							}
							else if( code_point >= 0xDC00 && code_point <= 0xDFFF ){
								return std::nullopt; // Lone low surrogate.
							}

							append_utf8( out, code_point );
							break;
						}
						default:
							return std::nullopt;
					}
				}

				return std::nullopt; // Unterminated string.
			}

			std::optional<char32_t> parse_hex4(){
				if( _position + 4 > _text.size() )
					return std::nullopt;

				char32_t value = 0;
				for( int i = 0; i < 4; ++i ){
					char const c = _text[_position++];
					value <<= 4;
					if( c >= '0' && c <= '9' )
						value |= static_cast<char32_t>( c - '0' );
					else if( c >= 'a' && c <= 'f' )
						value |= static_cast<char32_t>( c - 'a' + 10 );
					else if( c >= 'A' && c <= 'F' )
						value |= static_cast<char32_t>( c - 'A' + 10 );
					else
						return std::nullopt;
				}
				return value;
			}

			static void append_utf8( std::string& out, char32_t code_point ){
				if( code_point < 0x80 ){
					out.push_back( static_cast<char>( code_point ) );
				}
				else if( code_point < 0x800 ){
					out.push_back( static_cast<char>( 0xC0 | ( code_point >> 6 ) ) );
					out.push_back( static_cast<char>( 0x80 | ( code_point & 0x3F ) ) );
				}
				else if( code_point < 0x10000 ){
					out.push_back( static_cast<char>( 0xE0 | ( code_point >> 12 ) ) );
					out.push_back( static_cast<char>( 0x80 | ( ( code_point >> 6 ) & 0x3F ) ) );
					out.push_back( static_cast<char>( 0x80 | ( code_point & 0x3F ) ) );
				}
				else{
					out.push_back( static_cast<char>( 0xF0 | ( code_point >> 18 ) ) );
					out.push_back( static_cast<char>( 0x80 | ( ( code_point >> 12 ) & 0x3F ) ) );
					out.push_back( static_cast<char>( 0x80 | ( ( code_point >> 6 ) & 0x3F ) ) );
					out.push_back( static_cast<char>( 0x80 | ( code_point & 0x3F ) ) );
				}
			}

			std::optional<Value> parse_number(){
				std::size_t end = _position;
				if( end < _text.size() && _text[end] == '-' )
					++end;
				while( end < _text.size() ){
					char const c = _text[end];
					if( ( c >= '0' && c <= '9' ) || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-' )
						++end;
					else
						break;
				}
				if( end == _position )
					return std::nullopt;

				// std::from_chars for floating point needs a newer libc++ than the
				// deployment target guarantees, so the token goes through strtod.
				std::string const token( _text.substr( _position, end - _position ) );
				char*             parsed_end = nullptr;
				double const      value      = std::strtod( token.c_str(), &parsed_end );
				if( parsed_end != token.c_str() + token.size() )
					return std::nullopt;

				_position = end;
				return Value( value );
			}

			std::string_view _text;
			std::size_t      _position{ 0 };
		};

		// -- Writing -----------------------------------------------------------

		void write_string( std::string& out, std::string_view text ){
			out.push_back( '"' );
			for( char const c : text ){
				switch( c ){
					case '"':
						out += "\\\"";
						break;
					case '\\':
						out += "\\\\";
						break;
					case '\b':
						out += "\\b";
						break;
					case '\f':
						out += "\\f";
						break;
					case '\n':
						out += "\\n";
						break;
					case '\r':
						out += "\\r";
						break;
					case '\t':
						out += "\\t";
						break;
					default:
						if( static_cast<unsigned char>( c ) < 0x20 )
							out += std::format( "\\u{:04x}", static_cast<unsigned char>( c ) );
						else
							out.push_back( c ); // UTF-8 passes through verbatim.
				}
			}
			out.push_back( '"' );
		}

		void write_number( std::string& out, double value ){
			// Integers print without an exponent or trailing ".0", so ports and
			// timeouts read back the way a human wrote them.
			if( std::nearbyint( value ) == value && std::abs( value ) < 1e15 )
				out += std::format( "{}", static_cast<long long>( value ) );
			else
				out += std::format( "{}", value );
		}

		void write_value( std::string& out, Value const& value, int depth ){
			std::string const indent( static_cast<std::size_t>( depth ) * 2, ' ' );
			std::string const inner( ( static_cast<std::size_t>( depth ) + 1 ) * 2, ' ' );

			if( value.is_null() ){
				out += "null";
			}
			else if( value.is_bool() ){
				out += value.to_bool() ? "true" : "false";
			}
			else if( value.is_number() ){
				write_number( out, value.to_number() );
			}
			else if( value.is_string() ){
				write_string( out, value.to_string() );
			}
			else if( value.is_array() ){
				Array const& array = value.array();
				if( array.empty() ){
					out += "[]";
					return;
				}
				out += "[\n";
				for( std::size_t i = 0; i < array.size(); ++i ){
					out += inner;
					write_value( out, array[i], depth + 1 );
					if( i + 1 < array.size() )
						out.push_back( ',' );
					out.push_back( '\n' );
				}
				out += indent + "]";
			}
			else{
				Object const& object = value.object();
				if( object.empty() ){
					out += "{}";
					return;
				}
				out += "{\n";
				std::size_t remaining = object.size();
				for( auto const& [key, member] : object ){
					out += inner;
					write_string( out, key );
					out += ": ";
					write_value( out, member, depth + 1 );
					if( --remaining > 0 )
						out.push_back( ',' );
					out.push_back( '\n' );
				}
				out += indent + "}";
			}
		}

	} // namespace

	bool Value::to_bool( bool fallback ) const{
		auto const* value = std::get_if<bool>( &_data );
		return value != nullptr ? *value : fallback;
	}

	double Value::to_number( double fallback ) const{
		auto const* value = std::get_if<double>( &_data );
		return value != nullptr ? *value : fallback;
	}

	int Value::to_int( int fallback ) const{
		auto const* value = std::get_if<double>( &_data );
		return value != nullptr ? static_cast<int>( *value ) : fallback;
	}

	std::string Value::to_string( std::string fallback ) const{
		auto const* value = std::get_if<std::string>( &_data );
		return value != nullptr ? *value : fallback;
	}

	Array const& Value::array() const{
		auto const* value = std::get_if<Array>( &_data );
		return value != nullptr ? *value : EMPTY_ARRAY;
	}

	Object const& Value::object() const{
		auto const* value = std::get_if<Object>( &_data );
		return value != nullptr ? *value : EMPTY_OBJECT;
	}

	Value const& Value::operator[]( std::string_view key ) const{
		auto const* value = std::get_if<Object>( &_data );
		if( value == nullptr )
			return NULL_VALUE;

		auto const found = value->find( key );
		return found != value->end() ? found->second : NULL_VALUE;
	}

	std::optional<Value> parse( std::string_view text ){
		return Parser( text ).run();
	}

	std::string dump( Value const& value ){
		std::string out;
		write_value( out, value, 0 );
		out.push_back( '\n' );
		return out;
	}

} // namespace arterm::json
