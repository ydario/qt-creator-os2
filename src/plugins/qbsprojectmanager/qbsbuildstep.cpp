/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "qbsbuildstep.h"

#include "qbsbuildconfiguration.h"
#include "qbsparser.h"
#include "qbsproject.h"
#include "qbsprojectmanagerconstants.h"
#include "qbsprojectmanagersettings.h"

#include <coreplugin/icore.h>
#include <coreplugin/variablechooser.h>
#include <projectexplorer/buildsteplist.h>
#include <projectexplorer/kit.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/target.h>
#include <qtsupport/qtversionmanager.h>
#include <utils/macroexpander.h>
#include <utils/pathchooser.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>
#include <utils/utilsicons.h>

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSpinBox>

#include <qbs.h>

// --------------------------------------------------------------------
// Constants:
// --------------------------------------------------------------------

const char QBS_CONFIG[] = "Qbs.Configuration";
const char QBS_DRY_RUN[] = "Qbs.DryRun";
const char QBS_KEEP_GOING[] = "Qbs.DryKeepGoing";
const char QBS_MAXJOBCOUNT[] = "Qbs.MaxJobs";
const char QBS_SHOWCOMMANDLINES[] = "Qbs.ShowCommandLines";
const char QBS_INSTALL[] = "Qbs.Install";
const char QBS_CLEAN_INSTALL_ROOT[] = "Qbs.CleanInstallRoot";

using namespace ProjectExplorer;
using namespace Utils;

namespace QbsProjectManager {
namespace Internal {

class QbsBuildStepConfigWidget : public ProjectExplorer::BuildStepConfigWidget
{
    Q_OBJECT
public:
    QbsBuildStepConfigWidget(QbsBuildStep *step);

private:
    void updateState();
    void updateQmlDebuggingOption();
    void updatePropertyEdit(const QVariantMap &data);

    void changeBuildVariant(int);
    void changeShowCommandLines(bool show);
    void changeKeepGoing(bool kg);
    void changeJobCount(int count);
    void changeInstall(bool install);
    void changeCleanInstallRoot(bool clean);
    void changeUseDefaultInstallDir(bool useDefault);
    void changeInstallDir(const QString &dir);
    void changeForceProbes(bool forceProbes);
    void applyCachedProperties();

    QbsBuildStep *qbsStep() const;

    // QML debugging:
    void linkQmlDebuggingLibraryChecked(bool checked);

    bool validateProperties(Utils::FancyLineEdit *edit, QString *errorMessage);

    class Property
    {
    public:
        Property() = default;
        Property(const QString &n, const QString &v, const QString &e) :
            name(n), value(v), effectiveValue(e)
        {}
        bool operator==(const Property &other) const
        {
            return name == other.name
                    && value == other.value
                    && effectiveValue == other.effectiveValue;
        }

        QString name;
        QString value;
        QString effectiveValue;
    };

    QList<Property> m_propertyCache;
    bool m_ignoreChange = false;

