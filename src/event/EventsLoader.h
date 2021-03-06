#ifndef EVENTSLOADER_H
#define EVENTSLOADER_H

#include <QDateTime>
#include <QObject>

class DVRServer;
class EventData;

class EventsLoader : public QObject
{
    Q_OBJECT

public:
    explicit EventsLoader(DVRServer *server, QObject *parent = 0);
    virtual ~EventsLoader();

    DVRServer * server() const { return m_server.data(); }

    void setLimit(int limit);
    void setStartTime(const QDateTime &startTime);
    void setEndTime(const QDateTime &endTime);

    void loadEvents();

signals:
    void eventsLoaded(bool ok, const QList<EventData *> &events);

private slots:
    void serverRequestFinished();
    void eventParseFinished();

private:
    QWeakPointer<DVRServer> m_server;
    int m_limit;
    QDateTime m_startTime;
    QDateTime m_endTime;
};

#endif // EVENTSLOADER_H
