#include "quickmenumodel.h"

#include <QtGlobal>

namespace {

// Row of the "Server Commands" entry on the main page
constexpr int kServerCommandsRow = 2;

const char* const kSelectedMarker = "> ";
const char* const kRowIndent = "  ";

}

void QuickMenuModel::open(const HostState& state)
{
    m_Host = state;
    if (m_Host.serverCommands.isEmpty() && m_Host.serverCommandsUnavailable.isEmpty()) {
        m_Host.serverCommandsUnavailable = QStringLiteral("No server commands on this host");
    }

    buildMainItems();

    m_Open = true;
    m_Page = Page::Main;
    m_Selection = 0;
    m_FirstVisible = 0;
}

void QuickMenuModel::close()
{
    m_Open = false;
    m_Page = Page::Main;
    m_Selection = 0;
    m_FirstVisible = 0;
}

void QuickMenuModel::buildMainItems()
{
    const int commandCount = m_Host.serverCommands.size();

    m_MainItems.clear();
    m_MainItems.append({ Action::Disconnect,
                         QStringLiteral("Disconnect"),
                         // DeferredSessionCleanupTask quits the app if set
                         m_Host.disconnectQuitsApp
                             ? QStringLiteral("End the stream and quit the app on the host (your settings)")
                             : QStringLiteral("End the stream and leave the app running on the host"),
                         QString() });
    m_MainItems.append({ Action::Quit,
                         QStringLiteral("Quit"),
                         QStringLiteral("Quit the app on the host and close Artemis"),
                         QString() });
    m_MainItems.append({ Action::None, // Opens the server command page
                         commandCount > 0 ? QStringLiteral("Server Commands (%1)").arg(commandCount)
                                          : QStringLiteral("Server Commands"),
                         commandCount == 1 ? QStringLiteral("Run the host's server command")
                                           : QStringLiteral("Run one of the host's %1 server commands").arg(commandCount),
                         commandCount > 0 ? QString() : m_Host.serverCommandsUnavailable });
    m_MainItems.append({ Action::UploadClipboard,
                         QStringLiteral("Upload Clipboard"),
                         QStringLiteral("Send this computer's clipboard text to the host"),
                         m_Host.uploadUnavailable });
    m_MainItems.append({ Action::FetchClipboard,
                         QStringLiteral("Fetch Clipboard"),
                         QStringLiteral("Copy the host's clipboard text to this computer"),
                         m_Host.fetchUnavailable });
    m_MainItems.append({ Action::ToggleStats,
                         QStringLiteral("Toggle Performance Stats"),
                         m_Host.statsVisible ? QStringLiteral("Hide the performance stats")
                                             : QStringLiteral("Show the performance stats"),
                         QString() });

    Q_ASSERT(m_MainItems.at(kServerCommandsRow).action == Action::None);
}

int QuickMenuModel::rowCount() const
{
    if (!m_Open) {
        return 0;
    }
    return m_Page == Page::Main ? m_MainItems.size() : m_Host.serverCommands.size();
}

int QuickMenuModel::visibleRowCount() const
{
    if (m_Page == Page::Main) {
        return rowCount();
    }
    return qMin(rowCount(), kVisibleCommandRows);
}

void QuickMenuModel::moveSelection(int delta, bool wrap)
{
    const int count = rowCount();
    if (count == 0 || delta == 0) {
        return;
    }

    int target = m_Selection + delta;
    if (target < 0) {
        target = (wrap && delta == -1 && m_Selection == 0) ? count - 1 : 0;
    }
    else if (target >= count) {
        target = (wrap && delta == 1 && m_Selection == count - 1) ? 0 : count - 1;
    }

    m_Selection = target;
    scrollToSelection();
}

void QuickMenuModel::moveToFirst()
{
    if (rowCount() > 0) {
        m_Selection = 0;
        scrollToSelection();
    }
}