    QComboBox *buildVariantComboBox;
    QSpinBox *jobSpinBox;
    QCheckBox *qmlDebuggingLibraryCheckBox;
    FancyLineEdit *propertyEdit;
    PathChooser *installDirChooser;
    QLabel *qmlDebuggingWarningIcon;
    QLabel *qmlDebuggingWarningText;
    QCheckBox *keepGoingCheckBox;
    QCheckBox *showCommandLinesCheckBox;
    QCheckBox *forceProbesCheckBox;
    QCheckBox *installCheckBox;
    QCheckBox *cleanInstallRootCheckBox;
    QCheckBox *defaultInstallDirCheckBox;
    QPlainTextEdit *commandLineTextEdit;
};

// --------------------------------------------------------------------
// QbsBuildStep:
// --------------------------------------------------------------------

QbsBuildStep::QbsBuildStep(ProjectExplorer::BuildStepList *bsl) :
    ProjectExplorer::BuildStep(bsl, Constants::QBS_BUILDSTEP_ID),
    m_enableQmlDebugging(QtSupport::BaseQtVersion::isQmlDebuggingSupported(target()->kit()))
{
    setDisplayName(tr("Qbs Build"));
    setQbsConfiguration(QVariantMap());

    auto qbsBuildConfig = qobject_cast<QbsBuildConfiguration *>(buildConfiguration());
    QTC_CHECK(qbsBuildConfig);
    connect(this, &QbsBuildStep::qbsConfigurationChanged,
            qbsBuildConfig, &QbsBuildConfiguration::qbsConfigurationChanged);

//    setQbsConfiguration(other->qbsConfiguration(PreserveVariables));
}

QbsBuildStep::~QbsBuildStep()
{
    doCancel();
    if (m_job) {
        m_job->deleteLater();
        m_job = nullptr;
    }
    delete m_parser;
}

bool QbsBuildStep::init()
{
    if (project()->isParsing() || m_job)
        return false;

    auto bc = static_cast<QbsBuildConfiguration *>(buildConfiguration());

    if (!bc)
        return false;

    delete m_parser;
    m_parser = new Internal::QbsParser;
    ProjectExplorer::IOutputParser *parser = target()->kit()->createOutputParser();
    if (parser)
        m_parser->appendOutputParser(parser);

    m_changedFiles = bc->changedFiles();
    m_activeFileTags = bc->activeFileTags();
    m_products = bc->products();

    connect(m_parser, &ProjectExplorer::IOutputParser::addOutput,
            this, [this](const QString &string, ProjectExplorer::BuildStep::OutputFormat format) {
        emit addOutput(string, format);
    });
    connect(m_parser, &ProjectExplorer::IOutputParser::addTask, this, &QbsBuildStep::addTask);

    return true;
}

void QbsBuildStep::doRun()
{
    // We need a pre-build parsing step in order not to lose project file changes done
    // right before building (but before the delay has elapsed).
    parseProject();
}

ProjectExplorer::BuildStepConfigWidget *QbsBuildStep::createConfigWidget()
{
    return new QbsBuildStepConfigWidget(this);
}

void QbsBuildStep::doCancel()
{
    if (m_parsingProject)
        qbsProject()->cancelParsing();
    else if (m_job)
        m_job->cancel();
}

QVariantMap QbsBuildStep::qbsConfiguration(VariableHandling variableHandling) const
{
    QVariantMap config = m_qbsConfiguration;
    config.insert(Constants::QBS_FORCE_PROBES_KEY, m_forceProbes);
    if (m_enableQmlDebugging)
        config.insert(Constants::QBS_CONFIG_QUICK_DEBUG_KEY, true);
    else
        config.remove(Constants::QBS_CONFIG_QUICK_DEBUG_KEY);
    if (variableHandling == ExpandVariables) {
        const Utils::MacroExpander *expander = Utils::globalMacroExpander();
        for (auto it = config.begin(), end = config.end(); it != end; ++it) {
            const QString rawString = it.value().toString();
            const QString expandedString = expander->expand(rawString);
            it.value() = qbs::representationToSettingsValue(expandedString);
        }
    }
    return config;
}

void QbsBuildStep::setQbsConfiguration(const QVariantMap &config)
{
    auto pro = static_cast<QbsProject *>(project());

    QVariantMap tmp = config;
    tmp.insert(Constants::QBS_CONFIG_PROFILE_KEY, pro->profileForTarget(target()));
    if (!tmp.contains(Constants::QBS_CONFIG_VARIANT_KEY))
        tmp.insert(Constants::QBS_CONFIG_VARIANT_KEY,
                   QString::fromLatin1(Constants::QBS_VARIANT_DEBUG));

    if (tmp == m_qbsConfiguration)
        return;
    m_qbsConfiguration = tmp;
    if (ProjectExplorer::BuildConfiguration *bc = buildConfiguration())
        emit bc->buildTypeChanged();
    emit qbsConfigurationChanged();
}

bool QbsBuildStep::keepGoing() const
{
    return m_qbsBuildOptions.keepGoing();
}

bool QbsBuildStep::showCommandLines() const
{
    return m_qbsBuildOptions.echoMode() == qbs::CommandEchoModeCommandLine;
}

bool QbsBuildStep::install() const
{
    return m_qbsBuildOptions.install();
}

bool QbsBuildStep::cleanInstallRoot() const
{
    return m_qbsBuildOptions.removeExistingInstallation();
}

bool QbsBuildStep::hasCustomInstallRoot() const
{
    return m_qbsConfiguration.contains(Constants::QBS_INSTALL_ROOT_KEY);
}

Utils::FilePath QbsBuildStep::installRoot(VariableHandling variableHandling) const
{
    const QString root =
            qbsConfiguration(variableHandling).value(Constants::QBS_INSTALL_ROOT_KEY).toString();
    if (!root.isNull())
        return Utils::FilePath::fromString(root);

    const QbsBuildConfiguration * const bc
            = static_cast<QbsBuildConfiguration *>(buildConfiguration());
    return bc->buildDirectory().pathAppended(bc->configurationName())
            .pathAppended(qbs::InstallOptions::defaultInstallRoot());
}

int QbsBuildStep::maxJobs() const
{
    if (m_qbsBuildOptions.maxJobCount() > 0)
        return m_qbsBuildOptions.maxJobCount();
    return qbs::BuildOptions::defaultMaxJobCount();
}

static QString forceProbesKey() { return QLatin1String("Qbs.forceProbesKey"); }
static QString enableQmlDebuggingKey() { return QLatin1String("Qbs.enableQmlDebuggingKey"); }

bool QbsBuildStep::fromMap(const QVariantMap &map)
{
    if (!ProjectExplorer::BuildStep::fromMap(map))
        return false;

    setQbsConfiguration(map.value(QBS_CONFIG).toMap());
    m_qbsBuildOptions.setDryRun(map.value(QBS_DRY_RUN).toBool());
    m_qbsBuildOptions.setKeepGoing(map.value(QBS_KEEP_GOING).toBool());
    m_qbsBuildOptions.setMaxJobCount(map.value(QBS_MAXJOBCOUNT).toInt());
    const bool showCommandLines = map.value(QBS_SHOWCOMMANDLINES).toBool();
    m_qbsBuildOptions.setEchoMode(showCommandLines ? qbs::CommandEchoModeCommandLine
                                                   : qbs::CommandEchoModeSummary);
    m_qbsBuildOptions.setInstall(map.value(QBS_INSTALL, true).toBool());
    m_qbsBuildOptions.setRemoveExistingInstallation(map.value(QBS_CLEAN_INSTALL_ROOT)
                                                    .toBool());
    m_forceProbes = map.value(forceProbesKey()).toBool();
    m_enableQmlDebugging = map.value(enableQmlDebuggingKey()).toBool();
    return true;
}

QVariantMap QbsBuildStep::toMap() const
{
    QVariantMap map = ProjectExplorer::BuildStep::toMap();
    map.insert(QBS_CONFIG, m_qbsConfiguration);
    map.insert(QBS_DRY_RUN, m_qbsBuildOptions.dryRun());
    map.insert(QBS_KEEP_GOING, m_qbsBuildOptions.keepGoing());
    map.insert(QBS_MAXJOBCOUNT, m_qbsBuildOptions.maxJobCount());
    map.insert(QBS_SHOWCOMMANDLINES,
               m_qbsBuildOptions.echoMode() == qbs::CommandEchoModeCommandLine);
    map.insert(QBS_INSTALL, m_qbsBuildOptions.install());
    map.insert(QBS_CLEAN_INSTALL_ROOT,
               m_qbsBuildOptions.removeExistingInstallation());
    map.insert(forceProbesKey(), m_forceProbes);
    map.insert(enableQmlDebuggingKey(), m_enableQmlDebugging);
    return map;
}

void QbsBuildStep::buildingDone(bool success)
{
    m_lastWasSuccess = success;
    // Report errors:
    foreach (const qbs::ErrorItem &item, m_job->error().items())
        createTaskAndOutput(ProjectExplorer::Task::Error, item.description(),
                            item.codeLocation().filePath(), item.codeLocation().line());

    auto pro = static_cast<QbsProject *>(project());

    // Building can uncover additional target artifacts.
    pro->updateAfterBuild();

    // The reparsing, if it is necessary, has to be done before finished() is emitted, as
    // otherwise a potential additional build step could conflict with the parsing step.
    if (pro->parsingScheduled())
        parseProject();
    else
        finish();
}

void QbsBuildStep::reparsingDone(bool success)
{
    disconnect(project(), &Project::parsingFinished, this, &QbsBuildStep::reparsingDone);
    m_parsingProject = false;
    if (m_job) { // This was a scheduled reparsing after building.
        finish();
    } else if (!success) {
        m_lastWasSuccess = false;
        finish();
    } else {
        build();
    }
}

void QbsBuildStep::handleTaskStarted(const QString &desciption, int max)
{
    m_currentTask = desciption;
    m_maxProgress = max;
}

void QbsBuildStep::handleProgress(int value)
{
    if (m_maxProgress > 0)
        emit progress(value * 100 / m_maxProgress, m_currentTask);
}

void QbsBuildStep::handleCommandDescriptionReport(const QString &highlight, const QString &message)
{
    Q_UNUSED(highlight)
    emit addOutput(message, OutputFormat::Stdout);
}

void QbsBuildStep::handleProcessResultReport(const qbs::ProcessResult &result)
{
    bool hasOutput = !result.stdOut().isEmpty() || !result.stdErr().isEmpty();

    if (result.success() && !hasOutput)
        return;

    m_parser->setWorkingDirectory(result.workingDirectory());

    QString commandline = result.executableFilePath() + ' '
            + Utils::QtcProcess::joinArgs(result.arguments());
    emit addOutput(commandline, OutputFormat::Stdout);

    foreach (const QString &line, result.stdErr()) {
        m_parser->stdError(line);
        emit addOutput(line, OutputFormat::Stderr);
    }
    foreach (const QString &line, result.stdOut()) {
        m_parser->stdOutput(line);
        emit addOutput(line, OutputFormat::Stdout);
    }
    m_parser->flush();
}

void QbsBuildStep::createTaskAndOutput(ProjectExplorer::Task::TaskType type, const QString &message,
                                       const QString &file, int line)
{
    ProjectExplorer::Task task = ProjectExplorer::Task(type, message,
                                                       Utils::FilePath::fromString(file), line,
                                                       ProjectExplorer::Constants::TASK_CATEGORY_COMPILE);
    emit addTask(task, 1);
    emit addOutput(message, OutputFormat::Stdout);
}

QString QbsBuildStep::buildVariant() const
{
    return qbsConfiguration(PreserveVariables).value(Constants::QBS_CONFIG_VARIANT_KEY).toString();
}

void QbsBuildStep::setBuildVariant(const QString &variant)
{
    if (m_qbsConfiguration.value(Constants::QBS_CONFIG_VARIANT_KEY).toString() == variant)
        return;
    m_qbsConfiguration.insert(Constants::QBS_CONFIG_VARIANT_KEY, variant);
    emit qbsConfigurationChanged();
    if (ProjectExplorer::BuildConfiguration *bc = buildConfiguration())
        emit bc->buildTypeChanged();
}

QString QbsBuildStep::profile() const
{
    return qbsConfiguration(PreserveVariables).value(Constants::QBS_CONFIG_PROFILE_KEY).toString();
}

void QbsBuildStep::setKeepGoing(bool kg)
{
    if (m_qbsBuildOptions.keepGoing() == kg)
        return;
    m_qbsBuildOptions.setKeepGoing(kg);
    emit qbsBuildOptionsChanged();
}

void QbsBuildStep::setMaxJobs(int jobcount)
{
    if (m_qbsBuildOptions.maxJobCount() == jobcount)
        return;
    m_qbsBuildOptions.setMaxJobCount(jobcount);
    emit qbsBuildOptionsChanged();
}

void QbsBuildStep::setShowCommandLines(bool show)
{
    if (showCommandLines() == show)
        return;
    m_qbsBuildOptions.setEchoMode(show ? qbs::CommandEchoModeCommandLine
                                       : qbs::CommandEchoModeSummary);
    emit qbsBuildOptionsChanged();
}

void QbsBuildStep::setInstall(bool install)
{
    if (m_qbsBuildOptions.install() == install)
        return;
    m_qbsBuildOptions.setInstall(install);
    emit qbsBuildOptionsChanged();
}

void QbsBuildStep::setCleanInstallRoot(bool clean)
{
    if (m_qbsBuildOptions.removeExistingInstallation() == clean)
        return;
    m_qbsBuildOptions.setRemoveExistingInstallation(clean);
    emit qbsBuildOptionsChanged();
}

void QbsBuildStep::parseProject()
{
    m_parsingProject = true;
    connect(project(), &Project::parsingFinished, this, &QbsBuildStep::reparsingDone);
    qbsProject()->parseCurrentBuildConfiguration();
}

void QbsBuildStep::build()
{
    qbs::BuildOptions options(m_qbsBuildOptions);
    options.setChangedFiles(m_changedFiles);
    options.setFilesToConsider(m_changedFiles);
    options.setActiveFileTags(m_activeFileTags);
    options.setLogElapsedTime(!qEnvironmentVariableIsEmpty(Constants::QBS_PROFILING_ENV));

    QString error;
    m_job = qbsProject()->build(options, m_products, error);
    if (!m_job) {
        emit addOutput(error, OutputFormat::ErrorMessage);
        emit finished(false);
        return;
    }

    m_maxProgress = 0;

    connect(m_job, &qbs::AbstractJob::finished, this, &QbsBuildStep::buildingDone);
    connect(m_job, &qbs::AbstractJob::taskStarted,
            this, &QbsBuildStep::handleTaskStarted);
    connect(m_job, &qbs::AbstractJob::taskProgress,
            this, &QbsBuildStep::handleProgress);
    connect(m_job, &qbs::BuildJob::reportCommandDescription,
            this, &QbsBuildStep::handleCommandDescriptionReport);
    connect(m_job, &qbs::BuildJob::reportProcessResult,
            this, &QbsBuildStep::handleProcessResultReport);

}

void QbsBuildStep::finish()
{
    emit finished(m_lastWasSuccess);
    if (m_job) {
        m_job->deleteLater();
        m_job = nullptr;
    }
}

QbsProject *QbsBuildStep::qbsProject() const
{
    return static_cast<QbsProject *>(project());
}

// --------------------------------------------------------------------
// QbsBuildStepConfigWidget:
// --------------------------------------------------------------------

QbsBuildStepConfigWidget::QbsBuildStepConfigWidget(QbsBuildStep *step) :
    BuildStepConfigWidget(step),
    m_ignoreChange(false)
{
    connect(step, &ProjectConfiguration::displayNameChanged,
            this, &QbsBuildStepConfigWidget::updateState);
    connect(step, &QbsBuildStep::qbsConfigurationChanged,
            this, &QbsBuildStepConfigWidget::updateState);
    connect(step, &QbsBuildStep::qbsBuildOptionsChanged,
            this, &QbsBuildStepConfigWidget::updateState);
    connect(&QbsProjectManagerSettings::instance(), &QbsProjectManagerSettings::settingsBaseChanged,
            this, &QbsBuildStepConfigWidget::updateState);
    connect(step->buildConfiguration(), &BuildConfiguration::buildDirectoryChanged,
            this, &QbsBuildStepConfigWidget::updateState);

    setContentsMargins(0, 0, 0, 0);

    buildVariantComboBox = new QComboBox(this);
    buildVariantComboBox->addItem(tr("Debug"));
    buildVariantComboBox->addItem(tr("Release"));

    QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    sizePolicy.setHorizontalStretch(0);
    sizePolicy.setVerticalStretch(0);
    sizePolicy.setHeightForWidth(buildVariantComboBox->sizePolicy().hasHeightForWidth());
    buildVariantComboBox->setSizePolicy(sizePolicy);

    auto horizontalLayout_5 = new QHBoxLayout();
    horizontalLayout_5->addWidget(buildVariantComboBox);
    horizontalLayout_5->addItem(new QSpacerItem(70, 13, QSizePolicy::Expanding, QSizePolicy::Minimum));

    jobSpinBox = new QSpinBox(this);

    auto horizontalLayout_6 = new QHBoxLayout();
    horizontalLayout_6->addWidget(jobSpinBox);
    horizontalLayout_6->addItem(new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum));

