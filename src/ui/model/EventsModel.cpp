/*
 * Copyright 2010-2013 Bluecherry
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "EventsModel.h"
#include "server/DVRServer.h"
#include "server/DVRServerConfiguration.h"
#include "server/DVRServerRepository.h"
#include "camera/DVRCamera.h"
#include "core/EventData.h"
#include "core/ServerRequestManager.h"
#include "event/EventsLoader.h"
#include <QTextDocument>
#include <QColor>
#include <QDebug>
#include <QIcon>
#include <QtConcurrentRun>

EventsModel::EventsModel(DVRServerRepository *serverRepository, QObject *parent)
    : QAbstractItemModel(parent), m_serverRepository(serverRepository), serverEventsLimit(-1), incompleteEventsFirst(false)
{
    Q_ASSERT(m_serverRepository);

    connect(m_serverRepository, SIGNAL(serverAdded(DVRServer*)), SLOT(serverAdded(DVRServer*)));
    connect(m_serverRepository, SIGNAL(serverRemoved(DVRServer*)), SLOT(clearServerEvents(DVRServer*)));
    connect(&updateTimer, SIGNAL(timeout()), SLOT(updateServers()));

    //createTestData();

    sortColumn = DateColumn;
    sortOrder = Qt::DescendingOrder;
    applyFilters();

    foreach (DVRServer *s, m_serverRepository->servers())
        serverAdded(s);
}

void EventsModel::serverAdded(DVRServer *server)
{
    connect(server, SIGNAL(loginSuccessful()), SLOT(updateServer()));
    connect(server, SIGNAL(disconnected()), SLOT(clearServerEvents()));
    updateServer(server);
}

#if 0
/* Randomized events for testing until real ones are available */
void EventsModel::createTestData()
{
    unsigned seed = QDateTime::currentDateTime().time().msec() * QDateTime::currentDateTime().time().second();
    qsrand(seed);
    qDebug("seed: %u", seed);

    QList<DVRServer*> servers = bcApp->servers();
    if (servers.isEmpty())
        return;

    QDateTime end = QDateTime::currentDateTime().addSecs(-3600);
    int duration = 86400 * 7; /* one week */

    int count = (qrand() % 285) + 15;
    for (int i = 0; i < count; ++i)
    {
        EventData *event = new EventData(servers[qrand() % servers.size()]);
        event->date = end.addSecs(-((qrand() * qrand()) % duration));
        event->duration = 1 + (qrand() % 1299);

        bool useCamera = qrand() % 6;
        if (useCamera && !event->server->cameras().isEmpty())
        {
            event->setLocation(QString::fromLatin1("camera-%1").arg(qrand() % event->server->cameras().size()));
            event->type = (qrand() % 10) ? QLatin1String("motion") : QLatin1String("video signal loss");
            event->level = (qrand() % 3) ? EventLevel::Warning : EventLevel::Alarm;
            if (event->type == EventType::CameraVideoLost)
                event->level = EventLevel::Critical;
        }
        else
        {
            event->setLocation(QString::fromLatin1("system"));
            event->type = (qrand() % 10) ? QLatin1String("disk-space") : QLatin1String("crash");
            event->level = (qrand() % 5) ? EventLevel::Info : EventLevel::Critical;
        }

        cachedEvents[event->server].append(event);
    }
}
#endif

int EventsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return items.size();
}

int EventsModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return LastColumn+1;
}

QModelIndex EventsModel::index(int row, int column, const QModelIndex &parent) const
{
    if (parent.isValid() || row < 0 || column < 0 || row >= items.size() || column >= columnCount())
        return QModelIndex();

    return createIndex(row, column, items[row]);
}

QModelIndex EventsModel::parent(const QModelIndex &child) const
{
    Q_UNUSED(child);
    return QModelIndex();
}

