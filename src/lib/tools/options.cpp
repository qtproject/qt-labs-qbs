/****************************************************************************
**
** Copyright (C) 2012 Digia Plc and/or its subsidiary(-ies).
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

#include "options.h"

#include <tools/error.h>
#include <tools/fileinfo.h>
#include <tools/logger.h>
#include <tools/platform.h>
#include <tools/hostosinfo.h>

#include <QCoreApplication>
#include <QDir>
#include <QTextStream>
#include <QThread>

#include <cstdio>

namespace qbs {

static QString logLevelToString(LoggerLevel level)
{
    switch (level) {
    case LoggerError: return QLatin1String("error");
    case LoggerWarning: return QLatin1String("warning");
    case LoggerInfo: return QLatin1String("info");
    case LoggerDebug: return QLatin1String("debug");
    case LoggerTrace: return QLatin1String("trace");
    default: qFatal("%s: Missing case in switch statement", Q_FUNC_INFO);
    }
    return QString(); // Never reached.
}

static LoggerLevel logLevelFromString(const QString &logLevelString)
{
    const QList<LoggerLevel> levels = QList<LoggerLevel>() << LoggerError << LoggerWarning
            << LoggerInfo << LoggerDebug << LoggerTrace;
    foreach (LoggerLevel l, levels) {
        if (logLevelToString(l) == logLevelString)
            return l;
    }
    throw Error(CommandLineOptions::tr("Invalid log level '%1'.").arg(logLevelString));
}

static QStringList allLogLevelStrings()
{
    return QStringList() << logLevelToString(LoggerError) << logLevelToString(LoggerWarning)
            << logLevelToString(LoggerInfo) << logLevelToString(LoggerDebug)
            << logLevelToString(LoggerTrace);
}

CommandLineOptions::CommandLineOptions()
{
    m_settings = Settings::create();
    m_jobs = m_settings->value("preferences/jobs", 0).toInt();
    if (m_jobs <= 0)
        m_jobs = QThread::idealThreadCount();
}

void CommandLineOptions::printHelp() const
{
    QTextStream stream(m_help ? stdout : stderr);

    stream << "qbs " QBS_VERSION "\n";
    stream << tr("Usage: qbs [command] [options]\n"
         "\nCommands:\n"
         "  build  [variant] [property:value]\n"
         "                     Build project (default command).\n"
         "  clean  ........... Remove all generated files.\n"
         "  properties [variant] [property:value]\n"
         "                     Show all calculated properties for a build.\n"
         "  shell  ........... Open a shell.\n"
         "  status [variant] [property:value]\n"
         "                     List files that are (un)tracked by the project.\n"
         "  run target [variant] [property:value] -- <args>\n"
         "                     Run the specified target.\n"
         "  config\n"
         "                     Set or get project/global option(s).\n"
         "\nGeneral options:\n"
         "  -h -? --help  .... Show this help.\n"
         "  -d  .............. Dump the build graph.\n"
         "  -f <file>  ....... Specify the .qbp project file.\n"
         "  -v  .............. Be more verbose. Increases the log level by one.\n"
         "  -q  .............. Be more quiet. Decreases the log level by one.\n"
         "\nBuild options:\n"
         "  -j <n>  .......... Use <n> jobs (default is the number of cores).\n"
         "  -k  .............. Keep going (ignore errors).\n"
         "  -n  .............. Dry run.\n"
         "  --changed-files file[,file...]\n"
         "      .............. Assume these and only these files have changed.\n"
         "  --products name[,name...]\n"
         "      .............. Build only the specified products.\n"
         "  --log-level level\n"
         "      .............. Use the specified log level. Possible values are \"%1\".\n"
         "                     The default is \"%2\".\n")
             .arg(allLogLevelStrings().join(QLatin1String("\", \"")),
                  logLevelToString(Logger::defaultLevel()));
}

/**
 * Finds automatically the project file (.qbs) in the search directory.
 * If there's more than one project file found then this function returns false.
 * A project file can explicitely be given by the -f option.
 */
bool CommandLineOptions::parseCommandLine(const QStringList &args)
{
    m_commandLine = args;
    try {
        doParse();
    } catch (const Error &error) {
        qbsError(qPrintable(tr("Invalid command line: %1").arg(error.toString())));
        return false;
    }
    return true;
}