    qmlDebuggingLibraryCheckBox = new QCheckBox(this);
    qmlDebuggingWarningIcon = new QLabel(this);
    qmlDebuggingWarningText = new QLabel(this);

    auto qmlDebuggingLayout = new QHBoxLayout();
    qmlDebuggingLayout->addWidget(qmlDebuggingLibraryCheckBox);
    qmlDebuggingLayout->addWidget(qmlDebuggingWarningIcon);
    qmlDebuggingLayout->addWidget(qmlDebuggingWarningText);
    qmlDebuggingLayout->addItem(new QSpacerItem(40, 5, QSizePolicy::Expanding, QSizePolicy::Minimum));

    propertyEdit = new FancyLineEdit(this);

    keepGoingCheckBox = new QCheckBox(this);

    showCommandLinesCheckBox = new QCheckBox(this);

    forceProbesCheckBox = new QCheckBox(this);

    auto flagsLayout = new QHBoxLayout();
    flagsLayout->addWidget(keepGoingCheckBox);
    flagsLayout->addWidget(showCommandLinesCheckBox);
    flagsLayout->addWidget(forceProbesCheckBox);
    flagsLayout->addItem(new QSpacerItem(40, 13, QSizePolicy::Expanding, QSizePolicy::Minimum));

    installCheckBox = new QCheckBox(this);

