#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

/**
 * @brief The quick menu's pages, selection and text.
 *
 * Plain logic with no SDL or stream dependencies, so it can be tested on its
 * own. QuickMenu drives it from the stream's event loop and draws text() with
 * the overlay system.
 *
 * The text is drawn in the overlay font, which only has printable ASCII, and
 * has to fit the overlay's text buffer, so names are sanitized and elided and
 * long command lists scroll in a window around the selection.
 */
class QuickMenuModel
{
public:
    enum class Page {
        Main,
        ServerCommands,
    };

    enum class Action {
        None, // Nothing to run: the menu moved to another page
        Unavailable, // The item can't be used; Activation::message says why
        Disconnect,
        Quit,
        UploadClipboard,
        FetchClipboard,
        ToggleStats,
        RunServerCommand, // Activation::commandIndex is the host's index
    };

    struct Activation {
        Action action = Action::None;
        int commandIndex = -1;
        QString message;
    };

    // What the host and the stream allow, gathered when the menu opens. An
    // empty reason means the item can be used.
    struct HostState {
        QStringList serverCommands; // In the host's order
        QString serverCommandsUnavailable; // Why there are none to run
        QString uploadUnavailable;
        QString fetchUnavailable;
        bool statsVisible = false;
        bool disconnectQuitsApp = false; // "Quit app on host PC after ending stream"
    };

    // Rows of the server command list shown at once
    static constexpr int kVisibleCommandRows = 8;
    // Widest line of menu text, in characters
    static constexpr int kMaxColumns = 44;
    // Lines of the detail area (status, or what the selection does)
    static constexpr int kDetailLines = 2;
    // Lines of a status shown while the menu is closed
    static constexpr int kToastLines = 3;
    // Characters of a host-supplied name that are looked at; more can't show
    static constexpr int kMaxNameLength = 256;

    void open(const HostState& state);
    void close();
    bool isOpen() const { return m_Open; }

    Page page() const { return m_Page; }
    int selection() const { return m_Selection; }
    int rowCount() const;

    // The server command rows in view: [firstVisibleRow(), firstVisibleRow() + visibleRowCount())
    int firstVisibleRow() const { return m_FirstVisible; }
    int visibleRowCount() const;

    // Moves the selection by delta rows. A single step past either end wraps
    // around if wrap is set (a fresh key press) and stops there otherwise (key
    // repeat, paging). Larger steps stop at the ends.
    void moveSelection(int delta, bool wrap);
    void moveToFirst();
    void moveToLast();

    Activation activate();

    // Goes back to the main page. Returns false if the menu was already on
    // it, in which case it closed.
    bool back();

    // The menu as overlay text. The detail area shows status if it isn't
    // empty, otherwise what the selected item does or why it can't be used.
    // The selected row starts with '>', which the overlay highlights.
    QString text(const QString& status) const;

    // A status on its own, for while the menu is closed
    static QString toastText(const QString& status);

    // Helpers, public for tests
    static QString sanitize(const QString& text); // Printable ASCII only
    static QString elide(const QString& text, int columns);
    // Word-wraps text to at most maxLines lines (all of them if maxLines is
    // 0), ending the last with "..." if it's cut short
    static QStringList wrap(const QString& text, int columns, int maxLines);

private:
    struct Item {
        Action action;
        QString label;
        QString description;
        QString unavailable; // Empty if the item can be used
    };

    void buildMainItems();
    void scrollToSelection();
    QString rowLabel(int row) const;
    QString detail() const;

    bool m_Open = false;
    Page m_Page = Page::Main;
    int m_Selection = 0;
    int m_FirstVisible = 0;
    HostState m_Host;
    QVector<Item> m_MainItems;
};