QVariant EventsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    EventData *data = reinterpret_cast<EventData*>(index.internalPointer());
    if (!data)
        return QVariant();

    if (role == EventDataPtr)
    {
        return QVariant::fromValue(data);
    }
    else if (role == Qt::ToolTipRole)
    {
        return tr("%1 (%2)<br>%3 on %4<br>%5").arg(data->uiType(), data->uiLevel(), Qt::escape(data->uiLocation()),
                                                   Qt::escape(data->uiServer()), data->serverStartDate().toString());
    }
    else if (role == Qt::ForegroundRole)
    {
        return data->uiColor(false);
    }

    switch (index.column())
    {
    case ServerColumn:
        if (role == Qt::DisplayRole)
            if (data->server())
                return data->server()->configuration().displayName();
            else
                return QString();
        break;
    case LocationColumn:
        if (role == Qt::DisplayRole)
            return data->uiLocation();
        break;
    case TypeColumn:
        if (role == Qt::DisplayRole)
            return data->uiType();
        else if (role == Qt::DecorationRole)
            return data->hasMedia() ? QIcon(QLatin1String(":/icons/control-000-small.png")) : QVariant();
        break;
    case DurationColumn:
        if (role == Qt::DisplayRole)
            return data->uiDuration();
        else if (role == Qt::EditRole)
            return data->durationInSeconds();
        else if (role == Qt::FontRole && data->inProgress())
        {
            QFont f;
            f.setBold(true);
            return f;
        }
        break;
    case LevelColumn:
        if (role == Qt::DisplayRole)
            return data->uiLevel();
        else if (role == Qt::EditRole)
            return data->level().level;
        break;
    case DateColumn:
        if (role == Qt::DisplayRole)
            return data->serverStartDate().toString();
        else if (role == Qt::EditRole)
            return data->utcStartDate();
        break;
    }

    return QVariant();
}

QVariant EventsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();

    switch (section)
    {
    case ServerColumn: return tr("Server");
    case LocationColumn: return tr("Device");
    case TypeColumn: return tr("Event");
    case DurationColumn: return tr("Duration");
    case LevelColumn: return tr("Priority");
    case DateColumn: return tr("Date");
    }

    return QVariant();
}

void EventsModel::clearServerEvents(DVRServer *server)
{
    if (!server && !(server = qobject_cast<DVRServer*>(sender())))
    {
        ServerRequestManager *srm = qobject_cast<ServerRequestManager*>(sender());
        if (srm)
            server = srm->server;
        else
        {
            Q_ASSERT_X(false, "clearServerEvents", "No server and no appropriate sender");
            return;
        }
    }

    /* Group contiguous removed rows together; provides a significant boost in performance */
    int removeFirst = -1;
    for (int i = 0; ; ++i)
    {
        if (i < items.size() && items[i]->server() == server)
        {
            if (removeFirst < 0)
                removeFirst = i;
        }
        else if (removeFirst >= 0)
        {
            beginRemoveRows(QModelIndex(), removeFirst, i-1);
            items.erase(items.begin()+removeFirst, items.begin()+i);
            i = removeFirst;
            removeFirst = -1;
            endRemoveRows();
        }

        if (i == items.size())
            break;
    }

    cachedEvents.remove(server);
}

class EventSort
{
public:
    const int column;
    const bool lessThan;
    const bool incompleteFirst;

    EventSort(int c, bool l, bool incomplete = false)
        : column(c), lessThan(l), incompleteFirst(incomplete)
    {
    }

