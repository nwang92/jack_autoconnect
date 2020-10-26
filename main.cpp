#include <QCoreApplication>
#include "jackautoconnect.h"

int main(int argc, char *argv[])
{
    int i;
    QHash<QRegExp*, QRegExp*>* connectionsToDo = nullptr;
    JackAutoconnect* worker = nullptr;

    QCoreApplication a(argc, argv);

    if (argc >= 3)
    {
        // Iterate over the command line arguments to create Output/Input-port RegEx pairs that shall be automatically connected
        connectionsToDo = new QHash<QRegExp*, QRegExp*>();
        for (i = 1; i < (argc - 1); i = i + 2)
        {
            connectionsToDo->insert(new QRegExp(QString(argv[i])), new QRegExp(QString(argv[i + 1])));
        }
    }

    // Create the object that connects to jack and does all the work
    worker = new JackAutoconnect(connectionsToDo);
    Q_UNUSED(worker);

    // Simply enter the event loop and wait for the things to come
    return a.exec();
}