    cleanInstallRootCheckBox = new QCheckBox(this);

    defaultInstallDirCheckBox = new QCheckBox(this);

    auto installLayout = new QHBoxLayout();
    installLayout->addWidget(installCheckBox);
    installLayout->addWidget(cleanInstallRootCheckBox);
    installLayout->addWidget(defaultInstallDirCheckBox);
    installLayout->addItem(new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum));

    installDirChooser = new PathChooser(this);
    installDirChooser->setExpectedKind(PathChooser::Directory);

    commandLineTextEdit = new QPlainTextEdit(this);
    commandLineTextEdit->setUndoRedoEnabled(false);
    commandLineTextEdit->setReadOnly(true);
    commandLineTextEdit->setTextInteractionFlags(Qt::TextSelectableByKeyboard|Qt::TextSelectableByMouse);

    auto formLayout = new QFormLayout(this);
    formLayout->addRow(tr("Build variant:"), horizontalLayout_5);
    formLayout->addRow(tr("Parallel jobs:"), horizontalLayout_6);
    formLayout->addRow(tr("Enable QML debugging:"), qmlDebuggingLayout);
    formLayout->addRow(tr("Properties:"), propertyEdit);
    formLayout->addRow(tr("Flags:"), flagsLayout);
    formLayout->addRow(tr("Installation flags:"), installLayout);
    formLayout->addRow(tr("Installation directory:"), installDirChooser);
    formLayout->addRow(tr("Equivalent command line:"), commandLineTextEdit);

