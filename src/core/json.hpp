#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace arterm::json
{

	/// A JSON document as a value type. Small on purpose: ArTerm stores one host
	/// list and one drag payload, not arbitrary documents.
	///
	/// Object keys are kept sorted (std::map), so a saved file diffs cleanly.
	class Value;

	using Array  = std::vector<Value>;
	using Object = std::map<std::string, Value, std::less<>>;

	class Value
	{
	public:
		Value() = default;
		Value( std::nullptr_t ) {}
		Value( bool value )
			: _data( value )
		{}
		Value( double value )
			: _data( value )
		{}
		Value( int value )
			: _data( static_cast<double>( value ) )
		{}
		Value( std::int64_t value )
			: _data( static_cast<double>( value ) )
		{}
		Value( char const* value )
			: _data( std::string( value ) )
		{}
		Value( std::string value )
			: _data( std::move( value ) )
		{}
		Value( Array value )
			: _data( std::move( value ) )
		{}
		Value( Object value )
			: _data( std::move( value ) )
		{}

		[[nodiscard]] bool is_null() const noexcept { return std::holds_alternative<std::nullptr_t>( _data ); }
		[[nodiscard]] bool is_bool() const noexcept { return std::holds_alternative<bool>( _data ); }
		[[nodiscard]] bool is_number() const noexcept { return std::holds_alternative<double>( _data ); }
		[[nodiscard]] bool is_string() const noexcept { return std::holds_alternative<std::string>( _data ); }
		[[nodiscard]] bool is_array() const noexcept { return std::holds_alternative<Array>( _data ); }
		[[nodiscard]] bool is_object() const noexcept { return std::holds_alternative<Object>( _data ); }

		/// The `to_*` accessors return the fallback when the value has another type,
		/// which is what reading a config file wants: absent and wrong both mean
		/// "use the default".
		[[nodiscard]] bool        to_bool( bool fallback = false ) const;
		[[nodiscard]] double      to_number( double fallback = 0.0 ) const;
		[[nodiscard]] int         to_int( int fallback = 0 ) const;
		[[nodiscard]] std::string to_string( std::string fallback = {} ) const;

		[[nodiscard]] Array const&  array() const;
		[[nodiscard]] Object const& object() const;

		/// Object lookup; returns a shared null for a missing key or a non-object.
		[[nodiscard]] Value const& operator[]( std::string_view key ) const;

	private:
		std::variant<std::nullptr_t, bool, double, std::string, Array, Object> _data{ nullptr };
	};

	/// Strict parse: the whole input must be one JSON document.
	[[nodiscard]] std::optional<Value> parse( std::string_view text );

	/// Two-space indented, keys sorted, UTF-8 passed through verbatim.
	[[nodiscard]] std::string dump( Value const& value );

} // namespace arterm::json
