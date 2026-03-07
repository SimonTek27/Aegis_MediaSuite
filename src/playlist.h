// playlist.h — Minimal Playlist class for Aegis MediaSuite
#pragma once

#include <QObject>
#include <QUrl>
#include <QString>
#include <QList>
#include <memory>

namespace Aegis {

struct PlaylistEntry {
    QUrl    url;
    QString title;
    QString artist;
    QString album;
    qint64  duration = 0; // milliseconds
};

class Playlist : public QObject {
    Q_OBJECT
public:
    explicit Playlist(QObject* parent = nullptr) : QObject(parent) {}
    ~Playlist() override = default;

    void append(const PlaylistEntry& item) {
        m_items.append(item);
        emit changed();
    }
    void clear() {
        m_items.clear();
        emit changed();
    }
    int  count() const { return m_items.count(); }
    bool isEmpty() const { return m_items.isEmpty(); }

    const PlaylistEntry& at(int i) const { return m_items.at(i); }
    PlaylistEntry& operator[](int i)    { return m_items[i]; }
    const QList<PlaylistEntry>& items() const { return m_items; }

    int  currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int idx) {
        if (idx != m_currentIndex) {
            m_currentIndex = idx;
            emit currentIndexChanged(idx);
        }
    }

    QUrl currentUrl() const {
        if (m_currentIndex >= 0 && m_currentIndex < m_items.count())
            return m_items.at(m_currentIndex).url;
        return {};
    }

signals:
    void changed();
    void currentIndexChanged(int index);

private:
    QList<PlaylistEntry> m_items;
    int m_currentIndex = -1;
};

} // namespace Aegis
