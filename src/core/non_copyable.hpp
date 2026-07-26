#pragma once

namespace arterm
{

	/// Base for types that own a resource and must not be copied. Move stays
	/// available to whoever declares it explicitly.
	class NonCopyable
	{
	protected:
		NonCopyable()  = default;
		~NonCopyable() = default;

		NonCopyable( NonCopyable const& )            = delete;
		NonCopyable& operator=( NonCopyable const& ) = delete;
	};

} // namespace arterm