    bool operator()(const EventData *e1, const EventData *e2)
    {
        bool re;

        if (incompleteFirst)
        {
            if (e1->inProgress() && !e2->inProgress())
                return true;
            else if (e2->inProgress() && !e1->inProgress())
                return false;
        }

        switch (column)
        {
        case EventsModel::ServerColumn:
            re = QString::localeAwareCompare(e1->server()->configuration().displayName(), e2->server()->configuration().displayName()) <= 0;
            break;
        case EventsModel::LocationColumn:
            re = QString::localeAwareCompare(e1->uiLocation(), e2->uiLocation()) <= 0;
            break;
        case EventsModel::TypeColumn:
            re = QString::localeAwareCompare(e1->uiType(), e2->uiType()) <= 0;
            break;
        case EventsModel::DurationColumn:
            re = e1->durationInSeconds() <= e2->durationInSeconds();
            break;
        case EventsModel::LevelColumn:
            re = e1->level() <= e2->level();
            break;
        case EventsModel::DateColumn:
            re = e1->utcStartDate() <= e2->utcStartDate();
            break;
        default:
            Q_ASSERT_X(false, "EventSort", "sorting not implemented for column");
            re = true;
        }

        if (lessThan)
            return re;
        else
            return !re;
    }
};

void EventsModel::setIncompleteEventsFirst(bool enabled)
{
    if (incompleteEventsFirst == enabled)
        return;
    incompleteEventsFirst = enabled;
    resort();
}

void EventsModel::sort(int column, Qt::SortOrder order)
{
    this->sortColumn = column;
    this->sortOrder = order;

    emit layoutAboutToBeChanged();
    bool lessThan = order == Qt::AscendingOrder;
    qSort(items.begin(), items.end(), EventSort(column, lessThan, incompleteEventsFirst));
    emit layoutChanged();
}

bool EventsModel::Filter::operator()(const EventData *data) const
{
    if (data->level() < level ||
            (!types.isNull() && (int)data->type() >= 0 && !types[(int)data->type()]) ||
            (!dateBegin.isNull() && data->serverStartDate() < dateBegin) ||
            (!dateEnd.isNull() && data->serverStartDate() > dateEnd))
        return false;

    QHash<DVRServer*, QSet<int> >::ConstIterator it = sources.find(data->server());
    if (!sources.isEmpty() && (it == sources.end() || (!it->isEmpty() && !it->contains(data->locationId()))))
        return false;

    return true;
}

void EventsModel::applyFilters(bool fromCache)
{
    if (fromCache)
    {
        beginResetModel();
        items.clear();

        for (QHash<DVRServer*,QList<EventData*> >::Iterator it = cachedEvents.begin(); it != cachedEvents.end(); ++it)
        {
            if (!m_filter.sources.isEmpty() && !m_filter.sources.contains(it.key()))
                continue;

            for (QList<EventData*>::Iterator eit = it->begin(); eit != it->end(); ++eit)
            {
                if (m_filter(*eit))
                    items.append(*eit);
            }
        }

        bool block = blockSignals(true);
        resort();
        blockSignals(block);

        endResetModel();
    }
    else
    {
        /* Group contiguous removed rows together; provides a significant boost in performance */
        int removeFirst = -1;
        for (int i = 0; ; ++i)
        {
            if (i < items.size() && !m_filter(items[i]))
            {
                if (removeFirst < 0)
                    removeFirst = i;
            }
            else if (removeFirst >= 0)
            {
                beginRemoveRows(QModelIndex(), removeFirst, i-1);
                items.erase(items.begin()+removeFirst, items.begin()+i);
                i = removeFirst;
                removeFirst = -1;
                endRemoveRows();
            }

            if (i == items.size())
                break;
        }

#if 0
        /* Experimental concurrent implementation; would later be extended to be nonblocking */
        QList<EventData*> newItems = QtConcurrent::blockingFiltered(items, m_filter);

        int oFirstRemove = -1;
        for (int oI = 0, nI = 0; oI < items.size(); ++oI)
        {
            if (nI >= newItems.size())
            {
                if (oFirstRemove < 0)
                    oFirstRemove = oI;

                beginRemoveRows(QModelIndex(), oFirstRemove, items.size()-1);
                items.erase(items.begin()+oFirstRemove, items.end());
                endRemoveRows();
                break;
            }

            if (oFirstRemove < 0 && items[oI] != newItems[nI])
                oFirstRemove = oI;
            else if (oFirstRemove >= 0 && items[oI] == newItems[nI])
            {
                beginRemoveRows(QModelIndex(), oFirstRemove, oI-1);
                items.erase(items.begin()+oFirstRemove, items.begin()+oI);
                oI = oFirstRemove;
                oFirstRemove = -1;
                endRemoveRows();
            }

            if (oFirstRemove < 0)
            {
                ++nI;
            }
        }
#endif
    }

    emit filtersChanged();
}