    QWidget::setTabOrder(buildVariantComboBox, jobSpinBox);
    QWidget::setTabOrder(jobSpinBox, qmlDebuggingLibraryCheckBox);
    QWidget::setTabOrder(qmlDebuggingLibraryCheckBox, propertyEdit);
    QWidget::setTabOrder(propertyEdit, keepGoingCheckBox);
    QWidget::setTabOrder(keepGoingCheckBox, showCommandLinesCheckBox);
    QWidget::setTabOrder(showCommandLinesCheckBox, forceProbesCheckBox);
    QWidget::setTabOrder(forceProbesCheckBox, installCheckBox);
    QWidget::setTabOrder(installCheckBox, cleanInstallRootCheckBox);
    QWidget::setTabOrder(cleanInstallRootCheckBox, commandLineTextEdit);

    jobSpinBox->setToolTip(tr("Number of concurrent build jobs."));
    propertyEdit->setToolTip(tr("Properties to pass to the project."));
    keepGoingCheckBox->setToolTip(tr("Keep going when errors occur (if at all possible)."));
    keepGoingCheckBox->setText(tr("Keep going"));
    showCommandLinesCheckBox->setText(tr("Show command lines"));
    forceProbesCheckBox->setText(tr("Force probes"));
    installCheckBox->setText(tr("Install"));
    cleanInstallRootCheckBox->setText(tr("Clean install root"));
    defaultInstallDirCheckBox->setText(tr("Use default location"));

