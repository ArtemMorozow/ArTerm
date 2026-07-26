#pragma once

#include "ssh/ssh_types.hpp"

#include <QDialog>

namespace arterm::ui
{

	/// Shows the fingerprint of an unknown or changed host key and asks whether to
	/// trust it.
	///
	/// A mismatch is presented very differently from a first connection: the same
	/// dialog with a softer wording would train users to click through the one case
	/// that actually matters.
	class HostKeyDialog : public QDialog
	{
		Q_OBJECT

	public:
		explicit HostKeyDialog( ssh::HostKeyInfo const& info, QWidget* parent = nullptr );

		/// Runs the dialog and returns whether the key was accepted.
		[[nodiscard]] static bool ask( ssh::HostKeyInfo const& info, QWidget* parent );
	};

} // namespace arterm::ui
