/*
    main.cpp

    This file is part of Kuesa.

    Copyright (C) 2019 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
    Author: Jean-Michaël Celerier <jean-michael.celerier@kdab.com>

    Licensees holding valid proprietary KDAB Kuesa licenses may use this file in
    accordance with the Kuesa Enterprise License Agreement provided with the Software in the
    LICENSE.KUESA.ENTERPRISE file.

    Contact info@kdab.com if any conditions of this licensing are not clear to you.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <Kuesa/private/gltf2parser_p.h>
#include <Kuesa/private/gltf2exporter_p.h>
#include <Kuesa/SceneEntity>

#include <QCoreApplication>
#include <QDir>
#include <QCommandLineParser>
#include <QElapsedTimer>

int main(int argc, char *argv[])
{
    using namespace Kuesa;
    using namespace Kuesa::GLTF2Import;

    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Kuesa Asset Compressor"));
    app.setOrganizationDomain(QStringLiteral("kdab.com"));
    app.setOrganizationName(QStringLiteral("KDAB"));
    app.setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser cmdline;
    cmdline.addHelpOption();
    cmdline.addVersionOption();
    cmdline.addPositionalArgument("input", QObject::tr("glTF2 file to compress"));
    cmdline.addPositionalArgument("output", QObject::tr("Target directory"));

    QCommandLineOption compRatio({"r", "ratio"}, QObject::tr("Compression ratio, between 1 and 16"), QString(), "7");
    cmdline.addOption(compRatio);

    cmdline.process(app);

    const auto& args = cmdline.positionalArguments();
    if(args.size() != 2)
    {
        qCritical("Invalid arguments specified.\n%s", cmdline.helpText().toLatin1().constData());
        return 1;
    }

    int ratio = cmdline.value(compRatio).toInt();
    if(ratio < 1 || ratio > 16)
    {
        qCritical("Invalid compression ratio.");
        return 1;
    }

    QString asset = args[0];
    if(!QFileInfo::exists(asset))
    {
        qCritical("Could not find input file.");
        return 1;
    }

    QDir sourceDir = QFileInfo(asset).dir();
    QDir targetDir = args[1];
    if(!targetDir.exists())
    {
        if(!QDir{}.mkpath(args[1]))
        {
            qCritical("Could not create target directory.");
            return 1;
        }
    }

    // Parsing
    SceneEntity scene;
    GLTF2Context ctx;

    GLTF2Parser parser(&scene);
    parser.setContext(&ctx);

    auto res = parser.parse(asset);
    if(!res)
    {
        qCritical("Could not parse glTF2 file.");
        return 1;
    }

    // Compression
    GLTF2ExportConfiguration configuration;
    configuration.setMeshCompressionEnabled(true);
    configuration.setAttributeQuantizationLevel(GLTF2ExportConfiguration::Position, ratio);
    configuration.setAttributeQuantizationLevel(GLTF2ExportConfiguration::Normal, ratio);
    configuration.setAttributeQuantizationLevel(GLTF2ExportConfiguration::Color, ratio);
    configuration.setAttributeQuantizationLevel(GLTF2ExportConfiguration::TextureCoordinate, ratio);
    configuration.setAttributeQuantizationLevel(GLTF2ExportConfiguration::Generic, ratio);

    GLTF2Exporter exporter;
    exporter.setContext(&ctx);
    exporter.setScene(&scene);
    exporter.setConfiguration(configuration);

    QElapsedTimer timer;
    timer.start();

    auto new_asset = exporter.saveInFolder(sourceDir, targetDir);

    if(!res)
    {
        QTextStream errors;
        for(const auto& err : exporter.errors())
            errors << err << "\n";
        qCritical("Could not write glTF2 file. \n%s", errors.readAll().toLatin1().constData());
        return 1;
    }

    auto time_taken = timer.nsecsElapsed();
    auto space_taken = QFileInfo(targetDir.filePath("compressedBuffer.bin")).size();
    QTextStream out(stdout);
    out << QObject::tr("Kuesa: Compressing mesh took %1 milliseconds\n").arg(time_taken / 1000000);
    out << QObject::tr("Kuesa: Space taken by compressed meshes: %2 bytes\n").arg(space_taken);

    return 0;
}