QString EventsModel::filterDescription() const
{
    QString re;

    if (m_filter.level > EventLevel::Info)
        re = tr("%1 events").arg(m_filter.level.uiString());
    else if (m_filter.dateBegin.isNull() && m_filter.dateEnd.isNull())
        re = tr("Recent events");
    else
        re = tr("All events");

    bool allCameras = true;
    for (QHash<DVRServer*, QSet<int> >::ConstIterator it = m_filter.sources.begin();
         it != m_filter.sources.end(); ++it)
    {
        if (it->count() != it.key()->cameras().size()+1)
        {
            allCameras = false;
            break;
        }
    }

    if (!m_filter.sources.isEmpty() && m_filter.sources.size() != m_serverRepository->servers().size())
    {
        if (m_filter.sources.size() == 1)
        {
            /* Single server */
            if (!allCameras)
            {
                if (m_filter.sources.begin()->size() == 1)
                    re += tr(" on %1").arg(*m_filter.sources.begin()->begin());
                else
                    re += tr(" on selected cameras");
            }

            re += tr(" from %1").arg(m_filter.sources.begin().key()->configuration().displayName());
        }
        else if (!allCameras)
            re += tr(" on selected cameras");
        else
            re += tr(" from selected servers");
    }
    else if (!allCameras)
        re += tr(" on selected cameras");

    if (!m_filter.dateBegin.isNull() && !m_filter.dateEnd.isNull())
    {
        if (m_filter.dateBegin.date() == m_filter.dateEnd.date())
            re += tr(" on %1").arg(m_filter.dateBegin.date().toString());
        else
            re += tr(" between %1 to %2").arg(m_filter.dateBegin.date().toString(), m_filter.dateEnd.date().toString());
    }
    else if (!m_filter.dateBegin.isNull())
        re += tr(" after %1").arg(m_filter.dateBegin.date().toString());
    else if (!m_filter.dateEnd.isNull())
        re += tr(" before %1").arg(m_filter.dateEnd.date().toString());

    return re;
}

void EventsModel::clearFilters()
{
    if (m_filter.sources.isEmpty() && m_filter.dateBegin.isNull() && m_filter.dateEnd.isNull() &&
        m_filter.types.isNull() && m_filter.level == EventLevel::Info)
        return;

    bool reload = !m_filter.dateBegin.isNull() || !m_filter.dateEnd.isNull();

    m_filter.sources.clear();
    m_filter.dateBegin = m_filter.dateEnd = QDateTime();
    m_filter.types.clear();
    m_filter.level = EventLevel::Info;

    if (reload)
    {
        beginResetModel();
        items.clear();
        updateServers();
        endResetModel();
    } else
        applyFilters();
}

void EventsModel::setFilterDates(const QDateTime &begin, const QDateTime &end)
{
    m_filter.dateBegin = begin;
    m_filter.dateEnd = end;

    beginResetModel();
    items.clear();
    updateServers();
    endResetModel();
}

void EventsModel::setFilterDay(const QDateTime &date)
{
    setFilterDates(QDateTime(date.date(), QTime(0, 0)), QDateTime(date.date(), QTime(23, 59, 59, 999)));
}

void EventsModel::setFilterLevel(EventLevel minimum)
{
    if (m_filter.level == minimum)
        return;

    bool fast = minimum > m_filter.level;
    m_filter.level = minimum;

    applyFilters(!fast);
}

