#include <QCoreApplication>
#include <QThread>
// A harmless live process that the checkpoint test copies under an agent's name.
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QThread::sleep(30);
    return 0;
}
