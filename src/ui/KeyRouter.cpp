#include "KeyRouter.h"

#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

// A text field takes its own keys, and so does anything open over the
// window, such as an event's card or a menu.
bool keysTaken(const QQuickWindow *window)
{
    const QQuickItem *item = window ? window->activeFocusItem() : nullptr;
    if (item && (item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit")))
        return true;
    for (; item; item = item->parentItem()) {
        if (item->inherits("QQuickPopupItem"))
            return true;
    }
    return false;
}

KeyAction act(const QString &id, const QString &label, const QString &group,
              const QVariantList &standard, const QString &vi, const QString &leader,
              bool inCard = false)
{
    return {id, label, group, standard, vi, leader, inCard};
}

} // namespace

KeyRouter::KeyRouter(QObject *parent) : QObject(parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &KeyRouter::cancel);
    connect(this, &KeyRouter::settingsChanged, this, [this] {
        if (!m_viMode)
            cancel();
    });
}

QList<KeyAction> KeyRouter::actions()
{
    using K = QKeySequence;
    return {
        act(u"newEvent"_s, tr("New event"), u"events"_s, {int(K::New)}, u"n"_s, {}),
        act(u"search"_s, tr("Search events"), u"general"_s, {int(K::Find)}, u"/"_s, {}),
        act(u"undo"_s, tr("Undo the last change"), u"events"_s, {int(K::Undo)}, u"u"_s, {}),
        act(u"copy"_s, tr("Copy the open event"), u"events"_s, {int(K::Copy)}, {}, {}, true),
        act(u"duplicate"_s, tr("Duplicate the open event"), u"events"_s, {u"Ctrl+D"_s}, {}, {},
            true),
        act(u"paste"_s, tr("Paste under the pointer"), u"events"_s, {int(K::Paste)}, u"p"_s, {}),
        act(u"previous"_s, tr("Previous day, week or month"), u"views"_s, {u"Alt+Left"_s}, u"h"_s,
            {}),
        act(u"next"_s, tr("Next day, week or month"), u"views"_s, {u"Alt+Right"_s}, u"l"_s, {}),
        act(u"today"_s, tr("Go to today"), u"views"_s, {u"Alt+Home"_s}, u"t"_s, {}),
        act(u"scrollDown"_s, tr("Scroll down the day"), u"views"_s, {}, u"j"_s, {}),
        act(u"scrollUp"_s, tr("Scroll up the day"), u"views"_s, {}, u"k"_s, {}),
        act(u"top"_s, tr("Go to the start of the day"), u"views"_s, {}, u"gg"_s, {}),
        act(u"bottom"_s, tr("Go to the end of the day"), u"views"_s, {}, u"G"_s, {}),
        act(u"dayView"_s, tr("Day view"), u"views"_s, {u"Ctrl+1"_s}, {}, u"d"_s),
        act(u"weekView"_s, tr("Week view"), u"views"_s, {u"Ctrl+2"_s}, {}, u"w"_s),
        act(u"monthView"_s, tr("Month view"), u"views"_s, {u"Ctrl+3"_s}, {}, u"m"_s),
        act(u"agendaView"_s, tr("Agenda view"), u"views"_s, {u"Ctrl+4"_s}, {}, u"a"_s),
        act(u"invites"_s, tr("Show invitations"), u"general"_s, {}, {}, u"i"_s),
        act(u"refresh"_s, tr("Sync now"), u"general"_s, {int(K::Refresh)}, {}, u"r"_s),
        act(u"settings"_s, tr("Settings"), u"general"_s, {int(K::Preferences), u"Ctrl+,"_s}, {},
            u"s"_s),
        act(u"help"_s, tr("Keyboard shortcuts"), u"general"_s, {int(K::HelpContents)}, u"?"_s, {}),
        act(u"quit"_s, tr("Quit Callie"), u"general"_s, {int(K::Quit)}, {}, {}),
    };
}

QVariantList KeyRouter::actionList()
{
    QVariantList list;
    for (const KeyAction &action : actions()) {
        QStringList keys;
        for (const QVariant &key : action.standard) {
            const QKeySequence sequence = key.typeId() == QMetaType::Int
                                              ? QKeySequence(QKeySequence::StandardKey(key.toInt()))
                                              : QKeySequence(key.toString());
            if (!sequence.isEmpty())
                keys << sequence.toString(QKeySequence::NativeText);
        }
        list << QVariantMap{{u"id"_s, action.id},
                            {u"label"_s, action.label},
                            {u"group"_s, action.group},
                            {u"standard"_s, action.standard},
                            {u"keys"_s, keys},
                            {u"vi"_s, action.vi},
                            {u"leader"_s, action.leader},
                            {u"inCard"_s, action.inCard}};
    }
    return list;
}

void KeyRouter::setWindow(QQuickWindow *window)
{
    if (m_window == window)
        return;
    if (m_window)
        m_window->removeEventFilter(this);
    m_window = window;
    if (m_window)
        m_window->installEventFilter(this);
    Q_EMIT windowChanged();
}

bool KeyRouter::press(const QString &text)
{
    if (!m_viMode || text.isEmpty())
        return false;
    const QList<KeyAction> all = actions();
    const auto find = [&all](auto matches) -> QString {
        for (const KeyAction &action : all) {
            if (matches(action))
                return action.id;
        }
        return {};
    };

    if (m_pending == u"leader") {
        const QString id = find([&text](const KeyAction &a) { return a.leader == text; });
        cancel();
        if (!id.isEmpty())
            Q_EMIT triggered(id);
        // Whatever followed the leader was meant for it.
        return true;
    }
    if (m_pending == u"g") {
        cancel();
        const QString id = find([&text](const KeyAction &a) { return a.vi == u"g"_s + text; });
        if (!id.isEmpty()) {
            Q_EMIT triggered(id);
            return true;
        }
    }

    if (text == m_leaderKey) {
        setPending(u"leader"_s);
        if (m_leaderTimeout > 0)
            m_timeout.start(m_leaderTimeout);
        return true;
    }
    if (text == u"g") {
        setPending(u"g"_s);
        m_timeout.start(std::max(m_leaderTimeout, 1000));
        return true;
    }
    const QString id = find([&text](const KeyAction &a) { return a.vi == text; });
    if (id.isEmpty())
        return false;
    Q_EMIT triggered(id);
    return true;
}

void KeyRouter::cancel()
{
    m_timeout.stop();
    setPending({});
}

void KeyRouter::setPending(const QString &pending)
{
    if (m_pending == pending)
        return;
    m_pending = pending;
    Q_EMIT pendingChanged();
}

bool KeyRouter::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::KeyPress || !m_viMode || keysTaken(m_window))
        return QObject::eventFilter(watched, event);
    const auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_Escape && !m_pending.isEmpty()) {
        cancel();
        return true;
    }
    // Shortcuts with Ctrl or Alt are QML's, and Space presses a focused button
    // unless a command is under way.
    if (key->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier))
        return QObject::eventFilter(watched, event);
    const QQuickItem *focus = m_window ? m_window->activeFocusItem() : nullptr;
    if (key->key() == Qt::Key_Space && m_pending.isEmpty() && focus &&
        focus->inherits("QQuickAbstractButton"))
        return QObject::eventFilter(watched, event);
    return press(key->text()) || QObject::eventFilter(watched, event);
}

} // namespace callie
