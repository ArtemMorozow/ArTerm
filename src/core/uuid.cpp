#include "core/uuid.hpp"

#include <array>
#include <cstdint>
#include <format>

#include <stdlib.h>

namespace arterm
{

	std::string generate_uuid(){
		std::array<std::uint8_t, 16> bytes{};
		arc4random_buf( bytes.data(), bytes.size() );

		// RFC 4122: version 4, variant 10xx.
		bytes[6] = static_cast<std::uint8_t>( ( bytes[6] & 0x0F ) | 0x40 );
		bytes[8] = static_cast<std::uint8_t>( ( bytes[8] & 0x3F ) | 0x80 );

		return std::format( "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-"
							"{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
							bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8],
							bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15] );
	}

} // namespace arterm