void CommandLineOptions::doParse()
{
    m_command = BuildCommand;
    m_projectFileName.clear();
    m_changedFiles.clear();
    m_selectedProductNames.clear();
    m_dryRun = false;
    m_dumpGraph = false;
    m_help = false;
    m_clean = false;
    m_keepGoing = false;
    m_logLevel = Logger::defaultLevel();

    while (!m_commandLine.isEmpty()) {
        const QString arg = m_commandLine.takeFirst();
        if (arg.isEmpty())
            throw Error(tr("Empty arguments not allowed."));
        if (arg.startsWith(QLatin1String("--")))
            parseLongOption(arg);
        else if (arg.at(0) == QLatin1Char('-'))
            parseShortOptions(arg);
        else
            parseArgument(arg);
    }

    if (m_logLevel < LoggerMinLevel) {
        qbsWarning() << tr("Cannot decrease log level as much as specified; using \"%1\".")
                .arg(logLevelToString(LoggerMinLevel));
        m_logLevel = LoggerMinLevel;
    } else if (m_logLevel > LoggerMaxLevel) {
        qbsWarning() << tr("Cannot increase log level as much as specified; using \"%1\".")
                .arg(logLevelToString(LoggerMaxLevel));
        m_logLevel = LoggerMaxLevel;
    }
    Logger::instance().setLevel(m_logLevel);

    // automatically detect the project file name
    if (m_projectFileName.isEmpty())
        m_projectFileName = guessProjectFileName();

    // make the project file name absolute
    if (!m_projectFileName.isEmpty() && !FileInfo::isAbsolute(m_projectFileName)) {
        m_projectFileName = FileInfo::resolvePath(QDir::currentPath(), m_projectFileName);
        m_projectFileName = QDir::cleanPath(m_projectFileName);
    }
}

void CommandLineOptions::parseLongOption(const QString &option)
{
    const QString optionName = option.mid(2);
    if (optionName.isEmpty()) {
        if (m_command != RunCommand)
            throw Error(tr("Argument '--' only allowed in run mode."));
        m_runArgs = m_commandLine;
        m_commandLine.clear();
    }
    else if (optionName == QLatin1String("help")) {
        m_help = true;
        m_commandLine.clear();
    } else if (optionName == QLatin1String("changed-files") && m_command == BuildCommand) {
        m_changedFiles = getOptionArgumentAsList(option);
    } else if (optionName == QLatin1String("products") && (m_command == BuildCommand
            || m_command == CleanCommand || m_command == PropertiesCommand)) {
        m_selectedProductNames = getOptionArgumentAsList(option);
    } else if (optionName == QLatin1String("log-level")) {
        m_logLevel = logLevelFromString(getOptionArgument(option));
    } else {
        throw Error(tr("Unknown option '%1'.").arg(option));
    }
}

void CommandLineOptions::parseShortOptions(const QString &options)
{
    for (int i = 1; i < options.count(); ++i) {
        const char option = options.at(i).toLower().toLatin1();
        switch (option) {
        case '?':
        case 'h':
            m_help = true;
            m_commandLine.clear();
            return;
        case 'j': {
            const QString jobCountString = getShortOptionArgument(options, i);
            bool stringOk;
            m_jobs = jobCountString.toInt(&stringOk);
            if (!stringOk || m_jobs <= 0)
                throw Error(tr("Invalid job count '%1'.").arg(jobCountString));
            break;
        }
        case 'v':
            ++m_logLevel;
            break;
        case 'q':
            --m_logLevel;
            break;
        case 'd':
            m_dumpGraph = true;
            break;
        case 'f':
            m_projectFileName = QDir::fromNativeSeparators(getShortOptionArgument(options, i));
            setRealProjectFile();
            break;
        case 'k':
            m_keepGoing = true;
            break;
        case 'n':
            m_dryRun = true;
            break;
        default:
            throw Error(tr("Unknown option '-%1'.").arg(option));
        }
    }
}

QString CommandLineOptions::getShortOptionArgument(const QString &options, int optionPos)
{
    const QString option = QLatin1Char('-') + options.at(optionPos);
    if (optionPos < options.count() - 1)
        throw Error(tr("Option '%1' needs an argument.").arg(option));
    return getOptionArgument(option);
}

QString CommandLineOptions::getOptionArgument(const QString &option)
{
    if (m_commandLine.isEmpty())
        throw Error(tr("Option '%1' needs an argument.").arg(option));
    return m_commandLine.takeFirst();
}

QStringList CommandLineOptions::getOptionArgumentAsList(const QString &option)
{
    if (m_commandLine.isEmpty())
        throw Error(tr("Option '%1' expects an argument.").arg(option));
    const QStringList list = m_commandLine.takeFirst().split(QLatin1Char(','));
    if (list.isEmpty())
        throw Error(tr("Argument list for option '%1' must not be empty.").arg(option));
    foreach (const QString &element, list) {
        if (element.isEmpty()) {
            throw Error(tr("Argument list for option '%1' must not contain empty elements.")
                    .arg(option));
        }
    }
    return list;
}