    auto chooser = new Core::VariableChooser(this);
    chooser->addSupportedWidget(propertyEdit);
    chooser->addSupportedWidget(installDirChooser->lineEdit());
    propertyEdit->setValidationFunction([this](FancyLineEdit *edit, QString *errorMessage) {
        return validateProperties(edit, errorMessage);
    });

    qmlDebuggingWarningIcon->setPixmap(Utils::Icons::WARNING.pixmap());

    connect(buildVariantComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &QbsBuildStepConfigWidget::changeBuildVariant);
    connect(keepGoingCheckBox, &QAbstractButton::toggled,
            this, &QbsBuildStepConfigWidget::changeKeepGoing);
    connect(jobSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &QbsBuildStepConfigWidget::changeJobCount);
    connect(showCommandLinesCheckBox, &QCheckBox::toggled, this,
            &QbsBuildStepConfigWidget::changeShowCommandLines);
    connect(installCheckBox, &QCheckBox::toggled, this,
            &QbsBuildStepConfigWidget::changeInstall);
    connect(cleanInstallRootCheckBox, &QCheckBox::toggled, this,
            &QbsBuildStepConfigWidget::changeCleanInstallRoot);
    connect(defaultInstallDirCheckBox, &QCheckBox::toggled, this,
            &QbsBuildStepConfigWidget::changeUseDefaultInstallDir);
    connect(installDirChooser, &Utils::PathChooser::rawPathChanged, this,
            &QbsBuildStepConfigWidget::changeInstallDir);
    connect(forceProbesCheckBox, &QCheckBox::toggled, this,
            &QbsBuildStepConfigWidget::changeForceProbes);
    connect(qmlDebuggingLibraryCheckBox, &QAbstractButton::toggled,
            this, &QbsBuildStepConfigWidget::linkQmlDebuggingLibraryChecked);
    updateState();
}

