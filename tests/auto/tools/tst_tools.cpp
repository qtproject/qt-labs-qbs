/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the Qt Build Suite.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#include <app/qbs/parser/commandlineparser.h>
#include <logging/logger.h>
#include <tools/buildoptions.h>
#include <tools/fileinfo.h>
#include <tools/hostosinfo.h>
#include <QDir>
#include <QTemporaryFile>
#include <QtTest>

using namespace qbs;

class TestTools : public QObject
{
    Q_OBJECT
private slots:
    void testValidCommandLine()
    {
        QTemporaryFile projectFile;
        QVERIFY(projectFile.open());
        const QStringList fileArgs = QStringList() << "-f" << projectFile.fileName();
        QStringList args;
        args.append("-vvk");
        args.append("-v");
        args << "--products" << "blubb";
        args << "--changed-files" << "foo,bar" << fileArgs;
        CommandLineParser parser;
        QVERIFY(parser.parseCommandLine(args));
        QCOMPARE(Logger::instance().level(), LoggerTrace);
        QCOMPARE(parser.command(), BuildCommandType);
        QCOMPARE(parser.products(), QStringList() << "blubb");
        QCOMPARE(parser.buildOptions().changedFiles.count(), 2);
        QVERIFY(parser.buildOptions().keepGoing);
        QVERIFY(parser.parseCommandLine(QStringList() << "-vvvqqq" << fileArgs));
        QCOMPARE(Logger::instance().level(), Logger::defaultLevel());
        QVERIFY(parser.parseCommandLine(QStringList() << "-vvqqq" << fileArgs));
        QCOMPARE(Logger::instance().level(), LoggerWarning);
        QVERIFY(parser.parseCommandLine(QStringList() << "-vvvqq" << fileArgs));
        QCOMPARE(Logger::instance().level(), LoggerDebug);
        QVERIFY(parser.parseCommandLine(QStringList() << "--log-level" << "trace" << fileArgs));
        QCOMPARE(Logger::instance().level(), LoggerTrace);
    }

    void testInvalidCommandLine()
    {
        QTemporaryFile projectFile;
        QVERIFY(projectFile.open());
        const QStringList fileArgs = QStringList() << "-f" << projectFile.fileName();
        CommandLineParser parser;
        QVERIFY(!parser.parseCommandLine(QStringList() << "-x" << fileArgs)); // Unknown short option.
        QVERIFY(!parser.parseCommandLine(QStringList() << "--xyz" << fileArgs)); // Unknown long option.
        QVERIFY(!parser.parseCommandLine(QStringList() << "-vjv" << fileArgs)); // Invalid position.
        QVERIFY(!parser.parseCommandLine(QStringList() << "-j" << fileArgs));  // Missing argument.
        QVERIFY(!parser.parseCommandLine(QStringList() << "-j" << "0" << fileArgs)); // Wrong argument.
        QVERIFY(!parser.parseCommandLine(QStringList() << "--products" << fileArgs));  // Missing argument.
        QVERIFY(!parser.parseCommandLine(QStringList() << "--changed-files" << "," << fileArgs)); // Wrong argument.
        QVERIFY(!parser.parseCommandLine(QStringList() << "--log-level" << "blubb" << fileArgs)); // Wrong argument.
    }

    void testFileInfo()
    {
        QCOMPARE(FileInfo::fileName("C:/waffl/copter.exe"), QString("copter.exe"));
        QCOMPARE(FileInfo::baseName("C:/waffl/copter.exe.lib"), QString("copter"));
        QCOMPARE(FileInfo::completeBaseName("C:/waffl/copter.exe.lib"), QString("copter.exe"));
        QCOMPARE(FileInfo::path("abc"), QString("."));
        QCOMPARE(FileInfo::path("/abc/lol"), QString("/abc"));
        QVERIFY(!FileInfo::isAbsolute("bla/lol"));
        QVERIFY(FileInfo::isAbsolute("/bla/lol"));
        if (HostOsInfo::isWindowsHost())
            QVERIFY(FileInfo::isAbsolute("C:\\bla\\lol"));
        QCOMPARE(FileInfo::resolvePath("/abc/lol", "waffl"), QString("/abc/lol/waffl"));
        QCOMPARE(FileInfo::resolvePath("/abc/def/ghi/jkl/", "../foo/bar"), QString("/abc/def/ghi/foo/bar"));
        QCOMPARE(FileInfo::resolvePath("/abc/def/ghi/jkl/", "../../foo/bar"), QString("/abc/def/foo/bar"));
        QCOMPARE(FileInfo::resolvePath("/abc", "../../../foo/bar"), QString("/foo/bar"));
        QCOMPARE(FileInfo("/does/not/exist").lastModified(), FileTime());
    }

    void testProjectFileLookup()
    {
        const QString srcDir = QLatin1String(SRCDIR);
        const QString noProjectsDir = srcDir + QLatin1String("data/dirwithnoprojects");
        const QString oneProjectDir = srcDir + QLatin1String("data/dirwithoneproject");
        const QString multiProjectsDir = srcDir + QLatin1String("data/dirwithmultipleprojects");
        Q_ASSERT(QDir(noProjectsDir).exists() && QDir(oneProjectDir).exists()
                && QDir(multiProjectsDir).exists());
        CommandLineParser parser;
        const QStringList args(QLatin1String("-f"));
        QString projectFilePath = multiProjectsDir + QLatin1String("/project.qbs");
        QVERIFY(parser.parseCommandLine(args + QStringList(projectFilePath)));
        QCOMPARE(projectFilePath, parser.projectFilePath());
        projectFilePath = oneProjectDir + QLatin1String("/project.qbs");
        QVERIFY(parser.parseCommandLine(args + QStringList(oneProjectDir)));
        QCOMPARE(projectFilePath, parser.projectFilePath());
        QVERIFY(!parser.parseCommandLine(args + QStringList(noProjectsDir)));
        QVERIFY(!parser.parseCommandLine(args + QStringList(multiProjectsDir)));
    }
};

QTEST_MAIN(TestTools)

#include "tst_tools.moc"
