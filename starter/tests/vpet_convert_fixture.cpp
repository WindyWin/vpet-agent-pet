// Test-only bridge: compare native conversion output with the Python reference.
#include "packs/vpet_mod.h"
#include "packs/installer.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <cstdio>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 6) return 2;
    pet::packs::VPetOptions options; options.id = app.arguments()[3]; options.terms = app.arguments()[4];
    QJsonObject report, metadata; QString error, terms;
    if (!pet::packs::convertVPetMod(app.arguments()[1], app.arguments()[2], options, report, error)
        || !pet::packs::validate(app.arguments()[2], metadata, terms, error)) { std::fprintf(stderr, "%s\n", qPrintable(error)); return 1; }
    QFile out(app.arguments()[5]); const auto bytes = QJsonDocument(report).toJson();
    return out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size() ? 0 : 1;
}
