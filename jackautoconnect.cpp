#include "jackautoconnect.h"

JackAutoconnect::JackAutoconnect(QHash<QRegExp *, QRegExp *>* connectionsToDo, QObject *parent) :
    QObject(parent)
{
    if ((client = jack_client_open("autoconnect", JackNoStartServer, 0)) == 0)
    {
        qCritical() << "Unable to connect to jack server :( Exiting ...";
    }

    this->connectionsToDo = connectionsToDo;

    // We explicitely want a QueuedConnection since we cannot connect ports in the callback/notification-thread
    if (this->connectionsToDo == nullptr) {
        connect(this, SIGNAL(newPort()), this, SLOT(doNewPortJackTripSuperCollider()), Qt::QueuedConnection);
    } else {
        connect(this, SIGNAL(newPort()), this, SLOT(doNewPort()), Qt::QueuedConnection);
    }

    jack_set_port_registration_callback(client, &myRegCallback_static, (void*)this);

    jack_activate(client);
}

void JackAutoconnect::myRegCallback_static(jack_port_id_t port, int action, void *arg)
{
    Q_UNUSED(port);

    if (action == 0)
    {
        // A port has been unregistered. Nothing to do for us
        return;
    }

    // Emit the signal so that the main thread will check the ports and connect them
    emit ((JackAutoconnect*) arg)->newPort();
}

void JackAutoconnect::doNewPort()
{
    const char ** outPorts;
    const char ** inPorts;
    long unsigned int i = 0;

    qDebug() << "Port registered";

    QList<QString> outputPorts;
    QList<QString> inputPorts;

    // Iterate over all output ports and save them to our QList
    outPorts = jack_get_ports(client, NULL, NULL, JackPortIsOutput);
    for (i = 0; outPorts[i]; ++i)
    {
        qDebug() << "Found output port:" << QString(outPorts[i]);
        outputPorts.append(QString(outPorts[i]));
    }
    free(outPorts);

    // Iterate over all output ports and save them to our QList
    inPorts = jack_get_ports(client, NULL, NULL, JackPortIsInput);
    for (i = 0; inPorts[i]; ++i)
    {
        qDebug() << "Found input port:" << QString(inPorts[i]);
        inputPorts.append(QString(inPorts[i]));
    }
    free(inPorts);

    // now check all our regexes to see if a pair of existing jack ports matches
    foreach(QRegExp* expr, connectionsToDo->keys())
    {
        qDebug() << "Checking RegEx pair" << expr->pattern() << "->" << connectionsToDo->value(expr)->pattern();
        foreach (QString existingOutPort, outputPorts)
        {
            if ((expr->indexIn(existingOutPort)) != -1)
            {
                qDebug() << "Got matching outputPort:" << existingOutPort;
                foreach (QString existingInPort, inputPorts)
                {
                    if ((connectionsToDo->value(expr)->indexIn(existingInPort)) != -1)
                    {
                        qDebug() << "Got matching inputPort:" << existingInPort << "CONNECT!";

                        // We don't even check if the ports are connected yet, we just tell jack to connect them and even ignore the return value
                        jack_connect(client, existingOutPort.toUtf8().constData(), existingInPort.toUtf8().constData());
                    }
                }
            }
        }
    }
}

void JackAutoconnect::doNewPortJackTripSuperCollider()
{
    static const unsigned int CHANNELS = 2;
    static const QString JT_RECEIVE(":receive_");
    static const QString JT_SEND(":send_");
    static const QString JT_RECEIVE_RX(".*:receive_.*");
    static const QString JT_SEND_RX(".*:send_.*");
    static const QString SC_IN("SuperCollider:in_");
    static const QString SC_OUT("SuperCollider:out_");

    const char ** outPorts;
    const char ** inPorts;
    long unsigned int i = 0;

    qDebug() << "Connecting JackTrip clients to SuperCollider";

    // Iterate over all output ports that match JackTrip receive pattern
    outPorts = jack_get_ports(client, JT_RECEIVE_RX.toUtf8().constData(), NULL, JackPortIsOutput);
    for (i = 0; outPorts[i]; ++i)
    {
        const QString clientPortName(outPorts[i]);
        //qDebug() << "Found output port: " << clientPortName;

        // extract client name from connection name
        const int receiveIdx = clientPortName.indexOf(JT_RECEIVE);
        const QString clientName(clientPortName.left(receiveIdx));

        // remember client next time around
        const int clientNum = getClientNum(clientName);

        // extract client channel number
        const int clientChannelDigits = clientPortName.size() - (receiveIdx + JT_RECEIVE.size());
        const int clientChannelNum = clientPortName.right(clientChannelDigits).toUInt();

        // determine server channel number and port name
        const int serverChannelNum = (clientNum * CHANNELS) + clientChannelNum;
        const QString serverPortName(SC_IN + QString::number(serverChannelNum));

        //qDebug() << "Connecting output port " << clientPortName << " to " << serverPortName;

        // We don't even check if the ports are connected yet, we just tell jack to connect them and even ignore the return value
        jack_connect(client, clientPortName.toUtf8().constData(), serverPortName.toUtf8().constData());
    }
    free(outPorts);

    // Iterate over all input ports that match JackTrip send pattern
    inPorts = jack_get_ports(client, JT_SEND_RX.toUtf8().constData(), NULL, JackPortIsInput);
    for (i = 0; inPorts[i]; ++i)
    {
        const QString clientPortName(inPorts[i]);
        //qDebug() << "Found input port: " << clientPortName;

        // extract client name from connection name
        const int sendIdx = clientPortName.indexOf(JT_SEND);
        const QString clientName(clientPortName.left(sendIdx));

        // remember client next time around
        const int clientNum = getClientNum(clientName);

        // extract client channel number
        const int clientChannelDigits = clientPortName.size() - (sendIdx + JT_SEND.size());
        const int clientChannelNum = clientPortName.right(clientChannelDigits).toUInt();

        // determine server channel number and port name
        const int serverChannelNum = (clientNum * CHANNELS) + clientChannelNum;
        const QString serverPortName(SC_OUT + QString::number(serverChannelNum));

        //qDebug() << "Connecting input port " << clientPortName << " to " << serverPortName;

        // We don't even check if the ports are connected yet, we just tell jack to connect them and even ignore the return value
        jack_connect(client,  serverPortName.toUtf8().constData(), clientPortName.toUtf8().constData());
    }
    free(inPorts);
}

int JackAutoconnect::getClientNum(const QString& clientName)
{
    QHash<QString, int>::const_iterator i = this->knownClients.find(clientName);
    if (i != this->knownClients.end()) {
        return i.value();
    }
    
    const int clientNum = this->knownClients.size();
    this->knownClients.insert(clientName, clientNum);
    return clientNum;
}