#include "bios.h"

#include <QCoreApplication>
#include <QLocale>
#include <QTranslator>
#include <QCommandLineParser>
#include <QDebug>

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "libreaward_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }

    QCommandLineParser parser;
    parser.setApplicationDescription("A fully opensource utility that completely debloat Award Bios to latest 6.00 version, and also allows user to edit anything, including microcodes devices, drivers, addresses, BIOS timings and etc.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("source", QCoreApplication::translate("main", "Source .bin firmware BIOS (needed latest)"));
    parser.addPositionalArgument("destination", QCoreApplication::translate("main", "Destination modified .bin firmware BIOS (ready for flash rom)."));

    Bios bios;
    bios.args = parser.positionalArguments();

    qDebug() << "123";

    return QCoreApplication::exec();
}