void CommandLineOptions::parseArgument(const QString &arg)
{
    if (arg == QLatin1String("build")) {
        m_command = BuildCommand;
    } else if (arg == QLatin1String("clean")) {
        m_command = CleanCommand;
    } else if (arg == QLatin1String("shell")) {
        m_command = StartShellCommand;
    } else if (arg == QLatin1String("run")) {
        m_command = RunCommand;
    } else if (m_command == RunCommand && m_runTargetName.isEmpty()) {
        m_runTargetName = arg;
    } else if (arg == QLatin1String("status")) {
        m_command = StatusCommand;
    } else if (arg == QLatin1String("properties")) {
        m_command = PropertiesCommand;
    } else {
        m_positional.append(arg);
    }
}

QString CommandLineOptions::propertyName(const QString &aCommandLineName) const
{
    // Make fully-qualified, ie "platform" -> "qbs.platform"
    if (aCommandLineName.contains("."))
        return aCommandLineName;
    else
        return "qbs." + aCommandLineName;
}

void CommandLineOptions::setRealProjectFile()
{
    const QFileInfo projectFileInfo(m_projectFileName);
    if (!projectFileInfo.exists())
        throw Error(tr("Project file '%1' cannot be found.").arg(m_projectFileName));
    if (projectFileInfo.isFile())
        return;
    if (!projectFileInfo.isDir())
        throw Error(tr("Project file '%1' has invalid type.").arg(m_projectFileName));
    const QStringList namePatterns = QStringList(QLatin1String("*.qbp"));
    const QStringList &actualFileNames
            = QDir(m_projectFileName).entryList(namePatterns, QDir::Files);
    if (actualFileNames.isEmpty())
        throw Error(tr("No project file found in directory '%1'.").arg(m_projectFileName));
    if (actualFileNames.count() > 1) {
        throw Error(tr("More than one project file found in directory '%1'.")
                .arg(m_projectFileName));
    }
    m_projectFileName.append(QLatin1Char('/')).append(actualFileNames.first());
}

QList<QVariantMap> CommandLineOptions::buildConfigurations() const
{
    // Key: build variant, value: properties. Empty key used for global properties.
    typedef QMap<QString, QVariantMap> PropertyMaps;
    PropertyMaps propertyMaps;

    const QString buildVariantKey = QLatin1String("qbs.buildVariant");
    QString currentKey = QString();
    QVariantMap currentProperties;
    QStringList args = m_positional;
    while (!args.isEmpty()) {
        const QString arg = args.takeFirst();
        const int sepPos = arg.indexOf(QLatin1Char(':'));
        if (sepPos == -1) { // New build variant found.
            propertyMaps.insert(currentKey, currentProperties);
            currentKey = arg;
            currentProperties.clear();
            continue;
        }
        const QString property = propertyName(arg.left(sepPos));
        if (property.isEmpty())
            qbsWarning() << tr("Ignoring empty property.");
        else if (property == buildVariantKey)
            qbsWarning() << tr("Refusing to overwrite special property '%1'.").arg(buildVariantKey);
        else
            currentProperties.insert(property, arg.mid(sepPos + 1));
    }
    propertyMaps.insert(currentKey, currentProperties);

    if (propertyMaps.count() == 1) // No build variant specified on command line.
        propertyMaps.insert(m_settings->buildVariant(), QVariantMap());

    const PropertyMaps::Iterator globalMapIt = propertyMaps.find(QString());
    Q_ASSERT(globalMapIt != propertyMaps.end());
    const QVariantMap globalProperties = globalMapIt.value();
    propertyMaps.erase(globalMapIt);

    QList<QVariantMap> buildConfigs;
    for (PropertyMaps::ConstIterator mapsIt = propertyMaps.constBegin();
             mapsIt != propertyMaps.constEnd(); ++mapsIt) {
        QVariantMap properties = mapsIt.value();
        for (QVariantMap::ConstIterator globalPropIt = globalProperties.constBegin();
                 globalPropIt != globalProperties.constEnd(); ++globalPropIt) {
            properties.insert(globalPropIt.key(), globalPropIt.value());
        }
        properties.insert(buildVariantKey, mapsIt.key());
        buildConfigs << properties;
    }

    return buildConfigs;
}

QString CommandLineOptions::guessProjectFileName()
{
    QDir searchDir = QDir::current();
    for (;;) {
        QStringList projectFiles = searchDir.entryList(QStringList() << "*.qbp", QDir::Files);
        if (projectFiles.count() == 1) {
            QDir::setCurrent(searchDir.path());
            return searchDir.absoluteFilePath(projectFiles.first());
        } else if (projectFiles.count() > 1) {
            throw Error(tr("Multiple project files found in '%1'.\n"
                    "Please specify the correct project file using the -f option.")
                    .arg(QDir::toNativeSeparators(searchDir.absolutePath())));
        }
        if (!searchDir.cdUp())
            break;
    }
    return QString();
}

} // namespace qbs