void QuickMenuModel::moveToLast()
{
    if (rowCount() > 0) {
        m_Selection = rowCount() - 1;
        scrollToSelection();
    }
}

void QuickMenuModel::scrollToSelection()
{
    if (m_Page == Page::Main) {
        m_FirstVisible = 0;
        return;
    }

    // Scroll only as far as needed to keep the selection in view
    const int visible = visibleRowCount();
    if (m_Selection < m_FirstVisible) {
        m_FirstVisible = m_Selection;
    }
    else if (m_Selection >= m_FirstVisible + visible) {
        m_FirstVisible = m_Selection - visible + 1;
    }
    m_FirstVisible = qBound(0, m_FirstVisible, qMax(0, rowCount() - visible));
}

QuickMenuModel::Activation QuickMenuModel::activate()
{
    Activation activation;
    if (!m_Open || rowCount() == 0) {
        return activation;
    }

    if (m_Page == Page::ServerCommands) {
        activation.action = Action::RunServerCommand;
        activation.commandIndex = m_Selection;
        return activation;
    }

    const Item& item = m_MainItems.at(m_Selection);
    if (!item.unavailable.isEmpty()) {
        activation.action = Action::Unavailable;
        activation.message = item.unavailable;
        return activation;
    }

    if (m_Selection == kServerCommandsRow) {
        m_Page = Page::ServerCommands;
        m_Selection = 0;
        m_FirstVisible = 0;
        return activation;
    }

    activation.action = item.action;
    return activation;
}

bool QuickMenuModel::back()
{
    if (!m_Open) {
        return false;
    }

    if (m_Page == Page::ServerCommands) {
        m_Page = Page::Main;
        m_Selection = kServerCommandsRow;
        m_FirstVisible = 0;
        return true;
    }

    close();
    return false;
}

QString QuickMenuModel::rowLabel(int row) const
{
    QString label;
    if (m_Page == Page::Main) {
        const Item& item = m_MainItems.at(row);
        label = item.label;
        if (!item.unavailable.isEmpty()) {
            label += QStringLiteral(" (unavailable)");
        }
    }
    else {
        label = sanitize(m_Host.serverCommands.at(row).left(kMaxNameLength));
    }

    return elide(label, kMaxColumns - 2);
}

QString QuickMenuModel::detail() const
{
    if (m_Page == Page::Main) {
        const Item& item = m_MainItems.at(m_Selection);
        return item.unavailable.isEmpty() ? item.description : item.unavailable;
    }

    // The full name if the row had to cut it short. Past the part that's
    // looked at, mark it cut short before wrapping, so a name whose first
    // part fits on a line (say, after padding) doesn't look complete.
    const QString& command = m_Host.serverCommands.at(m_Selection);
    QString name = sanitize(command.left(kMaxNameLength));
    if (command.size() > kMaxNameLength) {
        name += QStringLiteral("...");
    }
    if (name.size() > kMaxColumns - 2) {
        return name;
    }
    return QStringLiteral("Run this command on the host");
}

