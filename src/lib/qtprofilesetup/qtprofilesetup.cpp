/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
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

#include "qtprofilesetup.h"

#include "qtmoduleinfo.h"

#include <logging/translator.h>
#include <tools/error.h>
#include <tools/fileinfo.h>
#include <tools/profile.h>
#include <tools/scripttools.h>
#include <tools/settings.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QRegExp>
#include <QTextStream>

namespace qbs {
using namespace Internal;

template <class T>
QByteArray utf8JSLiteral(T t)
{
    return toJSLiteral(t).toUtf8();
}

static void copyTemplateFile(const QString &fileName, const QString &targetDirectory,
                             const QString &profileName)
{
    if (!QDir::root().mkpath(targetDirectory)) {
        throw ErrorInfo(Internal::Tr::tr("Setting up Qt profile '%1' failed: "
                                         "Cannot create directory '%2'.")
                        .arg(profileName, targetDirectory));
    }
    QFile sourceFile(QLatin1String(":/templates/") + fileName);
    const QString targetPath = targetDirectory + QLatin1Char('/') + fileName;
    QFile::remove(targetPath); // QFile::copy() cannot overwrite.
    if (!sourceFile.copy(targetPath)) {
        throw ErrorInfo(Internal::Tr::tr("Setting up Qt profile '%1' failed: "
            "Cannot copy file '%2' into directory '%3' (%4).")
                        .arg(profileName, fileName, targetDirectory, sourceFile.errorString()));
    }
    QFile targetFile(targetPath);
    if (!targetFile.setPermissions(targetFile.permissions() | QFile::WriteUser)) {
        throw ErrorInfo(Internal::Tr::tr("Setting up Qt profile '%1' failed: Cannot set write "
                "permission on file '%2' (%3).")
                .arg(profileName, targetPath, targetFile.errorString()));
    }
}

static QString pathToJSLiteral(const QString &path)
{
    return toJSLiteral(QDir::fromNativeSeparators(path));
}

static void replaceSpecialValues(const QString &filePath, const Profile &profile,
        const QtModuleInfo &module, const QtEnvironment &qtEnvironment)
{
    QFile moduleFile(filePath);
    if (!moduleFile.open(QIODevice::ReadWrite)) {
        throw ErrorInfo(Internal::Tr::tr("Setting up Qt profile '%1' failed: Cannot adapt "
                "module file '%2' (%3).")
                .arg(profile.name(), moduleFile.fileName(), moduleFile.errorString()));
    }
    QByteArray content = moduleFile.readAll();
    content.replace("@name@", utf8JSLiteral(module.moduleName()));
    content.replace("@has_library@", utf8JSLiteral(module.hasLibrary));
    content.replace("@dependencies@", utf8JSLiteral(module.dependencies));
    content.replace("@includes@", utf8JSLiteral(module.includePaths));
    QByteArray propertiesString;
    QByteArray compilerDefines = utf8JSLiteral(module.compilerDefines);
    if (module.qbsName == QLatin1String("declarative")
            || module.qbsName == QLatin1String("quick")) {
        const QByteArray debugMacro = module.qbsName == QLatin1String("declarative")
                    || qtEnvironment.qtMajorVersion < 5
                ? "QT_DECLARATIVE_DEBUG" : "QT_QML_DEBUG";

        const QString indent = QLatin1String("    ");
        QTextStream s(&propertiesString);
        s << "property bool qmlDebugging: false" << endl;
        s << indent << "property string qmlPath";
        if (qtEnvironment.qmlPath.isEmpty())
            s << endl;
        else
            s << ": " << pathToJSLiteral(qtEnvironment.qmlPath) << endl;

        s << indent << "property string qmlImportsPath: "
                << pathToJSLiteral(qtEnvironment.qmlImportPath);

        const QByteArray baIndent(4, ' ');
        compilerDefines = "{\n"
                + baIndent + baIndent + "var result = " + compilerDefines + ";\n"
                + baIndent + baIndent + "if (qmlDebugging)\n"
                + baIndent + baIndent + baIndent + "result.push(\"" + debugMacro + "\");\n"
                + baIndent + baIndent + "return result;\n"
                + baIndent + "}";
    }
    content.replace("@defines@", compilerDefines);
    if (!module.modulePrefix.isEmpty()) {
        if (!propertiesString.isEmpty())
            propertiesString += "\n    ";
        propertiesString += "qtModulePrefix: " + utf8JSLiteral(module.modulePrefix);
    }
    if (module.isStaticLibrary) {
        if (!propertiesString.isEmpty())
            propertiesString += "\n    ";
        propertiesString += "isStaticLibrary: true";
    }
    content.replace("@special_properties@", propertiesString);
    moduleFile.resize(0);
    moduleFile.write(content);
}

static void createModules(Profile &profile, Settings *settings,
                               const QtEnvironment &qtEnvironment)
{
    const QList<QtModuleInfo> modules = qtEnvironment.qtMajorVersion < 5
            ? allQt4Modules(qtEnvironment)
            : allQt5Modules(profile, qtEnvironment);
    const QString profileBaseDir = QString::fromLocal8Bit("%1/qbs/profiles/%2")
            .arg(QFileInfo(settings->fileName()).dir().absolutePath(), profile.name());
    const QString qbsQtModuleBaseDir = profileBaseDir + QLatin1String("/modules/Qt");
    QString removeError;
    if (!qbs::Internal::removeDirectoryWithContents(qbsQtModuleBaseDir, &removeError)) {
        throw ErrorInfo(Internal::Tr::tr("Setting up Qt profile '%1' failed: Could not remove "
                "the existing profile of the same name (%2).").arg(profile.name(), removeError));
    }
    copyTemplateFile(QLatin1String("QtModule.qbs"), qbsQtModuleBaseDir, profile.name());
    copyTemplateFile(QLatin1String("qtfunctions.js"), qbsQtModuleBaseDir, profile.name());
    foreach (const QtModuleInfo &module, modules) {
        const QString qbsQtModuleDir = qbsQtModuleBaseDir + QLatin1Char('/') + module.qbsName;
        QString moduleTemplateFileName;
        if (module.qbsName == QLatin1String("core")) {
            moduleTemplateFileName = QLatin1String("core.qbs");
            copyTemplateFile(QLatin1String("moc.js"), qbsQtModuleDir, profile.name());
        } else if (module.qbsName == QLatin1String("gui")) {
            moduleTemplateFileName = QLatin1String("gui.qbs");
        } else if (module.qbsName == QLatin1String("phonon")) {
            moduleTemplateFileName = QLatin1String("phonon.qbs");
        } else {
            moduleTemplateFileName = QLatin1String("module.qbs");
        }
        copyTemplateFile(moduleTemplateFileName, qbsQtModuleDir, profile.name());
        replaceSpecialValues(qbsQtModuleDir + QLatin1Char('/') + moduleTemplateFileName,
                             profile, module, qtEnvironment);
    }
    profile.setValue(QLatin1String("preferences.qbsSearchPaths"), profileBaseDir);
}

static QString guessMinimumWindowsVersion(const QtEnvironment &qt)
{
    if (qt.mkspecName.startsWith("winrt-"))
        return QLatin1String("6.2");

    if (!qt.mkspecName.startsWith("win32-"))
        return QString();

    if (qt.architecture == QLatin1String("x86_64")
            || qt.architecture == QLatin1String("ia64")) {
        return QLatin1String("5.2");
    }

    QRegExp rex(QLatin1String("^win32-msvc(\\d+)$"));
    if (rex.exactMatch(qt.mkspecName)) {
        int msvcVersion = rex.cap(1).toInt();
        if (msvcVersion < 2012)
            return QLatin1String("5.0");
        else
            return QLatin1String("5.1");
    }

    return qt.qtMajorVersion < 5 ? QLatin1String("5.0") : QLatin1String("5.1");
}

static bool checkForStaticBuild(const QtEnvironment &qt)
{
    if (qt.qtMajorVersion >= 5)
        return qt.qtConfigItems.contains(QLatin1String("static"));
    if (qt.frameworkBuild)
        return false; // there are no Qt4 static frameworks
    QDir libdir(qt.libraryPath);
    const QStringList coreLibFiles
            = libdir.entryList(QStringList(QLatin1String("*Core*")), QDir::Files);
    if (coreLibFiles.isEmpty())
        throw ErrorInfo(Internal::Tr::tr("Could not determine whether Qt is a static build."));
    foreach (const QString &fileName, coreLibFiles) {
        if (QLibrary::isLibrary(qt.libraryPath + QLatin1Char('/') + fileName))
            return false;
    }
    return true;
}

void doSetupQtProfile(const QString &profileName, Settings *settings,
                      const QtEnvironment &_qtEnvironment)
{
    QtEnvironment qtEnvironment = _qtEnvironment;
    const bool staticBuild = checkForStaticBuild(qtEnvironment);

    // determine whether user apps require C++11
    if (qtEnvironment.qtConfigItems.contains(QLatin1String("c++11")) && staticBuild)
        qtEnvironment.configItems.append(QLatin1String("c++11"));

    Profile profile(profileName, settings);
    profile.removeProfile();
    const QString settingsTemplate(QLatin1String("Qt.core.%1"));
    profile.setValue(settingsTemplate.arg("config"), qtEnvironment.configItems);
    profile.setValue(settingsTemplate.arg("qtConfig"), qtEnvironment.qtConfigItems);
    profile.setValue(settingsTemplate.arg("binPath"), qtEnvironment.binaryPath);
    profile.setValue(settingsTemplate.arg("libPath"), qtEnvironment.libraryPath);
    profile.setValue(settingsTemplate.arg("pluginPath"), qtEnvironment.pluginPath);
    profile.setValue(settingsTemplate.arg("incPath"), qtEnvironment.includePath);
    profile.setValue(settingsTemplate.arg("mkspecPath"), qtEnvironment.mkspecPath);
    profile.setValue(settingsTemplate.arg("docPath"), qtEnvironment.documentationPath);
    profile.setValue(settingsTemplate.arg("version"), qtEnvironment.qtVersion);
    profile.setValue(settingsTemplate.arg("libInfix"), qtEnvironment.qtLibInfix);
    profile.setValue(settingsTemplate.arg("buildVariant"), qtEnvironment.buildVariant);
    profile.setValue(settingsTemplate.arg(QLatin1String("staticBuild")), staticBuild);

    // Set the minimum operating system versions appropriate for this Qt version
    const QString windowsVersion = guessMinimumWindowsVersion(qtEnvironment);
    QString osxVersion, iosVersion, androidVersion;

    if (qtEnvironment.mkspecPath.contains("macx")) {
        profile.setValue(settingsTemplate.arg("frameworkBuild"), qtEnvironment.frameworkBuild);
        if (qtEnvironment.qtMajorVersion >= 5) {
            osxVersion = QLatin1String("10.6");
        } else if (qtEnvironment.qtMajorVersion == 4 && qtEnvironment.qtMinorVersion >= 6) {
            QDir qconfigDir;
            if (qtEnvironment.frameworkBuild) {
                qconfigDir.setPath(qtEnvironment.libraryPath);
                qconfigDir.cd("QtCore.framework/Headers");
            } else {
                qconfigDir.setPath(qtEnvironment.includePath);
                qconfigDir.cd("Qt");
            }
            QFile qconfig(qconfigDir.absoluteFilePath("qconfig.h"));
            if (qconfig.open(QIODevice::ReadOnly)) {
                bool qtCocoaBuild = false;
                QTextStream ts(&qconfig);
                QString line;
                do {
                    line = ts.readLine();
                    if (QRegExp(QLatin1String("\\s*#define\\s+QT_MAC_USE_COCOA\\s+1\\s*"),
                                Qt::CaseSensitive).exactMatch(line)) {
                        qtCocoaBuild = true;
                        break;
                    }
                } while (!line.isNull());

                if (ts.status() == QTextStream::Ok)
                    osxVersion = qtCocoaBuild ? QLatin1String("10.5") : QLatin1String("10.4");
            }

            if (osxVersion.isEmpty()) {
                throw ErrorInfo(Internal::Tr::tr("Error reading qconfig.h; could not determine "
                                                 "whether Qt is using Cocoa or Carbon"));
            }
        }

        if (qtEnvironment.configItems.contains("c++11"))
            osxVersion = QLatin1String("10.7");
    }

    if (qtEnvironment.mkspecPath.contains("ios") && qtEnvironment.qtMajorVersion >= 5)
        iosVersion = QLatin1String("5.0");

    if (qtEnvironment.mkspecPath.contains("android")) {
        if (qtEnvironment.qtMajorVersion >= 5)
            androidVersion = QLatin1String("2.3");
        else if (qtEnvironment.qtMajorVersion == 4 && qtEnvironment.qtMinorVersion >= 8)
            androidVersion = QLatin1String("1.6"); // Necessitas
    }

    // ### TODO: wince, winphone, blackberry

    if (!windowsVersion.isEmpty())
        profile.setValue(QLatin1String("cpp.minimumWindowsVersion"), windowsVersion);

    if (!osxVersion.isEmpty())
        profile.setValue(QLatin1String("cpp.minimumOsxVersion"), osxVersion);

    if (!iosVersion.isEmpty())
        profile.setValue(QLatin1String("cpp.minimumIosVersion"), iosVersion);

    if (!androidVersion.isEmpty())
        profile.setValue(QLatin1String("cpp.minimumAndroidVersion"), androidVersion);

    createModules(profile, settings, qtEnvironment);
}

ErrorInfo setupQtProfile(const QString &profileName, Settings *settings,
                         const QtEnvironment &qtEnvironment)
{
    try {
        doSetupQtProfile(profileName, settings, qtEnvironment);
        return ErrorInfo();
    } catch (const ErrorInfo &e) {
        return e;
    }
}

} // namespace qbs
