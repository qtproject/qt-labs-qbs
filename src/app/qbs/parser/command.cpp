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
#include "command.h"

#include "commandlineoption.h"
#include "commandlineoptionpool.h"

#include <logging/translator.h>
#include <tools/error.h>
#include <tools/hostosinfo.h>

#include <QSet>

namespace qbs {
using namespace Internal;

Command::~Command()
{
}

void Command::parse(QStringList &input)
{
    parseOptions(input);
    parseMore(input);
    if (!input.isEmpty()) {
        throw Error(Tr::tr("Invalid use of command '%1': Extraneous input '%2'.\nUsage: %3")
                    .arg(representation(), input.join(QLatin1String(" ")), longDescription()));
    }
}

void Command::addAllToAdditionalArguments(QStringList &input)
{
    while (!input.isEmpty())
        addOneToAdditionalArguments(input.takeFirst());
}

// TODO: Stricter checking for build variants and properties.
void Command::addOneToAdditionalArguments(const QString &argument)
{
    if (argument.startsWith(QLatin1Char('-'))) {
        throw Error(Tr::tr("Invalid use of command '%1': Encountered option '%2', expected a "
                           "build variant or property.\nUsage: %3")
                           .arg(representation(), argument, longDescription()));
    }
    m_additionalArguments << argument;
}

QList<CommandLineOption::Type> Command::actualSupportedOptions() const
{
    QList<CommandLineOption::Type> options = supportedOptions();
    if (!HostOsInfo::isAnyUnixHost())
        options.removeOne(CommandLineOption::ShowProgressOptionType);
    return options;
}

void Command::parseOptions(QStringList &input)
{
    QSet<CommandLineOption *> usedOptions;
    while (!input.isEmpty()) {
        const QString optionString = input.first();
        if (!optionString.startsWith(QLatin1Char('-')))
            break;
        if (optionString == QLatin1String("--"))
            break;
        input.removeFirst();
        if (optionString.count() == 1) {
            throw Error(Tr::tr("Invalid use of command '%1': Empty options are not allowed.\n"
                               "Usage: %2").arg(representation(), longDescription()));
        }

        // Split up grouped short options.
        if (optionString.at(1) != QLatin1Char('-') && optionString.count() > 2) {
            for (int i = optionString.count(); --i > 0;)
                input.prepend(QLatin1Char('-') + optionString.at(i));
            continue;
        }

        bool matchFound = false;
        foreach (const CommandLineOption::Type optionType, actualSupportedOptions()) {
            CommandLineOption * const option = optionPool().getOption(optionType);
            if (option->shortRepresentation() != optionString
                    && option->longRepresentation() != optionString) {
                continue;
            }
            if (usedOptions.contains(option) && !option->canAppearMoreThanOnce()) {
                throw Error(Tr::tr("Invalid use of command '%1': Option '%2' cannot appear "
                                   "more than once.\nUsage: %3")
                            .arg(representation(), optionString, longDescription()));
            }
            option->parse(type(), optionString, input);
            usedOptions << option;
            matchFound = true;
            break;
        }
        if (!matchFound) {
            throw Error(Tr::tr("Invalid use of command '%1': Unknown option '%2'.\nUsage: %3")
                        .arg(representation(), optionString, longDescription()));
        }
    }
}

QString Command::supportedOptionsDescription() const
{
    QString s = Tr::tr("The possible options are:\n");
    foreach (const CommandLineOption::Type opType, actualSupportedOptions()) {
        const CommandLineOption * const option = optionPool().getOption(opType);
        s += option->description(type());
    }
    return s;
}

void Command::parseMore(QStringList &input)
{
    addAllToAdditionalArguments(input);
}

QString BuildCommand::shortDescription() const
{
    return Tr::tr("Build (parts of) a number of projects. This is the default command.");
}

QString BuildCommand::longDescription() const
{
    QString description = Tr::tr("qbs %1 [options] [[variant] [property:value] ...] ...\n")
            .arg(representation());
    description += Tr::tr("Builds a project in one or more configuration(s).\n");
    return description += supportedOptionsDescription();
}

static QString buildCommandRepresentation() { return QLatin1String("build"); }

QString BuildCommand::representation() const
{
    return buildCommandRepresentation();
}

QList<CommandLineOption::Type> BuildCommand::supportedOptions() const
{
    return QList<CommandLineOption::Type>()
            << CommandLineOption::FileOptionType
            << CommandLineOption::LogLevelOptionType
            << CommandLineOption::VerboseOptionType
            << CommandLineOption::QuietOptionType
            << CommandLineOption::ShowProgressOptionType
            << CommandLineOption::JobsOptionType
            << CommandLineOption::KeepGoingOptionType
            << CommandLineOption::DryRunOptionType
            << CommandLineOption::ProductsOptionType
            << CommandLineOption::ChangedFilesOptionType;
}

QString CleanCommand::shortDescription() const
{
    return Tr::tr("Remove the files generated during a build.");
}

QString CleanCommand::longDescription() const
{
    QString description = Tr::tr("qbs %1 [options] [[variant] [property:value] ...] ...\n");
    description += Tr::tr("Removes build artifacts for the given project and configuration(s).\n");
    return description += supportedOptionsDescription();
}

QString CleanCommand::representation() const
{
    return QLatin1String("clean");
}

QList<CommandLineOption::Type> CleanCommand::supportedOptions() const
{
    return QList<CommandLineOption::Type>()
            << CommandLineOption::FileOptionType
            << CommandLineOption::LogLevelOptionType
            << CommandLineOption::VerboseOptionType
            << CommandLineOption::QuietOptionType
            << CommandLineOption::ShowProgressOptionType
            << CommandLineOption::KeepGoingOptionType
            << CommandLineOption::DryRunOptionType
            << CommandLineOption::ProductsOptionType
            << CommandLineOption::AllArtifactsOptionType;
}

QString RunCommand::shortDescription() const
{
    return QLatin1String("Run an executable generated by building a project.");
}

QString RunCommand::longDescription() const
{
    QString description = Tr::tr("qbs %1 [options] [variant] [property:value] ... "
                                 "[ -- <arguments>]\n").arg(representation());
    description += Tr::tr("Run the specified product's executable with the specified arguments.\n");
    description += Tr::tr("If the project has only one product, the '%1' option may be omitted.\n")
            .arg(optionPool().productsOption()->longRepresentation());
    description += Tr::tr("The product will be built if it is not up to date; "
                          "see the '%2' command.\n").arg(buildCommandRepresentation());
    return description += supportedOptionsDescription();
}

QString RunCommand::representation() const
{
    return QLatin1String("run");
}

QList<CommandLineOption::Type> RunCommand::supportedOptions() const
{
    return QList<CommandLineOption::Type>()
            << CommandLineOption::FileOptionType
            << CommandLineOption::LogLevelOptionType
            << CommandLineOption::VerboseOptionType
            << CommandLineOption::QuietOptionType
            << CommandLineOption::ShowProgressOptionType
            << CommandLineOption::JobsOptionType
            << CommandLineOption::KeepGoingOptionType
            << CommandLineOption::DryRunOptionType
            << CommandLineOption::ProductsOptionType
            << CommandLineOption::ChangedFilesOptionType;
}

void RunCommand::parseMore(QStringList &input)
{
    // Build variants and properties
    while (!input.isEmpty()) {
        const QString arg = input.takeFirst();
        if (arg == QLatin1String("--"))
            break;
        addOneToAdditionalArguments(arg);
    }

    m_targetParameters = input;
    input.clear();
}

QString ShellCommand::shortDescription() const
{
    return Tr::tr("Open a shell with a product's environment.");
}

QString ShellCommand::longDescription() const
{
    QString description = Tr::tr("qbs %1 [options] [variant] [property:value] ...\n")
            .arg(representation());
    description += Tr::tr("Opens a shell in the same environment that a build with the given "
                          "parameters would use.\n");
    const ProductsOption * const option = optionPool().productsOption();
    description += Tr::tr("The '%1' option may be omitted if and only if the project has "
                          "exactly one product.").arg(option->longRepresentation());
    return description += supportedOptionsDescription();
}

QString ShellCommand::representation() const
{
    return QLatin1String("shell");
}

QList<CommandLineOption::Type> ShellCommand::supportedOptions() const
{
    return QList<CommandLineOption::Type>()
            << CommandLineOption::FileOptionType
            << CommandLineOption::LogLevelOptionType
            << CommandLineOption::VerboseOptionType
            << CommandLineOption::QuietOptionType
            << CommandLineOption::ShowProgressOptionType
            << CommandLineOption::ProductsOptionType;
}

QString PropertiesCommand::shortDescription() const
{
    return Tr::tr("Show the project properties for a configuration.");
}

QString PropertiesCommand::longDescription() const
{
    QString description = Tr::tr("qbs %1 [options] [variant] [property:value] ...\n")
            .arg(representation());
    description += Tr::tr("Shows all properties of the project in the given configuration.\n");
    return description += supportedOptionsDescription();
}

QString PropertiesCommand::representation() const
{
    return QLatin1String("properties");
}

QList<CommandLineOption::Type> PropertiesCommand::supportedOptions() const
{
    return QList<CommandLineOption::Type>()
            << CommandLineOption::FileOptionType
            << CommandLineOption::LogLevelOptionType
            << CommandLineOption::VerboseOptionType
            << CommandLineOption::QuietOptionType
            << CommandLineOption::ShowProgressOptionType
            << CommandLineOption::ProductsOptionType;
}

QString StatusCommand::shortDescription() const
{
    return Tr::tr("Show the status of files in the project directory.");
}

QString StatusCommand::longDescription() const
{
    QString description = Tr::tr("qbs %1 [options] [variant] [property:value] ...\n")
            .arg(representation());
    description += Tr::tr("Lists all the files in the project directory and shows whether "
                          "they are known to qbs in the respective configuration.\n");
    return description += supportedOptionsDescription();
}

QString StatusCommand::representation() const
{
    return QLatin1String("status");
}

QList<CommandLineOption::Type> StatusCommand::supportedOptions() const
{
    return QList<CommandLineOption::Type>()
            << CommandLineOption::FileOptionType
            << CommandLineOption::LogLevelOptionType
            << CommandLineOption::VerboseOptionType
            << CommandLineOption::QuietOptionType
            << CommandLineOption::ShowProgressOptionType;
}

QString UpdateTimestampsCommand::shortDescription() const
{
    return Tr::tr("Mark the build as up to date.");
}

QString UpdateTimestampsCommand::longDescription() const
{
    QString description = Tr::tr("qbs %1 [options] [[variant] [property:value] ...] ...\n")
            .arg(representation());
    return description += Tr::tr("Update the timestamps of all build artifacts, causing the next "
            "builds of the project to do nothing if no updates to source files happen in between.\n"
            "This functionality is useful if you know that the current changes to source files "
            "are irrelevant to the build.\n"
            "NOTE: Doing this causes a discrepancy between the \"real world\" and the information "
            "in the build graph, so use with care.\n");
}

QString UpdateTimestampsCommand::representation() const
{
    return QLatin1String("update-timestamps");
}

QList<CommandLineOption::Type> UpdateTimestampsCommand::supportedOptions() const
{
    return QList<CommandLineOption::Type>()
            << CommandLineOption::FileOptionType
            << CommandLineOption::LogLevelOptionType
            << CommandLineOption::VerboseOptionType
            << CommandLineOption::QuietOptionType
            << CommandLineOption::ProductsOptionType;
}

QString HelpCommand::shortDescription() const
{
    return Tr::tr("Show general or command-specific help.");
}

QString HelpCommand::longDescription() const
{
    QString description = Tr::tr("qbs %1 [<command>]\n").arg(representation());
    return description += Tr::tr("Shows either the general help or a description of "
                                 "the given command.\n");
}

QString HelpCommand::representation() const
{
    return QLatin1String("help");
}

QList<CommandLineOption::Type> HelpCommand::supportedOptions() const
{
    return QList<CommandLineOption::Type>();
}

void HelpCommand::parseMore(QStringList &input)
{
    if (input.isEmpty())
        return;
    if (input.count() > 1) {
        throw Error(Tr::tr("Invalid use of command '%1': Cannot describe more than one command.\n"
                           "Usage: %2").arg(representation(), longDescription()));
    }
    m_command = input.takeFirst();
    Q_ASSERT(input.isEmpty());
}

} // namespace qbs
