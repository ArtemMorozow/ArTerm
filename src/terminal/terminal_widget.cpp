#include "terminal/terminal_widget.hpp"

#include "terminal/char_width.hpp"
#include "terminal/key_encoder.hpp"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace arterm::term
{
	namespace
	{

		constexpr int CURSOR_BLINK_INTERVAL_MS = 530;
		constexpr int MINIMUM_FONT_POINT_SIZE  = 7;
		constexpr int MAXIMUM_FONT_POINT_SIZE  = 42;

		/// Characters that count as part of a word for double-click selection. Path
		/// separators and URL punctuation are included so a double click grabs a whole
		/// path, which is what the file-manager workflow needs.
		bool is_word_character( char32_t code_point ){
			if( code_point >= U'0' && code_point <= U'9' )
				return true;
			if( code_point >= U'a' && code_point <= U'z' )
				return true;
			if( code_point >= U'A' && code_point <= U'Z' )
				return true;
			if( code_point > 0x7F )
				return true;

			switch( code_point ){
				case U'_':
				case U'-':
				case U'.':
				case U'/':
				case U'~':
				case U':':
				case U'+':
				case U'@':
				case U'%':
				case U'=':
				case U'?':
				case U'&':
				case U'#':
					return true;
				default:
					return false;
			}
		}

		QFont default_monospace_font(){
			// Prefer the fonts that actually ship with macOS before falling back to
			// whatever Qt considers fixed-pitch.
			QStringList const candidates = { QStringLiteral( "SF Mono" ), QStringLiteral( "Menlo" ),
											 QStringLiteral( "JetBrains Mono" ), QStringLiteral( "Monaco" ),
											 QStringLiteral( "DejaVu Sans Mono" ) };

			QStringList const families = QFontDatabase::families();
			for( QString const& candidate : candidates ){
				if( families.contains( candidate, Qt::CaseInsensitive ) ){
					QFont font( candidate );
					font.setStyleHint( QFont::Monospace );
					return font;
				}
			}

			QFont font = QFontDatabase::systemFont( QFontDatabase::FixedFont );
			font.setStyleHint( QFont::Monospace );
			return font;
		}

	} // namespace

	TerminalWidget::TerminalWidget( QWidget* parent )
		: QAbstractScrollArea( parent )
		, _scheme( ColorScheme::arterm_dark() )
	{
		setAttribute( Qt::WA_OpaquePaintEvent );
		setAttribute( Qt::WA_InputMethodEnabled );
		setFocusPolicy( Qt::StrongFocus );
		setAcceptDrops( true );
		viewport()->setCursor( Qt::IBeamCursor );
		setFrameStyle( QFrame::NoFrame );
		setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
		setVerticalScrollBarPolicy( Qt::ScrollBarAsNeeded );

		_font = default_monospace_font();
		_font.setPointSize( _base_font_point_size );
		_font.setFixedPitch( true );
		_font.setHintingPreference( QFont::PreferFullHinting );

		_terminal = std::make_unique<Terminal>( _columns, _rows, this );

		connect( _terminal.get(), &Terminal::reply, this, &TerminalWidget::data_entered );
		connect( _terminal.get(), &Terminal::title_changed, this, &TerminalWidget::title_changed );
		connect( _terminal.get(), &Terminal::bell_rang, this, &TerminalWidget::bell_rang );
		connect( _terminal.get(), &Terminal::clipboard_write_requested, this,
				 []( QString const& text ) { QGuiApplication::clipboard()->setText( text ); } );
		connect( _terminal.get(), &Terminal::screen_changed, this, [this]{
			update_scroll_bar();
			viewport()->update();
		} );
		connect( _terminal.get(), &Terminal::alternate_screen_changed, this, [this]( bool active ){
			// The alternate buffer has no history, so hide the scrollbar entirely.
			setVerticalScrollBarPolicy( active ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded );
			clear_selection();
			update_scroll_bar();
		} );

		_blink_timer = new QTimer( this );
		_blink_timer->setInterval( CURSOR_BLINK_INTERVAL_MS );
		connect( _blink_timer, &QTimer::timeout, this, [this]{
			_cursor_on = !_cursor_on;
			viewport()->update();
		} );
		_blink_timer->start();

		connect( verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { viewport()->update(); } );

		update_metrics();
	}

	TerminalWidget::~TerminalWidget() = default;

	// ---------------------------------------------------------------------------
	// Configuration
	// ---------------------------------------------------------------------------

	void TerminalWidget::set_color_scheme( ColorScheme const& scheme ){
		_scheme = scheme;

		QPalette palette = viewport()->palette();
		palette.setColor( QPalette::Window, _scheme.background() );
		palette.setColor( QPalette::Base, _scheme.background() );
		viewport()->setPalette( palette );
		viewport()->setAutoFillBackground( true );

		viewport()->update();
	}

	void TerminalWidget::set_terminal_font( QFont const& font ){
		_font = font;
		_font.setFixedPitch( true );
		_base_font_point_size = font.pointSize() > 0 ? font.pointSize() : _base_font_point_size;
		update_metrics();
	}

	void TerminalWidget::set_line_spacing( double factor ){
		_line_spacing_factor = std::clamp( factor, 0.0, 1.0 );
		update_metrics();
	}

	void TerminalWidget::set_scrollback_limit( int lines ){
		_terminal->set_scrollback_limit( lines );
		update_scroll_bar();
	}

	void TerminalWidget::set_cursor_blinking( bool enabled ){
		_cursor_blinking = enabled;
		if( enabled ){
			_blink_timer->start();
		}
		else{
			_blink_timer->stop();
			_cursor_on = true;
			viewport()->update();
		}
	}

	void TerminalWidget::update_metrics(){
		_bold_font = _font;
		_bold_font.setBold( true );
		_italic_font = _font;
		_italic_font.setItalic( true );

		QFontMetricsF const metrics( _font );

		// Use the advance of a representative glyph rather than maxWidth: with a
		// proportional fallback font maxWidth is far too large and the grid ends up
		// riddled with gaps.
		_cell_width = metrics.horizontalAdvance( QLatin1Char( 'W' ) );
		if( _cell_width <= 0.0 )
			_cell_width = metrics.averageCharWidth();

		qreal const spacing = std::round( metrics.height() * _line_spacing_factor );
		_cell_height        = std::ceil( metrics.height() + spacing );
		_baseline           = metrics.ascent() + spacing / 2.0;

		update_grid_size();
		viewport()->update();
	}

	void TerminalWidget::increase_font_size(){
		if( _font.pointSize() >= MAXIMUM_FONT_POINT_SIZE )
			return;
		_font.setPointSize( _font.pointSize() + 1 );
		update_metrics();
	}

	void TerminalWidget::decrease_font_size(){
		if( _font.pointSize() <= MINIMUM_FONT_POINT_SIZE )
			return;
		_font.setPointSize( _font.pointSize() - 1 );
		update_metrics();
	}

	void TerminalWidget::reset_font_size(){
		_font.setPointSize( _base_font_point_size );
		update_metrics();
	}

	// ---------------------------------------------------------------------------
	// Geometry
	// ---------------------------------------------------------------------------

	QSize TerminalWidget::sizeHint() const{
		return QSize( static_cast<int>( std::ceil( _cell_width * 80 ) ) + verticalScrollBar()->sizeHint().width(),
					  static_cast<int>( std::ceil( _cell_height * 24 ) ) );
	}

	void TerminalWidget::update_grid_size(){
		QSize const available = viewport()->size();
		if( available.isEmpty() || _cell_width <= 0.0 || _cell_height <= 0.0 )
			return;

		int const columns = std::max( 1, static_cast<int>( available.width() / _cell_width ) );
		int const rows    = std::max( 1, static_cast<int>( available.height() / _cell_height ) );

		if( columns == _columns && rows == _rows )
			return;

		_columns = columns;
		_rows    = rows;
		_terminal->resize( columns, rows );
		clear_selection();
		update_scroll_bar();

		Q_EMIT terminal_resized( columns, rows, available.width(), available.height() );
	}

	void TerminalWidget::update_scroll_bar(){
		QScrollBar* bar     = verticalScrollBar();
		int const   history = _terminal->modes().alternate_screen ? 0 : _terminal->screen().scrollback_size();

		bool const was_at_bottom = is_following_output();

		// The scrollbar addresses history: [0, history] where history means "live".
		bar->setRange( 0, history );
		bar->setPageStep( _rows );
		bar->setSingleStep( 1 );

		if( was_at_bottom )
			bar->setValue( history );
	}

	bool TerminalWidget::is_following_output() const{
		QScrollBar const* bar = verticalScrollBar();
		return bar->value() >= bar->maximum();
	}

	int TerminalWidget::top_visible_row() const{
		// value == maximum means the live grid starts at row 0.
		return verticalScrollBar()->value() - verticalScrollBar()->maximum();
	}

	void TerminalWidget::scroll_to_bottom(){
		verticalScrollBar()->setValue( verticalScrollBar()->maximum() );
	}

	void TerminalWidget::resizeEvent( QResizeEvent* event ){
		QAbstractScrollArea::resizeEvent( event );
		update_grid_size();
	}

	Line const* TerminalWidget::line_at( int history_row ) const{
		return _terminal->screen().history_line( history_row );
	}

	GridPosition TerminalWidget::position_at( QPoint const& point ) const{
		GridPosition position;
		position.row    = top_visible_row() + static_cast<int>( std::floor( point.y() / _cell_height ) );
		position.column = static_cast<int>( std::floor( point.x() / _cell_width ) );
		position.column = std::clamp( position.column, 0, _columns - 1 );
		return position;
	}

	// ---------------------------------------------------------------------------
	// Painting
	// ---------------------------------------------------------------------------

	void TerminalWidget::paintEvent( QPaintEvent* event ){
		QPainter painter( viewport() );
		painter.setRenderHint( QPainter::TextAntialiasing, true );
		painter.fillRect( event->rect(), _scheme.background() );

		int const first_row = top_visible_row();

		for( int visual_row = 0; visual_row < _rows; ++visual_row ){
			int const y = static_cast<int>( std::floor( visual_row * _cell_height ) );
			if( y > event->rect().bottom() || y + _cell_height < event->rect().top() )
				continue;
			paint_row( painter, first_row + visual_row, y );
		}

		paint_cursor( painter );

		_painted_revision = _terminal->revision();
	}

	void TerminalWidget::paint_row( QPainter& painter, int history_row, int y ){
		Line const* line = line_at( history_row );
		if( line == nullptr )
			return;

		bool const reverse_screen = _terminal->modes().reverse_video;
		auto const columnCount    = std::min<int>( static_cast<int>( line->size() ), _columns );

		// Pass 1: backgrounds. Runs of the same colour are merged into one fill so
		// a full-width coloured line is a single rectangle rather than 200.
		int    run_start = 0;
		QColor run_color;
		bool   run_valid = false;

		auto const flush_run = [&]( int end_column ){
			if( !run_valid || end_column <= run_start )
				return;
			if( run_color != _scheme.background() ){
				QRectF const rect( run_start * _cell_width, y, ( end_column - run_start ) * _cell_width, _cell_height );
				painter.fillRect( rect, run_color );
			}
			run_start = end_column;
		};

		for( int column = 0; column < columnCount; ++column ){
			Cell const& cell     = ( *line )[static_cast<std::size_t>( column )];
			bool const  selected = is_selected( history_row, column );
			bool const  inverse  = has_flag( cell.attributes.flags, CellFlag::INVERSE ) != reverse_screen;

			QColor background;
			if( selected ){
				background = _scheme.selection();
			}
			else if( inverse ){
				background = _scheme.resolve( cell.attributes.foreground, true,
											  has_flag( cell.attributes.flags, CellFlag::BOLD ) );
			}
			else{
				background = _scheme.resolve( cell.attributes.background, false, false );
			}

			if( !run_valid ){
				run_color = background;
				run_valid = true;
				run_start = column;
			}
			else if( background != run_color ){
				flush_run( column );
				run_color = background;
			}
		}
		flush_run( columnCount );

		// Pass 2: glyphs.
		for( int column = 0; column < columnCount; ++column ){
			Cell const& cell = ( *line )[static_cast<std::size_t>( column )];

			if( has_flag( cell.attributes.flags, CellFlag::WIDE_TRAIL ) )
				continue;
			if( has_flag( cell.attributes.flags, CellFlag::HIDDEN ) )
				continue;
			if( cell.is_empty() )
				continue;

			bool const selected = is_selected( history_row, column );
			bool const bold     = has_flag( cell.attributes.flags, CellFlag::BOLD );
			bool const inverse  = has_flag( cell.attributes.flags, CellFlag::INVERSE ) != reverse_screen;

			QColor foreground;
			if( selected ){
				foreground = _scheme.selection_text();
			}
			else if( inverse ){
				foreground = _scheme.resolve( cell.attributes.background, false, false );
			}
			else{
				foreground = _scheme.resolve( cell.attributes.foreground, true, bold );
			}

			if( has_flag( cell.attributes.flags, CellFlag::FAINT ) )
				foreground.setAlphaF( 0.55f );

			bool const italic = has_flag( cell.attributes.flags, CellFlag::ITALIC );
			painter.setFont(bold ? (italic ? [this]{
            QFont font = _bold_font;
            font.setItalic(true);
            return font;
        }() : _bold_font)
                             : (italic ? _italic_font : _font));
			painter.setPen( foreground );

			if( !cell.is_blank() ){
				QString const glyph = QString::fromUcs4( &cell.character, 1 );
				qreal const   x     = column * _cell_width;
				painter.drawText( QPointF( x, y + _baseline ), glyph );
			}

			qreal const x     = column * _cell_width;
			qreal const width = has_flag( cell.attributes.flags, CellFlag::WIDE_LEAD ) ? _cell_width * 2 : _cell_width;

			if( has_flag( cell.attributes.flags, CellFlag::UNDERLINE ) ||
				has_flag( cell.attributes.flags, CellFlag::DOUBLE_UNDERLINE ) ){
				QColor underline_color = foreground;
				if( !cell.attributes.underline_color.is_default() )
					underline_color = _scheme.resolve( cell.attributes.underline_color, true, false );

				painter.setPen( underline_color );
				qreal const underline_y = y + _baseline + 2.0;
				painter.drawLine( QPointF( x, underline_y ), QPointF( x + width, underline_y ) );
				if( has_flag( cell.attributes.flags, CellFlag::DOUBLE_UNDERLINE ) )
					painter.drawLine( QPointF( x, underline_y + 2.0 ), QPointF( x + width, underline_y + 2.0 ) );
			}

			if( has_flag( cell.attributes.flags, CellFlag::STRIKEOUT ) ){
				painter.setPen( foreground );
				qreal const strike_y = y + _baseline - _cell_height * 0.25;
				painter.drawLine( QPointF( x, strike_y ), QPointF( x + width, strike_y ) );
			}
		}
	}

	void TerminalWidget::paint_cursor( QPainter& painter ){
		if( !_terminal->modes().cursor_visible )
			return;
		// The cursor is only meaningful where the live output is; scrolling back
		// into history should not paint a stray block.
		if( top_visible_row() != 0 )
			return;
		if( _cursor_blinking && !_cursor_on && hasFocus() )
			return;

		CursorState const& cursor = _terminal->screen().cursor();
		qreal const        x      = cursor.column * _cell_width;
		qreal const        y      = cursor.row * _cell_height;
		QRectF const       rect( x, y, _cell_width, _cell_height );

		if( !hasFocus() ){
			// An unfocused terminal shows a hollow cursor, matching Terminal.app.
			painter.setPen( _scheme.cursor() );
			painter.setBrush( Qt::NoBrush );
			painter.drawRect( rect.adjusted( 0.5, 0.5, -0.5, -0.5 ) );
			return;
		}

		painter.fillRect( rect, _scheme.cursor() );

		Line const* line = line_at( cursor.row );
		if( line == nullptr || cursor.column >= static_cast<int>( line->size() ) )
			return;

		Cell const& cell = ( *line )[static_cast<std::size_t>( cursor.column )];
		if( cell.is_blank() )
			return;

		painter.setPen( _scheme.cursor_text() );
		painter.setFont( has_flag( cell.attributes.flags, CellFlag::BOLD ) ? _bold_font : _font );
		painter.drawText( QPointF( x, y + _baseline ), QString::fromUcs4( &cell.character, 1 ) );
	}

	// ---------------------------------------------------------------------------
	// Input
	// ---------------------------------------------------------------------------

	void TerminalWidget::receive( QByteArray const& bytes ){
		_terminal->receive( bytes );

		// Any output snaps the view back to the bottom, which is what a user
		// expects after typing a command while scrolled up.
		if( !_terminal->modes().alternate_screen )
			scroll_to_bottom();
	}

	void TerminalWidget::keyPressEvent( QKeyEvent* event ){
		// Scrollback navigation is handled locally and never reaches the host.
		if( event->modifiers().testFlag( Qt::ShiftModifier ) ){
			switch( event->key() ){
				case Qt::Key_PageUp:
					verticalScrollBar()->triggerAction( QAbstractSlider::SliderPageStepSub );
					return;
				case Qt::Key_PageDown:
					verticalScrollBar()->triggerAction( QAbstractSlider::SliderPageStepAdd );
					return;
				case Qt::Key_Home:
					verticalScrollBar()->setValue( 0 );
					return;
				case Qt::Key_End:
					scroll_to_bottom();
					return;
				default:
					break;
			}
		}

		KeyEncoder::Options options;
		options.application_cursor_keys = _terminal->modes().application_cursor_keys;
		options.application_keypad      = _terminal->modes().application_keypad;
		options.new_line_mode           = _terminal->modes().new_line_mode;

		QByteArray const encoded = KeyEncoder::encode( *event, options );
		if( encoded.isEmpty() ){
			QAbstractScrollArea::keyPressEvent( event );
			return;
		}

		// Typing dismisses the selection and returns to the live output.
		if( _has_selection )
			clear_selection();
		scroll_to_bottom();

		// Restart the blink so the cursor is solid while the user is typing.
		_cursor_on = true;
		if( _cursor_blinking )
			_blink_timer->start();

		Q_EMIT data_entered( encoded );
		event->accept();
	}

	void TerminalWidget::inputMethodEvent( QInputMethodEvent* event ){
		// Dead keys and CJK input methods deliver their result here rather than as
		// a key event.
		if( !event->commitString().isEmpty() ){
			Q_EMIT data_entered( event->commitString().toUtf8() );
			scroll_to_bottom();
		}
		event->accept();
	}

	bool TerminalWidget::event( QEvent* event ){
		// Tab must reach the host instead of moving focus to the next widget.
		if( event->type() == QEvent::KeyPress ){
			auto* key_event = static_cast<QKeyEvent*>( event );
			if( key_event->key() == Qt::Key_Tab || key_event->key() == Qt::Key_Backtab ){
				keyPressEvent( key_event );
				return true;
			}
		}
		return QAbstractScrollArea::event( event );
	}

	void TerminalWidget::focusInEvent( QFocusEvent* event ){
		QAbstractScrollArea::focusInEvent( event );
		_cursor_on = true;
		if( _cursor_blinking )
			_blink_timer->start();
		if( _terminal->modes().focus_reporting )
			Q_EMIT data_entered( QByteArrayLiteral( "\033[I" ) );
		viewport()->update();
	}

	void TerminalWidget::focusOutEvent( QFocusEvent* event ){
		QAbstractScrollArea::focusOutEvent( event );
		_blink_timer->stop();
		if( _terminal->modes().focus_reporting )
			Q_EMIT data_entered( QByteArrayLiteral( "\033[O" ) );
		viewport()->update();
	}

	// ---------------------------------------------------------------------------
	// Mouse
	// ---------------------------------------------------------------------------

	bool TerminalWidget::send_mouse_event( QMouseEvent* event, bool release, bool motion ){
		MouseTracking const tracking = _terminal->modes().mouse_tracking;
		if( tracking == MouseTracking::OFF )
			return false;

		// Shift always bypasses application mouse tracking so the user can still
		// select text in vim or less.
		if( event->modifiers().testFlag( Qt::ShiftModifier ) )
			return false;

		if( motion && tracking != MouseTracking::BUTTON_EVENT && tracking != MouseTracking::ANY_EVENT )
			return false;
		if( release && tracking == MouseTracking::X10 )
			return false;

		GridPosition const position = position_at( event->position().toPoint() );
		int const          column   = position.column + 1;
		int const          row      = position.row - top_visible_row() + 1;
		if( row < 1 || row > _rows )
			return true; // Inside the widget but outside the grid: swallow it.

		int button = 3; // Release, in the legacy encoding.
		switch( event->button() == Qt::NoButton ? event->buttons() : event->button() ){
			case Qt::LeftButton:
				button = 0;
				break;
			case Qt::MiddleButton:
				button = 1;
				break;
			case Qt::RightButton:
				button = 2;
				break;
			default:
				button = motion ? 3 : button;
				break;
		}

		if( motion )
			button += 32;

		if( event->modifiers().testFlag( Qt::ShiftModifier ) )
			button += 4;
		if( event->modifiers().testFlag( Qt::AltModifier ) )
			button += 8;
		if( event->modifiers().testFlag( Qt::ControlModifier ) )
			button += 16;

		QByteArray sequence;
		switch( _terminal->modes().mouse_encoding ){
			case MouseEncoding::SGR:
				sequence = QByteArrayLiteral( "\033[<" ) + QByteArray::number( button ) + ';' +
						   QByteArray::number( column ) + ';' + QByteArray::number( row ) + ( release ? 'm' : 'M' );
				break;

			case MouseEncoding::URXVT:
				sequence = QByteArrayLiteral( "\033[" ) + QByteArray::number( button + 32 ) + ';' +
						   QByteArray::number( column ) + ';' + QByteArray::number( row ) + 'M';
				break;

			case MouseEncoding::UTF8:
			case MouseEncoding::DEFAULT:{
				if( release )
					button = 3 + ( motion ? 32 : 0 );
				// The legacy encoding cannot address past column 223.
				if( column > 223 || row > 223 )
					return true;
				sequence = QByteArrayLiteral( "\033[M" );
				sequence.append( static_cast<char>( 32 + button ) );
				sequence.append( static_cast<char>( 32 + column ) );
				sequence.append( static_cast<char>( 32 + row ) );
				break;
			}
		}

		Q_EMIT data_entered( sequence );
		return true;
	}

	bool TerminalWidget::send_wheel_event( QWheelEvent* event ){
		if( _terminal->modes().mouse_tracking == MouseTracking::OFF )
			return false;
		if( event->modifiers().testFlag( Qt::ShiftModifier ) )
			return false;

		int const steps = std::abs( event->angleDelta().y() ) / 120;
		if( steps == 0 )
			return false;

		bool const         up       = event->angleDelta().y() > 0;
		GridPosition const position = position_at( event->position().toPoint() );
		int const          column   = position.column + 1;
		int const          row      = position.row - top_visible_row() + 1;

		int const button = up ? 64 : 65;

		QByteArray sequence;
		for( int i = 0; i < steps; ++i ){
			if( _terminal->modes().mouse_encoding == MouseEncoding::SGR ){
				sequence += QByteArrayLiteral( "\033[<" ) + QByteArray::number( button ) + ';' +
							QByteArray::number( column ) + ';' + QByteArray::number( row ) + 'M';
			}
			else{
				if( column > 223 || row > 223 )
					break;
				sequence += QByteArrayLiteral( "\033[M" );
				sequence.append( static_cast<char>( 32 + button ) );
				sequence.append( static_cast<char>( 32 + column ) );
				sequence.append( static_cast<char>( 32 + row ) );
			}
		}

		if( sequence.isEmpty() )
			return false;

		Q_EMIT data_entered( sequence );
		return true;
	}

	void TerminalWidget::mousePressEvent( QMouseEvent* event ){
		setFocus( Qt::MouseFocusReason );

		if( send_mouse_event( event, /*release=*/false, /*motion=*/false ) ){
			event->accept();
			return;
		}

		if( event->button() == Qt::MiddleButton ){
			// X11-style middle-click paste; harmless on macOS where the selection
			// clipboard does not exist and the call simply returns nothing.
			QString const text = QGuiApplication::clipboard()->text( QClipboard::Selection );
			if( !text.isEmpty() )
				Q_EMIT data_entered( KeyEncoder::encode_paste( text, _terminal->modes().bracketed_paste ) );
			event->accept();
			return;
		}

		if( event->button() != Qt::LeftButton ){
			QAbstractScrollArea::mousePressEvent( event );
			return;
		}

		GridPosition const position = position_at( event->position().toPoint() );

		if( event->modifiers().testFlag( Qt::ShiftModifier ) && _has_selection ){
			extend_selection_to( position );
		}
		else{
			_selection_anchor = position;
			_selection_focus  = position;
			_selection_unit   = SelectionUnit::CHARACTER;
			_has_selection    = false;
		}

		_selecting = true;
		viewport()->update();
		event->accept();
	}

	void TerminalWidget::mouseMoveEvent( QMouseEvent* event ){
		if( !_selecting ){
			if( send_mouse_event( event, /*release=*/false, /*motion=*/true ) )
				event->accept();
			else
				QAbstractScrollArea::mouseMoveEvent( event );
			return;
		}

		// Dragging past the top or bottom edge scrolls the view.
		int const y = event->position().toPoint().y();
		if( y < 0 )
			verticalScrollBar()->triggerAction( QAbstractSlider::SliderSingleStepSub );
		else if( y > viewport()->height() )
			verticalScrollBar()->triggerAction( QAbstractSlider::SliderSingleStepAdd );

		extend_selection_to( position_at( event->position().toPoint() ) );
		viewport()->update();
		event->accept();
	}

	void TerminalWidget::mouseReleaseEvent( QMouseEvent* event ){
		if( !_selecting ){
			if( send_mouse_event( event, /*release=*/true, /*motion=*/false ) )
				event->accept();
			else
				QAbstractScrollArea::mouseReleaseEvent( event );
			return;
		}

		_selecting = false;

		// Mirror the selection into the X11 selection buffer where one exists.
		if( _has_selection && QGuiApplication::clipboard()->supportsSelection() )
			QGuiApplication::clipboard()->setText( selected_text(), QClipboard::Selection );

		event->accept();
	}

	void TerminalWidget::mouseDoubleClickEvent( QMouseEvent* event ){
		if( event->button() != Qt::LeftButton ){
			QAbstractScrollArea::mouseDoubleClickEvent( event );
			return;
		}

		GridPosition const position = position_at( event->position().toPoint() );

		// A third click within the double-click interval selects the whole line.
		if( _selection_unit == SelectionUnit::WORD && position.row == _selection_anchor.row )
			select_line_at( position );
		else
			select_word_at( position );

		_selecting = true;
		viewport()->update();
		event->accept();
	}

	void TerminalWidget::wheelEvent( QWheelEvent* event ){
		if( event->modifiers().testFlag( Qt::ControlModifier ) ){
			if( event->angleDelta().y() > 0 )
				increase_font_size();
			else if( event->angleDelta().y() < 0 )
				decrease_font_size();
			event->accept();
			return;
		}

		if( send_wheel_event( event ) ){
			event->accept();
			return;
		}

		QAbstractScrollArea::wheelEvent( event );
	}

	void TerminalWidget::contextMenuEvent( QContextMenuEvent* event ){
		QMenu menu( this );

		QAction* copy = menu.addAction( tr( "Copy" ), this, &TerminalWidget::copy_selection );
		copy->setShortcut( QKeySequence::Copy );
		copy->setEnabled( _has_selection );

		QAction* paste = menu.addAction( tr( "Paste" ), this, &TerminalWidget::paste_from_clipboard );
		paste->setShortcut( QKeySequence::Paste );
		paste->setEnabled( !QGuiApplication::clipboard()->text().isEmpty() );

		menu.addSeparator();
		menu.addAction( tr( "Select All" ), this, &TerminalWidget::select_all );
		menu.addAction( tr( "Clear Buffer" ), this, &TerminalWidget::clear_screen );

		menu.exec( event->globalPos() );
		event->accept();
	}

	// ---------------------------------------------------------------------------
	// Drag and drop
	// ---------------------------------------------------------------------------

	void TerminalWidget::dragEnterEvent( QDragEnterEvent* event ){
		if( event->mimeData()->hasUrls() || event->mimeData()->hasText() )
			event->acceptProposedAction();
	}

	void TerminalWidget::dropEvent( QDropEvent* event ){
		QMimeData const* mime = event->mimeData();

		if( mime->hasUrls() ){
			QStringList paths;
			for( QUrl const& url : mime->urls() ){
				if( url.isLocalFile() )
					paths << url.toLocalFile();
			}

			if( !paths.isEmpty() ){
				// The session decides what a dropped file means: upload it, or
				// insert its quoted path at the prompt.
				Q_EMIT files_dropped( paths );
				event->acceptProposedAction();
				return;
			}
		}

		if( mime->hasText() ){
			Q_EMIT data_entered( KeyEncoder::encode_paste( mime->text(), _terminal->modes().bracketed_paste ) );
			event->acceptProposedAction();
		}
	}

	// ---------------------------------------------------------------------------
	// Selection
	// ---------------------------------------------------------------------------

	bool TerminalWidget::has_selection() const{
		return _has_selection;
	}

	bool TerminalWidget::is_selected( int history_row, int column ) const{
		if( !_has_selection )
			return false;

		GridPosition start = _selection_anchor;
		GridPosition end   = _selection_focus;
		if( end < start )
			std::swap( start, end );

		if( history_row < start.row || history_row > end.row )
			return false;
		if( history_row == start.row && column < start.column )
			return false;
		if( history_row == end.row && column > end.column )
			return false;

		return true;
	}

	void TerminalWidget::extend_selection_to( GridPosition const& position ){
		_selection_focus = position;

		if( _selection_unit != SelectionUnit::CHARACTER ){
			// Keep the originally selected word or line inside the range.
			if( position < _selection_unit_start ){
				_selection_anchor = _selection_unit_end;
				_selection_focus  = position;
			}
			else{
				_selection_anchor = _selection_unit_start;
				_selection_focus  = position;
			}
		}

		_has_selection = _selection_anchor != _selection_focus || _selection_unit != SelectionUnit::CHARACTER;
	}

	void TerminalWidget::select_word_at( GridPosition const& position ){
		Line const* line = line_at( position.row );
		if( line == nullptr )
			return;

		auto const width = static_cast<int>( line->size() );
		if( position.column >= width )
			return;

		char32_t const clicked = ( *line )[static_cast<std::size_t>( position.column )].character;
		if( !is_word_character( clicked ) ){
			_selection_anchor     = position;
			_selection_focus      = position;
			_has_selection        = true;
			_selection_unit       = SelectionUnit::WORD;
			_selection_unit_start = position;
			_selection_unit_end   = position;
			return;
		}

		int start = position.column;
		while( start > 0 && is_word_character( ( *line )[static_cast<std::size_t>( start - 1 )].character ) )
			--start;

		int end = position.column;
		while( end + 1 < width && is_word_character( ( *line )[static_cast<std::size_t>( end + 1 )].character ) )
			++end;

		_selection_anchor     = GridPosition{ position.row, start };
		_selection_focus      = GridPosition{ position.row, end };
		_selection_unit_start = _selection_anchor;
		_selection_unit_end   = _selection_focus;
		_selection_unit       = SelectionUnit::WORD;
		_has_selection        = true;
	}

	void TerminalWidget::select_line_at( GridPosition const& position ){
		_selection_anchor     = GridPosition{ position.row, 0 };
		_selection_focus      = GridPosition{ position.row, _columns - 1 };
		_selection_unit_start = _selection_anchor;
		_selection_unit_end   = _selection_focus;
		_selection_unit       = SelectionUnit::WHOLE_LINE;
		_has_selection        = true;
	}

	void TerminalWidget::select_all(){
		int const history = _terminal->screen().scrollback_size();
		_selection_anchor = GridPosition{ -history, 0 };
		_selection_focus  = GridPosition{ _rows - 1, _columns - 1 };
		_selection_unit   = SelectionUnit::CHARACTER;
		_has_selection    = true;
		viewport()->update();
	}

	void TerminalWidget::clear_selection(){
		if( !_has_selection )
			return;
		_has_selection  = false;
		_selection_unit = SelectionUnit::CHARACTER;
		viewport()->update();
	}

	QString TerminalWidget::selected_text() const{
		if( !_has_selection )
			return {};

		GridPosition start = _selection_anchor;
		GridPosition end   = _selection_focus;
		if( end < start )
			std::swap( start, end );

		QString text;

		for( int row = start.row; row <= end.row; ++row ){
			Line const* line = line_at( row );
			if( line == nullptr )
				continue;

			int const width = static_cast<int>( line->size() );
			int const from  = ( row == start.row ) ? start.column : 0;
			int const to    = ( row == end.row ) ? std::min( end.column, width - 1 ) : width - 1;

			QString row_text;
			for( int column = from; column <= to; ++column ){
				Cell const& cell = ( *line )[static_cast<std::size_t>( column )];
				if( has_flag( cell.attributes.flags, CellFlag::WIDE_TRAIL ) )
					continue;
				row_text += QString::fromUcs4( &cell.character, 1 );
			}

			// Trailing blanks are padding, not content.
			while( row_text.endsWith( QLatin1Char( ' ' ) ) )
				row_text.chop( 1 );

			text += row_text;

			// A line that ended by wrapping is one logical line, so it must not
			// gain a newline when copied.
			bool const wrapped = row >= 0 && _terminal->screen().is_line_wrapped( row );
			if( row != end.row && !wrapped )
				text += QLatin1Char( '\n' );
		}

		return text;
	}

	void TerminalWidget::copy_selection(){
		QString const text = selected_text();
		if( !text.isEmpty() )
			QGuiApplication::clipboard()->setText( text );
	}

	void TerminalWidget::paste_from_clipboard(){
		QString const text = QGuiApplication::clipboard()->text();
		if( text.isEmpty() )
			return;

		scroll_to_bottom();
		Q_EMIT data_entered( KeyEncoder::encode_paste( text, _terminal->modes().bracketed_paste ) );
	}

	void TerminalWidget::clear_screen(){
		_terminal->screen().erase_in_display( Screen::EraseMode::ALL, _terminal->current_attributes() );
		_terminal->screen().clear_scrollback();
		_terminal->screen().move_cursor( 0, 0 );
		clear_selection();
		update_scroll_bar();
		viewport()->update();
	}

} // namespace arterm::term