QString QuickMenuModel::text(const QString& status) const
{
    if (!m_Open) {
        return QString();
    }

    QString title;
    QString hint;
    QStringList rows;

    auto rowLine = [this](int row) {
        return QString::fromLatin1(row == m_Selection ? kSelectedMarker : kRowIndent) + rowLabel(row);
    };

    if (m_Page == Page::Main) {
        title = QStringLiteral("Quick Menu");
        hint = QStringLiteral("Up/Down Move  Enter/A Select  Esc/B Close");
        for (int row = 0; row < m_MainItems.size(); row++) {
            rows.append(rowLine(row));
        }
    }
    else {
        const int count = m_Host.serverCommands.size();
        const int visible = visibleRowCount();
        const bool scrolls = count > visible;

        title = QStringLiteral("Server Commands (%1)").arg(count);
        hint = QStringLiteral("Up/Down Move  Enter/A Run  Esc/B Back");

        // The marker lines stay when there's nothing past an end, so the list
        // doesn't shift as it scrolls
        if (scrolls) {
            rows.append(m_FirstVisible > 0 ? QStringLiteral("  ^ %1 more").arg(m_FirstVisible) : QString());
        }
        for (int row = m_FirstVisible; row < m_FirstVisible + visible; row++) {
            rows.append(rowLine(row));
        }
        if (scrolls) {
            const int below = count - (m_FirstVisible + visible);
            rows.append(below > 0 ? QStringLiteral("  v %1 more").arg(below) : QString());
        }
    }

    int width = int(qMax(title.size(), hint.size()));
    for (const QString& row : std::as_const(rows)) {
        width = qMax(width, int(row.size()));
    }
    width = qMin(width, kMaxColumns);

    QStringList detailLines = wrap(status.isEmpty() ? detail() : sanitize(status), width, kDetailLines);
    while (detailLines.size() < kDetailLines) {
        detailLines.append(QString());
    }

    QStringList lines;
    lines.append(QString(qMax(0, int(width - title.size()) / 2), QLatin1Char(' ')) + title);
    lines.append(QString());
    lines.append(rows);
    lines.append(QString());
    lines.append(detailLines);
    lines.append(QString());
    lines.append(hint);
    return lines.join(QLatin1Char('\n'));
}

QString QuickMenuModel::toastText(const QString& status)
{
    return wrap(sanitize(status), kMaxColumns, kToastLines).join(QLatin1Char('\n'));
}

QString QuickMenuModel::sanitize(const QString& text)
{
    QString result;
    result.reserve(text.size());
    for (const QChar ch : text) {
        const char16_t c = ch.unicode();
        if (c == u'\t' || c == u'\n' || c == u'\r') {
            result.append(QLatin1Char(' '));
        }
        // ModeSeven, the overlay font, has glyphs for 0x20-0x7D. This also
        // keeps every character one byte in the overlay's UTF-8 text buffer.
        else if (c >= 0x20 && c <= 0x7D) {
            result.append(ch);
        }
        else if (ch.isLowSurrogate()) {
            // The high surrogate already became a '?'
        }
        else {
            result.append(QLatin1Char('?'));
        }
    }
    return result;
}

QString QuickMenuModel::elide(const QString& text, int columns)
{
    if (text.size() <= columns) {
        return text;
    }
    if (columns <= 3) {
        return text.left(columns);
    }
    return text.left(columns - 3) + QStringLiteral("...");
}

QStringList QuickMenuModel::wrap(const QString& text, int columns, int maxLines)
{
    QStringList lines;
    QString current;
    if (columns <= 0) {
        return lines;
    }

    // Only what can show is wrapped, plus a character to tell whether the
    // last line needs a "...", so a huge unbroken name isn't copied for every
    // line of it
    const QString visibleText = maxLines > 0
        ? text.simplified().left(qsizetype(maxLines) * (columns + 1) + 1)
        : text;
    const QStringList words = visibleText.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (QString word : words) {
        // Break words that can't fit on a line of their own
        while (word.size() > columns) {
            if (!current.isEmpty()) {
                lines.append(current);
                current.clear();
            }
            lines.append(word.left(columns));
            word = word.mid(columns);
        }

        if (current.isEmpty()) {
            current = word;
        }
        else if (current.size() + 1 + word.size() <= columns) {
            current += QLatin1Char(' ') + word;
        }
        else {
            lines.append(current);
            current = word;
        }
    }
    if (!current.isEmpty()) {
        lines.append(current);
    }

    if (maxLines > 0 && lines.size() > maxLines) {
        lines = lines.mid(0, maxLines);
        QString& last = lines.last();
        last = last.size() + 3 <= columns ? last + QStringLiteral("...")
                                          : last.left(qMax(0, columns - 3)) + QStringLiteral("...");
    }

    return lines;
}
