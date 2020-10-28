#include "jackautoconnect.h"

JackAutoconnect::JackAutoconnect(QHash<QRegExp *, QRegExp *>* connectionsToDo, QObject *parent) :
    QObject(parent)
{
    if ((client = jack_client_open("autoconnect", JackNoStartServer, 0)) == 0)
    {
        qCritical() << "Unable to connect to jack server :( Exiting ...";
    }

    this->knownClients.insert("Jamulus", 0);    // ensure Jamulus is always client 0
    this->connectionsToDo = connectionsToDo;

    // We explicitely want a QueuedConnection since we cannot connect ports in the callback/notification-thread
    connect(this, SIGNAL(newPort()), this, SLOT(doNewPort()), Qt::QueuedConnection);

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
    if (this->connectionsToDo == nullptr) {
        connectJackTripSuperCollider();
        connectJamulusSuperCollider();
    } else {
        connectRegex();
    }
}

void JackAutoconnect::connectJack(const QString& src, const QString& dst)
{
    int n = jack_connect(this->client, src.toUtf8().constData(), dst.toUtf8().constData());
    switch (n) {
    case 0:
        qDebug() << "Connected output port " << src << " to " << dst;
        break;
    case EEXIST:
        qDebug() << "Already connected output port " << src << " to " << dst;
        break;
    default:
        qWarning() << "Failed to connect output port " << src << " to " << dst;
    }
}

void JackAutoconnect::connectRegex()
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
                        connectJack(existingOutPort, existingInPort);
                    }
                }
            }
        }
    }
}

void JackAutoconnect::connectJackTripSuperCollider()
{
    static const unsigned int CHANNELS = 2;
    static const QString JT_RECEIVE(":receive_");
    static const QString JT_SEND(":send_");
    static const QString JT_RECEIVE_RX(".*:receive_.*");
    static const QString JT_SEND_RX(".*:send_.*");
    static const QString SC_IN("SuperCollider:in_");
    static const QString SC_OUT("SuperCollider:out_");
    static const QString SN_IN("supernova:input_");
    static const QString SN_OUT("supernova:output_");

    jack_port_t *port;
    const char ** outPorts;
    const char ** inPorts;
    long unsigned int i = 0;

    //qDebug() << "Connecting JackTrip clients to SuperCollider";

    // Iterate over all output ports that match JackTrip receive pattern
    outPorts = jack_get_ports(this->client, JT_RECEIVE_RX.toUtf8().constData(), NULL, JackPortIsOutput);
    if (outPorts != nullptr) {
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
            QString serverPortName(SC_IN + QString::number(serverChannelNum));
            port = jack_port_by_name(this->client, serverPortName.toUtf8().constData());
            if (port == nullptr) {
                serverPortName = SN_IN + QString::number(serverChannelNum);
                port = jack_port_by_name(this->client, serverPortName.toUtf8().constData());
            }

            // check if already connected first
            if (port != nullptr) {
                if (!jack_port_connected_to(port, clientPortName.toUtf8().constData())) {
                    connectJack(clientPortName, serverPortName);
                }
            }
        }
        free(outPorts);
    }

    // Iterate over all input ports that match JackTrip send pattern
    inPorts = jack_get_ports(this->client, JT_SEND_RX.toUtf8().constData(), NULL, JackPortIsInput);
    if (inPorts != nullptr) {
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
            QString serverPortName(SC_OUT + QString::number(serverChannelNum));
            port = jack_port_by_name(this->client, serverPortName.toUtf8().constData());
            if (port == nullptr) {
                serverPortName = SN_OUT + QString::number(serverChannelNum);
                port = jack_port_by_name(this->client, serverPortName.toUtf8().constData());
            }

            // check if already connected first
            if (port != nullptr) {
                if (!jack_port_connected_to(port, clientPortName.toUtf8().constData())) {
                    connectJack(serverPortName, clientPortName);
                }
            }
        }
        free(inPorts);
    }
}

void JackAutoconnect::connectJamulusSuperCollider()
{
    static const unsigned int CHANNELS = 2;
    static const QString JAMULUS_INPUT_LEFT("Jamulus:input left");
    static const QString JAMULUS_INPUT_RIGHT("Jamulus:input right");
    static const QString JAMULUS_OUTPUT_LEFT("Jamulus:output left");
    static const QString JAMULUS_OUTPUT_RIGHT("Jamulus:output right");
    static const QString SC_IN("SuperCollider:in_");
    static const QString SC_OUT("SuperCollider:out_");
    static const QString SN_IN("supernova:input_");
    static const QString SN_OUT("supernova:output_");

    jack_port_t *port;

    // remember client next time around
    const int clientNum = getClientNum("Jamulus");
    const int leftChannelNum = (clientNum * CHANNELS) + 1;
    const int rightChannelNum = (clientNum * CHANNELS) + 2;

    // connect Jamulus input left
    QString serverPortName(SC_OUT + QString::number(leftChannelNum));
    port = jack_port_by_name(this->client, serverPortName.toUtf8().constData());
    if (port == nullptr) {
        serverPortName = SN_OUT + QString::number(leftChannelNum);
        port = jack_port_by_name(this->client, serverPortName.toUtf8().constData());
    }
    if (port != nullptr && !jack_port_connected_to(port, JAMULUS_INPUT_LEFT.toUtf8().constData())) {
        connectJack(serverPortName, JAMULUS_INPUT_LEFT);
    }

    // connect Jamulus input right
    serverPortName = (SC_OUT + QString::number(rightChannelNum));
    port = jack_port_by_name(this->client, serverPortName.toUtf8().constData());
    if (port == nullptr) {
        serverPortName = SN_OUT + QString::number(rightChannelNum);
        port = jack_port_by_name(this->client, serverPortName.toUtf8().constData());
    }
    if (port != nullptr && !jack_port_connected_to(port, JAMULUS_INPUT_RIGHT.toUtf8().constData())) {
        connectJack(serverPortName, JAMULUS_INPUT_RIGHT);
    }

    // connect Jamulus output left
    serverPortName = (SC_IN + QString::number(leftChannelNum));
    port = jack_port_by_name(this->client, serverPortName.toUtf8().constData());
    if (port == nullptr) {
        serverPortName = SN_IN + QString::number(leftChannelNum);
        port = jack_port_by_name(this->client, serverPortName.toUtf8().constData());
    }
    if (port != nullptr && !jack_port_connected_to(port, JAMULUS_OUTPUT_LEFT.toUtf8().constData())) {
        connectJack(JAMULUS_OUTPUT_LEFT, serverPortName);
    }

    // connect Jamulus output right
    serverPortName = (SC_IN + QString::number(rightChannelNum));
    port = jack_port_by_name(this->client, serverPortName.toUtf8().constData());
    if (port == nullptr) {
        serverPortName = SN_IN + QString::number(rightChannelNum);
        port = jack_port_by_name(this->client, serverPortName.toUtf8().constData());
    }
    if (port != nullptr && !jack_port_connected_to(port, JAMULUS_OUTPUT_RIGHT.toUtf8().constData())) {
        connectJack(JAMULUS_OUTPUT_RIGHT, serverPortName);
    }
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