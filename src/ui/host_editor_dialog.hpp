#pragma once

#include "ssh/ssh_types.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QStackedWidget;

namespace arterm::ui
{

	/// Create or edit one host profile.
	class HostEditorDialog : public QDialog
	{
		Q_OBJECT

	public:
		explicit HostEditorDialog( QWidget* parent = nullptr );

		void                           set_profile( ssh::HostProfile const& profile );
		[[nodiscard]] ssh::HostProfile profile() const;

	private:
		void               build_ui();
		void               update_auth_page();
		void               browse_for_key();
		[[nodiscard]] bool validate();

		QLineEdit* _label{ nullptr };
		QLineEdit* _hostname{ nullptr };
		QSpinBox*  _port{ nullptr };
		QLineEdit* _username{ nullptr };
		QComboBox* _group{ nullptr };

		QComboBox*      _auth_method{ nullptr };
		QStackedWidget* _auth_pages{ nullptr };
		QLineEdit*      _password{ nullptr };
		QLineEdit*      _key_path{ nullptr };
		QLineEdit*      _key_passphrase{ nullptr };
		QCheckBox*      _save_password{ nullptr };

		QLineEdit* _startup_directory{ nullptr };
		QLineEdit* _startup_command{ nullptr };
		QCheckBox* _open_file_browser{ nullptr };
		QCheckBox* _compression{ nullptr };
		QCheckBox* _strict_host_key{ nullptr };
		QSpinBox*  _keep_alive{ nullptr };
		QComboBox* _transfer_backend{ nullptr };

		ssh::HostProfile _profile;
	};

} // namespace arterm::ui
