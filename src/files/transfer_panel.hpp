#pragma once

#include <QStyledItemDelegate>
#include <QWidget>

class QLabel;
class QTableView;
class QToolButton;

namespace arterm::files
{

	class TransferManager;

	/// Draws the progress column as a slim bar plus its percentage.
	class TransferProgressDelegate : public QStyledItemDelegate
	{
		Q_OBJECT

	public:
		using QStyledItemDelegate::QStyledItemDelegate;

		void paint( QPainter* painter, QStyleOptionViewItem const& option, QModelIndex const& index ) const override;
		[[nodiscard]] QSize sizeHint( QStyleOptionViewItem const& option, QModelIndex const& index ) const override;
	};

	/// The collapsible queue shown under the file browser.
	class TransferPanel : public QWidget
	{
		Q_OBJECT

	public:
		explicit TransferPanel( TransferManager* manager, QWidget* parent = nullptr );

		/// Expands the panel; called when a transfer starts.
		void               expand();
		void               collapse();
		[[nodiscard]] bool is_expanded() const noexcept { return _expanded; }

		/// Height of the header strip, which is all that remains when collapsed.
		[[nodiscard]] static int header_height() noexcept;

	Q_SIGNALS:
		/// The owner re-sizes its splitter in response; the panel deliberately does
		/// not pin its own height, because doing so squeezed the file lists to
		/// nothing when a transfer started.
		void expanded_changed( bool expanded );

	private:
		void update_header();

		TransferManager* _manager;
		QTableView*      _table{ nullptr };
		QLabel*          _header_label{ nullptr };
		QToolButton*     _toggle_button{ nullptr };
		QToolButton*     _clear_button{ nullptr };
		QToolButton*     _cancel_button{ nullptr };
		bool             _expanded{ false };
	};

} // namespace arterm::files