void QbsBuildStepConfigWidget::updateState()
{
    if (!m_ignoreChange) {
        keepGoingCheckBox->setChecked(qbsStep()->keepGoing());
        jobSpinBox->setValue(qbsStep()->maxJobs());
        showCommandLinesCheckBox->setChecked(qbsStep()->showCommandLines());
        installCheckBox->setChecked(qbsStep()->install());
        cleanInstallRootCheckBox->setChecked(qbsStep()->cleanInstallRoot());
        forceProbesCheckBox->setChecked(qbsStep()->forceProbes());
        updatePropertyEdit(qbsStep()->qbsConfiguration(QbsBuildStep::PreserveVariables));
        qmlDebuggingLibraryCheckBox->setChecked(qbsStep()->isQmlDebuggingEnabled());
        installDirChooser->setFileName(qbsStep()->installRoot(QbsBuildStep::PreserveVariables));
        defaultInstallDirCheckBox->setChecked(!qbsStep()->hasCustomInstallRoot());
    }

    updateQmlDebuggingOption();

    const QString buildVariant = qbsStep()->buildVariant();
    const int idx = (buildVariant == Constants::QBS_VARIANT_DEBUG) ? 0 : 1;
    buildVariantComboBox->setCurrentIndex(idx);
    QString command = static_cast<QbsBuildConfiguration *>(step()->buildConfiguration())
            ->equivalentCommandLine(qbsStep());

    for (int i = 0; i < m_propertyCache.count(); ++i) {
        command += ' ' + m_propertyCache.at(i).name + ':' + m_propertyCache.at(i).effectiveValue;
    }

    if (qbsStep()->isQmlDebuggingEnabled())
        command.append(' ').append(Constants::QBS_CONFIG_QUICK_DEBUG_KEY).append(":true");
    commandLineTextEdit->setPlainText(command);

    setSummaryText(tr("<b>Qbs:</b> %1").arg(command));
}

void QbsBuildStepConfigWidget::updateQmlDebuggingOption()
{
    QString warningText;
    bool supported = QtSupport::BaseQtVersion::isQmlDebuggingSupported(step()->target()->kit(),
                                                                       &warningText);
    qmlDebuggingLibraryCheckBox->setEnabled(supported);

    if (supported && qbsStep()->isQmlDebuggingEnabled())
        warningText = tr("Might make your application vulnerable. Only use in a safe environment.");

    qmlDebuggingWarningText->setText(warningText);
    qmlDebuggingWarningIcon->setVisible(!warningText.isEmpty());
}

void QbsBuildStepConfigWidget::updatePropertyEdit(const QVariantMap &data)
{
    QVariantMap editable = data;

    // remove data that is edited with special UIs:
    editable.remove(Constants::QBS_CONFIG_PROFILE_KEY);
    editable.remove(Constants::QBS_CONFIG_VARIANT_KEY);
    editable.remove(Constants::QBS_CONFIG_DECLARATIVE_DEBUG_KEY); // For existing .user files
    editable.remove(Constants::QBS_CONFIG_QUICK_DEBUG_KEY);
    editable.remove(Constants::QBS_FORCE_PROBES_KEY);
    editable.remove(Constants::QBS_INSTALL_ROOT_KEY);

    QStringList propertyList;
    for (QVariantMap::const_iterator i = editable.constBegin(); i != editable.constEnd(); ++i)
        propertyList.append(i.key() + ':' + i.value().toString());

    propertyEdit->setText(QtcProcess::joinArgs(propertyList));
}

void QbsBuildStepConfigWidget::changeBuildVariant(int idx)
{
    QString variant;
    if (idx == 1)
        variant = Constants::QBS_VARIANT_RELEASE;
    else
        variant = Constants::QBS_VARIANT_DEBUG;
    m_ignoreChange = true;
    qbsStep()->setBuildVariant(variant);
    m_ignoreChange = false;
}

void QbsBuildStepConfigWidget::changeShowCommandLines(bool show)
{
    m_ignoreChange = true;
    qbsStep()->setShowCommandLines(show);
    m_ignoreChange = false;
}

void QbsBuildStepConfigWidget::changeKeepGoing(bool kg)
{
    m_ignoreChange = true;
    qbsStep()->setKeepGoing(kg);
    m_ignoreChange = false;
}

void QbsBuildStepConfigWidget::changeJobCount(int count)
{
    m_ignoreChange = true;
    qbsStep()->setMaxJobs(count);
    m_ignoreChange = false;
}

void QbsBuildStepConfigWidget::changeInstall(bool install)
{
    m_ignoreChange = true;
    qbsStep()->setInstall(install);
    m_ignoreChange = false;
}

void QbsBuildStepConfigWidget::changeCleanInstallRoot(bool clean)
{
    m_ignoreChange = true;
    qbsStep()->setCleanInstallRoot(clean);
    m_ignoreChange = false;
}

void QbsBuildStepConfigWidget::changeUseDefaultInstallDir(bool useDefault)
{
    m_ignoreChange = true;
    QVariantMap config = qbsStep()->qbsConfiguration(QbsBuildStep::PreserveVariables);
    installDirChooser->setEnabled(!useDefault);
    if (useDefault)
        config.remove(Constants::QBS_INSTALL_ROOT_KEY);
    else
        config.insert(Constants::QBS_INSTALL_ROOT_KEY, installDirChooser->rawPath());
    qbsStep()->setQbsConfiguration(config);
    m_ignoreChange = false;
}

