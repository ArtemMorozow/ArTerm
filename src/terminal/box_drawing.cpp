#include "terminal/box_drawing.hpp"

namespace arterm::term
{
	namespace
	{

		constexpr Stroke N = Stroke::NONE;
		constexpr Stroke L = Stroke::LIGHT;
		constexpr Stroke H = Stroke::HEAVY;
		constexpr Stroke D = Stroke::DOUBLE;

		/// U+2500..U+254B in code point order: the light/heavy line set. Every
		/// entry is { up, down, left, right }.
		constexpr BoxGlyph LINES[] = {
			{ N, N, L, L }, // 2500 ─
			{ N, N, H, H }, // 2501 ━
			{ L, L, N, N }, // 2502 │
			{ H, H, N, N }, // 2503 ┃
			{ N, N, L, L }, // 2504 ┄ dashed, drawn solid
			{ N, N, H, H }, // 2505 ┅
			{ L, L, N, N }, // 2506 ┆
			{ H, H, N, N }, // 2507 ┇
			{ N, N, L, L }, // 2508 ┈
			{ N, N, H, H }, // 2509 ┉
			{ L, L, N, N }, // 250A ┊
			{ H, H, N, N }, // 250B ┋
			{ N, L, N, L }, // 250C ┌
			{ N, L, N, H }, // 250D ┍
			{ N, H, N, L }, // 250E ┎
			{ N, H, N, H }, // 250F ┏
			{ N, L, L, N }, // 2510 ┐
			{ N, L, H, N }, // 2511 ┑
			{ N, H, L, N }, // 2512 ┒
			{ N, H, H, N }, // 2513 ┓
			{ L, N, N, L }, // 2514 └
			{ L, N, N, H }, // 2515 ┕
			{ H, N, N, L }, // 2516 ┖
			{ H, N, N, H }, // 2517 ┗
			{ L, N, L, N }, // 2518 ┘
			{ L, N, H, N }, // 2519 ┙
			{ H, N, L, N }, // 251A ┚
			{ H, N, H, N }, // 251B ┛
			{ L, L, N, L }, // 251C ├
			{ L, L, N, H }, // 251D ┝
			{ H, L, N, L }, // 251E ┞
			{ L, H, N, L }, // 251F ┟
			{ H, H, N, L }, // 2520 ┠
			{ H, L, N, H }, // 2521 ┡
			{ L, H, N, H }, // 2522 ┢
			{ H, H, N, H }, // 2523 ┣
			{ L, L, L, N }, // 2524 ┤
			{ L, L, H, N }, // 2525 ┥
			{ H, L, L, N }, // 2526 ┦
			{ L, H, L, N }, // 2527 ┧
			{ H, H, L, N }, // 2528 ┨
			{ H, L, H, N }, // 2529 ┩
			{ L, H, H, N }, // 252A ┪
			{ H, H, H, N }, // 252B ┫
			{ N, L, L, L }, // 252C ┬
			{ N, L, H, L }, // 252D ┭
			{ N, L, L, H }, // 252E ┮
			{ N, L, H, H }, // 252F ┯
			{ N, H, L, L }, // 2530 ┰
			{ N, H, H, L }, // 2531 ┱
			{ N, H, L, H }, // 2532 ┲
			{ N, H, H, H }, // 2533 ┳
			{ L, N, L, L }, // 2534 ┴
			{ L, N, H, L }, // 2535 ┵
			{ L, N, L, H }, // 2536 ┶
			{ L, N, H, H }, // 2537 ┷
			{ H, N, L, L }, // 2538 ┸
			{ H, N, H, L }, // 2539 ┹
			{ H, N, L, H }, // 253A ┺
			{ H, N, H, H }, // 253B ┻
			{ L, L, L, L }, // 253C ┼
			{ L, L, H, L }, // 253D ┽
			{ L, L, L, H }, // 253E ┾
			{ L, L, H, H }, // 253F ┿
			{ H, L, L, L }, // 2540 ╀
			{ L, H, L, L }, // 2541 ╁
			{ H, H, L, L }, // 2542 ╂
			{ H, L, H, L }, // 2543 ╃
			{ H, L, L, H }, // 2544 ╄
			{ L, H, H, L }, // 2545 ╅
			{ L, H, L, H }, // 2546 ╆
			{ H, L, H, H }, // 2547 ╇
			{ L, H, H, H }, // 2548 ╈
			{ H, H, H, L }, // 2549 ╉
			{ H, H, L, H }, // 254A ╊
			{ H, H, H, H }, // 254B ╋
			{ N, N, L, L }, // 254C ╌ dashed, drawn solid
			{ N, N, H, H }, // 254D ╍
			{ L, L, N, N }, // 254E ╎
			{ H, H, N, N }, // 254F ╏
		};

