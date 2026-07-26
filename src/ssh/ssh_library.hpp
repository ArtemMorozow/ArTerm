#pragma once

#include <mutex>

namespace arterm::ssh
{

	/// Process-wide libssh2 init/teardown. `libssh2_init` is not thread safe and
	/// must run exactly once before any session is created.
	class SshLibrary
	{
	public:
		static SshLibrary& instance();

		void               ensure_initialised();
		[[nodiscard]] bool is_initialised() const noexcept { return _initialised; }

		SshLibrary( SshLibrary const& )            = delete;
		SshLibrary& operator=( SshLibrary const& ) = delete;

	private:
		SshLibrary() = default;
		~SshLibrary();

		std::once_flag _once;
		bool           _initialised{ false };
	};

} // namespace arterm::ssh