void QbsBuildStepConfigWidget::changeInstallDir(const QString &dir)
{
    if (!qbsStep()->hasCustomInstallRoot())
        return;
    m_ignoreChange = true;
    QVariantMap config = qbsStep()->qbsConfiguration(QbsBuildStep::PreserveVariables);
    config.insert(Constants::QBS_INSTALL_ROOT_KEY, dir);
    qbsStep()->setQbsConfiguration(config);
    m_ignoreChange = false;
}

void QbsBuildStepConfigWidget::changeForceProbes(bool forceProbes)
{
    m_ignoreChange = true;
    qbsStep()->setForceProbes(forceProbes);
    m_ignoreChange = false;
}

void QbsBuildStepConfigWidget::applyCachedProperties()
{
    QVariantMap data;
    const QVariantMap tmp = qbsStep()->qbsConfiguration(QbsBuildStep::PreserveVariables);

    // Insert values set up with special UIs:
    data.insert(Constants::QBS_CONFIG_PROFILE_KEY,
                tmp.value(Constants::QBS_CONFIG_PROFILE_KEY));
    data.insert(Constants::QBS_CONFIG_VARIANT_KEY,
                tmp.value(Constants::QBS_CONFIG_VARIANT_KEY));
    const QStringList additionalSpecialKeys({Constants::QBS_CONFIG_DECLARATIVE_DEBUG_KEY,
                                             Constants::QBS_CONFIG_QUICK_DEBUG_KEY,
                                             Constants::QBS_INSTALL_ROOT_KEY});
    for (const QString &key : additionalSpecialKeys) {
        const auto it = tmp.constFind(key);
        if (it != tmp.cend())
            data.insert(key, it.value());
    }

    for (int i = 0; i < m_propertyCache.count(); ++i) {
        const Property &property = m_propertyCache.at(i);
        data.insert(property.name, property.value);
    }

    m_ignoreChange = true;
    qbsStep()->setQbsConfiguration(data);
    m_ignoreChange = false;
}

QbsBuildStep *QbsBuildStepConfigWidget::qbsStep() const
{
    return static_cast<QbsBuildStep *>(step());
}

void QbsBuildStepConfigWidget::linkQmlDebuggingLibraryChecked(bool checked)
{
    m_ignoreChange = true;
    qbsStep()->setQmlDebuggingEnabled(checked);
    m_ignoreChange = false;
}

bool QbsBuildStepConfigWidget::validateProperties(Utils::FancyLineEdit *edit, QString *errorMessage)
{
    Utils::QtcProcess::SplitError err;
    QStringList argList = Utils::QtcProcess::splitArgs(edit->text(), Utils::HostOsInfo::hostOs(),
                                                       false, &err);
    if (err != Utils::QtcProcess::SplitOk) {
        if (errorMessage)
            *errorMessage = tr("Could not split properties.");
        return false;
    }

    QList<Property> properties;
    const Utils::MacroExpander *expander = Utils::globalMacroExpander();
    foreach (const QString &rawArg, argList) {
        int pos = rawArg.indexOf(':');
        if (pos > 0) {
            const QString propertyName = rawArg.left(pos);
            static const QStringList specialProperties{
                Constants::QBS_CONFIG_PROFILE_KEY, Constants::QBS_CONFIG_VARIANT_KEY,
                Constants::QBS_CONFIG_QUICK_DEBUG_KEY, Constants::QBS_INSTALL_ROOT_KEY};
            if (specialProperties.contains(propertyName)) {
                if (errorMessage) {
                    *errorMessage = tr("Property \"%1\" cannot be set here. "
                                       "Please use the dedicated UI element.").arg(propertyName);
                }
                return false;
            }
            const QString rawValue = rawArg.mid(pos + 1);
            Property property(propertyName, rawValue, expander->expand(rawValue));
            properties.append(property);
        } else {
            if (errorMessage)
                *errorMessage = tr("No \":\" found in property definition.");
            return false;
        }
    }

    if (m_propertyCache != properties) {
        m_propertyCache = properties;
        applyCachedProperties();
    }
    return true;
}

// --------------------------------------------------------------------
// QbsBuildStepFactory:
// --------------------------------------------------------------------

QbsBuildStepFactory::QbsBuildStepFactory()
{
    registerStep<QbsBuildStep>(Constants::QBS_BUILDSTEP_ID);
    setDisplayName(QbsBuildStep::tr("Qbs Build"));
    setSupportedStepList(ProjectExplorer::Constants::BUILDSTEPS_BUILD);
    setSupportedConfiguration(Constants::QBS_BC_ID);
    setSupportedProjectType(Constants::PROJECT_ID);
}

} // namespace Internal
} // namespace QbsProjectManager

#include "qbsbuildstep.moc"