		/// U+2550..U+256C: the double-line set, which mixes single and double
		/// arms in ways the light/heavy table cannot express.
		constexpr BoxGlyph DOUBLES[] = {
			{ N, N, D, D }, // 2550 ═
			{ D, D, N, N }, // 2551 ║
			{ N, L, N, D }, // 2552 ╒
			{ N, D, N, L }, // 2553 ╓
			{ N, D, N, D }, // 2554 ╔
			{ N, L, D, N }, // 2555 ╕
			{ N, D, L, N }, // 2556 ╖
			{ N, D, D, N }, // 2557 ╗
			{ L, N, N, D }, // 2558 ╘
			{ D, N, N, L }, // 2559 ╙
			{ D, N, N, D }, // 255A ╚
			{ L, N, D, N }, // 255B ╛
			{ D, N, L, N }, // 255C ╜
			{ D, N, D, N }, // 255D ╝
			{ L, L, N, D }, // 255E ╞
			{ D, D, N, L }, // 255F ╟
			{ D, D, N, D }, // 2560 ╠
			{ L, L, D, N }, // 2561 ╡
			{ D, D, L, N }, // 2562 ╢
			{ D, D, D, N }, // 2563 ╣
			{ N, L, D, D }, // 2564 ╤
			{ N, D, L, L }, // 2565 ╥
			{ N, D, D, D }, // 2566 ╦
			{ L, N, D, D }, // 2567 ╧
			{ D, N, L, L }, // 2568 ╨
			{ D, N, D, D }, // 2569 ╩
			{ L, L, D, D }, // 256A ╪
			{ D, D, L, L }, // 256B ╫
			{ D, D, D, D }, // 256C ╬
		};

	} // namespace

	std::optional<BoxGlyph> box_glyph_for( char32_t code_point ) noexcept{
		if( code_point >= 0x2500 && code_point <= 0x254F )
			return LINES[code_point - 0x2500];

		if( code_point >= 0x2550 && code_point <= 0x256C )
			return DOUBLES[code_point - 0x2550];

		// The rounded corners draw the same as the square ones; the radius is
		// lost, which nobody notices at terminal sizes.
		switch( code_point ){
			case 0x256D: // ╭
				return BoxGlyph{ N, L, N, L };
			case 0x256E: // ╮
				return BoxGlyph{ N, L, L, N };
			case 0x256F: // ╯
				return BoxGlyph{ L, N, L, N };
			case 0x2570: // ╰
				return BoxGlyph{ L, N, N, L };
			case 0x2574: // ╴
				return BoxGlyph{ N, N, L, N };
			case 0x2575: // ╵
				return BoxGlyph{ L, N, N, N };
			case 0x2576: // ╶
				return BoxGlyph{ N, N, N, L };
			case 0x2577: // ╷
				return BoxGlyph{ N, L, N, N };
			case 0x2578: // ╸
				return BoxGlyph{ N, N, H, N };
			case 0x2579: // ╹
				return BoxGlyph{ H, N, N, N };
			case 0x257A: // ╺
				return BoxGlyph{ N, N, N, H };
			case 0x257B: // ╻
				return BoxGlyph{ N, H, N, N };
			case 0x257C: // ╼
				return BoxGlyph{ N, N, L, H };
			case 0x257D: // ╽
				return BoxGlyph{ L, H, N, N };
			case 0x257E: // ╾
				return BoxGlyph{ N, N, H, L };
			case 0x257F: // ╿
				return BoxGlyph{ H, L, N, N };
			default:
				return std::nullopt;
		}
	}

	std::optional<BlockGlyph> block_glyph_for( char32_t code_point ) noexcept{
		switch( code_point ){
			case 0x2580:
				return BlockGlyph::UPPER_HALF;
			case 0x2584:
				return BlockGlyph::LOWER_HALF;
			case 0x2588:
				return BlockGlyph::FULL;
			case 0x258C:
				return BlockGlyph::LEFT_HALF;
			case 0x2590:
				return BlockGlyph::RIGHT_HALF;
			case 0x2591:
				return BlockGlyph::LIGHT_SHADE;
			case 0x2592:
				return BlockGlyph::MEDIUM_SHADE;
			case 0x2593:
				return BlockGlyph::DARK_SHADE;
			default:
				return std::nullopt;
		}
	}

} // namespace arterm::term