void EventsModel::setFilterSources(const QMap<DVRServer*, QList<int> > &sources)
{
    bool fast = false;

    if (sources.size() <= m_filter.sources.size())
    {
        fast = true;
        /* If the new sources contain any that the old don't, we can't do fast filtering */
        for (QMap<DVRServer*,QList<int> >::ConstIterator nit = sources.begin(); nit != sources.end(); ++nit)
        {
            QHash<DVRServer*, QSet<int> >::Iterator oit = m_filter.sources.find(nit.key());
            if (oit == m_filter.sources.end())
            {
                fast = false;
                break;
            }

            for (QList<int>::ConstIterator it = nit->begin(); it != nit->end(); ++it)
            {
                if (!oit->contains(*it))
                {
                    fast = false;
                    break;
                }
            }

            if (!fast)
                break;
        }
    }
    else if (m_filter.sources.isEmpty())
        fast = true;

    m_filter.sources.clear();
    for (QMap<DVRServer*, QList<int> >::ConstIterator nit = sources.begin(); nit != sources.end(); ++nit)
        m_filter.sources.insert(nit.key(), nit->toSet());

    applyFilters(!fast);
}

void EventsModel::setFilterSource(DVRCamera *camera)
{
    if (!camera)
        return;

    QMap<DVRServer*,QList<int> > sources;
    sources.insert(camera->data().server(), QList<int>() << camera->data().id());
    setFilterSources(sources);
}

void EventsModel::setFilterSource(DVRServer *server)
{
    if (!server)
        return;

    QMap<DVRServer*,QList<int> > sources;
    sources.insert(server, QList<int>());
    setFilterSources(sources);
}

void EventsModel::setFilterTypes(const QBitArray &typemap)
{
    bool fast = true;

    if (!m_filter.types.isNull() && m_filter.types.size() == typemap.size())
    {
        for (int i = 0; i < typemap.size(); ++i)
        {
            if (typemap[i] && !m_filter.types[i])
            {
                fast = false;
                break;
            }
        }
    }

    m_filter.types = typemap;
    applyFilters(!fast);
}

void EventsModel::setUpdateInterval(int ms)
{
    if (ms > 0)
    {
        updateTimer.setInterval(ms);
        updateTimer.start();
    }
    else
        updateTimer.stop();
}

void EventsModel::updateServers()
{
    foreach (DVRServer *s, m_serverRepository->servers())
        updateServer(s);
}

void EventsModel::updateServer(DVRServer *server)
{
    if (!server && !(server = qobject_cast<DVRServer*>(sender())))
    {
        ServerRequestManager *srm = qobject_cast<ServerRequestManager*>(sender());
        if (srm)
            server = srm->server;
        else
            return;
    }

    if (!server->isOnline() || updatingServers.contains(server))
        return;

    updatingServers.insert(server);
    if (updatingServers.size() == 1)
        emit loadingStarted();

    EventsLoader *eventsLoader = new EventsLoader(server);
    eventsLoader->setLimit(serverEventsLimit);
    eventsLoader->setStartTime(m_filter.dateBegin);
    eventsLoader->setEndTime(m_filter.dateEnd);
    connect(eventsLoader, SIGNAL(eventsLoaded(bool,QList<EventData*>)), this, SLOT(eventsLoaded(bool,QList<EventData*>)));
    eventsLoader->loadEvents();
}

void EventsModel::eventsLoaded(bool ok, const QList<EventData *> &events)
{
    EventsLoader *eventsLoader = qobject_cast<EventsLoader *>(sender());
    Q_ASSERT(eventsLoader);

    DVRServer *server = eventsLoader->server();
    if (!server)
        return;

    if (ok)
    {
        QList<EventData*> &cache = cachedEvents[server];
        qDeleteAll(cache);
        cache = events;

        applyFilters();
    }

    if (updatingServers.remove(server) && updatingServers.isEmpty())
        emit loadingFinished();
}
