#ifndef JACKAUTOCONNECT_H
#define JACKAUTOCONNECT_H

#include <QObject>
#include <QDebug>
#include <QRegExp>
#include <QHash>
#include <QAtomicInteger>
#include <jack/jack.h>

class JackAutoconnect : public QObject
{
    Q_OBJECT
public:
    // ctor :D
    explicit JackAutoconnect(QHash<QRegExp*, QRegExp*>* connectionsToDo, QObject *parent = 0);

    // Method to be calles whenever a new port has been registered with jack
    static void myRegCallback_static(jack_port_id_t port, int action, void *arg);

private:
    // The RegEx pairs of port names to connect
    QHash<QRegExp*, QRegExp*>* connectionsToDo;

    // Hash map of clients that we have seen before
    QHash<QString, int> knownClients;

    // Our jack client handle
    jack_client_t* client;

    // number of active signals being processed
    QAtomicInteger<int> triggers;

    // set to true after all jamulus connections are completed
    bool jamulusConnected;

    // connects jack output connection (src) to jack input connection (dst)
    void connectJack(const QString& src, const QString& dst);

    // Main worker method that connects RegEx pairs
    void connectRegex();

    // Main worker method that connects JackTrip clients to a SuperCollider server
    void connectJackTripSuperCollider();

    // Main worker method that connects a Jamulus bridge client to a SuperCollider server
    void connectJamulusSuperCollider();

    // returns auto incrementing number for each unique client
    int getClientNum(const QString& clientName);

signals:
    // Will be emitted every time a new port has been registered with jack
    void newPort();

public slots:
    // Main worker method that iterates through all entries in connectionsToDo
    void doNewPort();
};

#endif // JACKAUTOCONNECT_H
