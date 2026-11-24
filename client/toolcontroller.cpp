#include "toolcontroller.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLibraryInfo>
#include <QStandardPaths>
#include <QTimeZone>

ToolController::ToolController(WebFetcher* fetcher, QObject* parent)
    : QObject(parent)
    , m_fetcher(fetcher)
{
    loadSettings();
    detectDefaultBinaries();

    connect(this, &ToolController::agentEnabledChanged, this, &ToolController::saveSettings);
    connect(this, &ToolController::workspaceRootChanged, this, &ToolController::saveSettings);
    connect(this, &ToolController::cmakePathChanged, this, &ToolController::saveSettings);
    connect(this, &ToolController::qmllintPathChanged, this, &ToolController::saveSettings);
}

void ToolController::loadSettings()
{
    QSettings s("MlxLocal", "Client");
    m_agentEnabled = s.value("agent/enabled", true).toBool();
    m_workspaceRoot = s.value("agent/workspaceRoot").toString();
    m_cmakePath = s.value("agent/cmakePath").toString();
    m_qmllintPath = s.value("agent/qmllintPath").toString();

    const QVariantMap defaults {
        { "file_create", "ask" },
        { "file_read", "allow" },
        { "file_update", "ask" },
        { "file_delete", "ask" },
        { "dir_create", "ask" },
        { "dir_read", "allow" },
        { "dir_update", "ask" },
        { "dir_delete", "deny" },
        { "web_fetch", "ask" },
        { "cmake_build", "ask" },
        { "qmllint_check", "allow" }
    };

    QVariantMap loaded = s.value("agent/permissions").toMap();
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        if (!loaded.contains(it.key())) {
            loaded[it.key()] = it.value();
        }
    }
    m_permissions = loaded;
}

void ToolController::saveSettings()
{
    QSettings s("MlxLocal", "Client");
    s.setValue("agent/enabled", m_agentEnabled);
    s.setValue("agent/workspaceRoot", m_workspaceRoot);
    s.setValue("agent/cmakePath", m_cmakePath);
    s.setValue("agent/qmllintPath", m_qmllintPath);
    s.setValue("agent/permissions", m_permissions);
}

void ToolController::detectDefaultBinaries()
{
    bool changed = false;
    if (m_cmakePath.isEmpty()) {
        QString found = QStandardPaths::findExecutable("cmake");
        if (found.isEmpty()) {
            for (const QString& candidate : { "/opt/homebrew/bin/cmake", "/usr/local/bin/cmake", "/Applications/CMake.app/Contents/bin/cmake" }) {
                if (QFileInfo(candidate).isExecutable()) {
                    found = candidate;
                    break;
                }
            }
        }
        if (!found.isEmpty()) {
            m_cmakePath = found;
            changed = true;
        }
    }

    if (m_qmllintPath.isEmpty()) {
        const QString qtBin = QLibraryInfo::path(QLibraryInfo::BinariesPath) + "/qmllint";
        QString found = QFileInfo(qtBin).isExecutable() ? qtBin : QStandardPaths::findExecutable("qmllint");
        if (!found.isEmpty()) {
            m_qmllintPath = found;
            changed = true;
        }
    }

    if (changed) {
        saveSettings();
        emit cmakePathChanged();
        emit qmllintPathChanged();
    }
}

bool ToolController::cmakeValid() const
{
    return !m_cmakePath.isEmpty() && QFileInfo(m_cmakePath).isExecutable();
}

bool ToolController::qmllintValid() const
{
    return !m_qmllintPath.isEmpty() && QFileInfo(m_qmllintPath).isExecutable();
}

void ToolController::setWorkspaceUrl(const QUrl& folderUrl)
{
    setWorkspaceRoot(folderUrl.isLocalFile() ? folderUrl.toLocalFile() : folderUrl.toString());
}

void ToolController::setCmakeUrl(const QUrl& fileUrl)
{
    setCmakePath(fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString());
}

void ToolController::setQmllintUrl(const QUrl& fileUrl)
{
    setQmllintPath(fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString());
}

void ToolController::setPermission(const QString& toolName, const QString& mode)
{
    if (m_permissions.value(toolName).toString() != mode) {
        m_permissions[toolName] = mode;
        saveSettings();
        emit permissionsChanged();
    }
}

// --- Генерация JSON Schema только для разрешенных (не deny) инструментов ---

QJsonArray ToolController::enabledToolSchemas() const
{
    if (!m_agentEnabled)
        return { };

    QJsonArray arr;
    auto addTool = [&arr](const QString& name, const QString& desc, const QJsonObject& props, const QJsonArray& required) {
        arr.append(QJsonObject {
            { "type", "function" },
            { "function", QJsonObject { { "name", name }, { "description", desc }, { "parameters", QJsonObject { { "type", "object" }, { "properties", props }, { "required", required } } } } } });
    };

    // 1. Дата и время
    addTool("get_datetime", "Returns current local and UTC date, time, weekday, and timezone.", { }, { });

    // 2. CRUD ФС (только если выбрана рабочая папка)
    const bool hasWorkspace = !m_workspaceRoot.isEmpty() && QDir(m_workspaceRoot).exists();
    auto isAllowed = [this](const QString& key) {
        return m_permissions.value(key, "deny").toString() != "deny";
    };

    if (hasWorkspace) {
        if (isAllowed("file_create")) {
            addTool("file_create",
                "Creates a NEW file inside the workspace. Fails if the file already exists (use file_update to modify existing files).",
                QJsonObject {
                    { "path", QJsonObject { { "type", "string" }, { "description", "Relative path inside workspace" } } },
                    { "content", QJsonObject { { "type", "string" }, { "description", "Optional initial file content" } } } },
                QJsonArray { "path" });
        }
        if (isAllowed("file_read")) {
            addTool("file_read",
                "Reads UTF-8 text content of a file inside the workspace.",
                QJsonObject {
                    { "path", QJsonObject { { "type", "string" }, { "description", "Relative path inside workspace" } } } },
                QJsonArray { "path" });
        }
        if (isAllowed("file_update")) {
            addTool("file_update",
                "Modifies an existing file inside the workspace. By default replaces exact unique old_text with new_text. Set full_rewrite=true (and omit old_text) to overwrite the entire file.",
                QJsonObject {
                    { "path", QJsonObject { { "type", "string" }, { "description", "Relative path inside workspace" } } },
                    { "old_text", QJsonObject { { "type", "string" }, { "description", "Exact text block to replace (must be empty if full_rewrite is true)" } } },
                    { "new_text", QJsonObject { { "type", "string" }, { "description", "Replacement text or full new file content" } } },
                    { "full_rewrite", QJsonObject { { "type", "boolean" }, { "description", "If true, replaces entire file with new_text" } } } },
                QJsonArray { "path", "new_text" });
        }
        if (isAllowed("file_delete")) {
            addTool("file_delete",
                "Deletes an existing file inside the workspace.",
                QJsonObject {
                    { "path", QJsonObject { { "type", "string" }, { "description", "Relative path inside workspace" } } } },
                QJsonArray { "path" });
        }
        if (isAllowed("dir_create")) {
            addTool("dir_create",
                "Creates a directory (including parent directories) inside the workspace.",
                QJsonObject {
                    { "path", QJsonObject { { "type", "string" }, { "description", "Relative directory path" } } } },
                QJsonArray { "path" });
        }
        if (isAllowed("dir_read")) {
            addTool("dir_read",
                "Lists files and subdirectories inside a workspace directory.",
                QJsonObject {
                    { "path", QJsonObject { { "type", "string" }, { "description", "Relative directory path (empty or '.' for workspace root)" } } } },
                QJsonArray { });
        }
        if (isAllowed("dir_update")) {
            addTool("dir_update",
                "Renames or moves a directory inside the workspace.",
                QJsonObject {
                    { "old_path", QJsonObject { { "type", "string" } } },
                    { "new_path", QJsonObject { { "type", "string" } } } },
                QJsonArray { "old_path", "new_path" });
        }
        if (isAllowed("dir_delete")) {
            addTool("dir_delete",
                "Recursively deletes a directory inside the workspace.",
                QJsonObject {
                    { "path", QJsonObject { { "type", "string" } } } },
                QJsonArray { "path" });
        }
    }

    // 3. Web Fetch
    if (isAllowed("web_fetch")) {
        addTool("web_fetch",
            "Fetches text content from a given HTTP/HTTPS URL.",
            QJsonObject {
                { "url", QJsonObject { { "type", "string" }, { "description", "Full http:// or https:// URL" } } } },
            QJsonArray { "url" });
    }

    // 4. Внешние CLI инструменты (только если бинарник найден и не deny)
    if (hasWorkspace && cmakeValid() && isAllowed("cmake_build")) {
        addTool("cmake_build",
            "Runs CMake build in the workspace.",
            QJsonObject {
                { "build_dir", QJsonObject { { "type", "string" }, { "description", "Relative build directory (e.g. 'build')" } } },
                { "target", QJsonObject { { "type", "string" }, { "description", "Optional build target" } } },
                { "configure", QJsonObject { { "type", "boolean" }, { "description", "Run cmake -B <build_dir> before building" } } } },
            QJsonArray { "build_dir" });
    }
    if (hasWorkspace && qmllintValid() && isAllowed("qmllint_check")) {
        addTool("qmllint_check",
            "Runs qmllint static analyzer on specified QML files inside the workspace.",
            QJsonObject {
                { "files", QJsonObject { { "type", "array" }, { "items", QJsonObject { { "type", "string" } } }, { "description", "List of relative QML file paths" } } } },
            QJsonArray { "files" });
    }
    return arr;
}
// --- Проверка песочницы (Защита от Path Traversal) ---
bool ToolController::resolveSandboxedPath(const QString& relPath, QString& outAbsPath, QString& outError) const
{
    if (m_workspaceRoot.isEmpty()) {
        outError = "Workspace directory is not configured by user.";
        return false;
    }
    QFileInfo rootInfo(m_workspaceRoot);
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    if (canonicalRoot.isEmpty() || !rootInfo.isDir()) {
        outError = "Workspace directory does not exist.";
        return false;
    }
    QString cleanedRel = relPath.trimmed();
    if (QFileInfo(cleanedRel).isAbsolute()) {
        outError = "Access denied: absolute paths are forbidden. Use paths relative to workspace.";
        return false;
    }
    const QString candidate = QDir::cleanPath(canonicalRoot + "/" + cleanedRel);
    // Проверяем строгую принадлежность песочнице
    if (candidate != canonicalRoot && !candidate.startsWith(canonicalRoot + "/")) {
        outError = "Access denied: path escapes workspace sandbox.";
        return false;
    }
    // Дополнительно проверяем симлинки на существующей части пути
    QFileInfo candInfo(candidate);
    if (candInfo.exists()) {
        const QString realTarget = candInfo.canonicalFilePath();
        if (realTarget != canonicalRoot && !realTarget.startsWith(canonicalRoot + "/")) {
            outError = "Access denied: symlink target is outside workspace.";
            return false;
        }
    }
    outAbsPath = candidate;
    return true;
}
// --- Очередь выполнения и подтверждения (Allow / Ask / Deny) ---
void ToolController::executeToolCalls(const QString& chatId, const QJsonArray& toolCalls)
{
    m_activeBatchChatId = chatId;
    m_queue.clear();
    m_batchResults = QJsonArray();
    m_waitingUserApproval = false;
    for (const auto& val : toolCalls) {
        const auto obj = val.toObject();
        const auto fn = obj["function"].toObject();
        QJsonObject args;
        if (fn["arguments"].isObject()) {
            args = fn["arguments"].toObject();
        } else if (fn["arguments"].isString()) {
            args = QJsonDocument::fromJson(fn["arguments"].toString().toUtf8()).object();
        }
        m_queue.append(PendingCall {
            .chatId = chatId,
            .callId = obj["id"].toString(),
            .name = fn["name"].toString(),
            .args = args });
    }
    processNextCall();
}
void ToolController::processNextCall()
{
    if (m_queue.isEmpty()) {
        emit toolBatchCompleted(m_activeBatchChatId, m_batchResults);
        return;
    }
    const PendingCall current = m_queue.first();
    const QString perm = (current.name == "get_datetime")
        ? "allow"
        : m_permissions.value(current.name, "deny").toString();
    if (perm == "deny") {
        finishCurrentCall("Permission denied: tool '" + current.name + "' is disabled by user.");
        return;
    }
    if (perm == "ask") {
        m_waitingUserApproval = true;
        m_pendingApprovals.append(QVariantMap {
            { "callId", current.callId },
            { "chatId", current.chatId },
            { "name", current.name },
            { "argsJson", QString::fromUtf8(QJsonDocument(current.args).toJson(QJsonDocument::Compact)) } });
        emit pendingApprovalsChanged();
        return;
    }
    runApprovedCall(current);
}
void ToolController::approveCall(const QString& callId)
{
    if (!m_waitingUserApproval || m_queue.isEmpty() || m_queue.first().callId != callId)
        return;
    m_waitingUserApproval = false;
    m_pendingApprovals.clear();
    emit pendingApprovalsChanged();
    runApprovedCall(m_queue.first());
}
void ToolController::rejectCall(const QString& callId)
{
    if (!m_waitingUserApproval || m_queue.isEmpty() || m_queue.first().callId != callId)
        return;
    m_waitingUserApproval = false;
    m_pendingApprovals.clear();
    emit pendingApprovalsChanged();
    finishCurrentCall("User explicitly rejected this tool execution.");
}
void ToolController::finishCurrentCall(const QString& outputText)
{
    if (m_queue.isEmpty())
        return;
    const PendingCall done = m_queue.takeFirst();
    m_batchResults.append(QJsonObject {
        { "role", "tool" },
        { "tool_call_id", done.callId },
        { "content", outputText } });
    processNextCall();
}
void ToolController::runApprovedCall(const PendingCall& call)
{
    if (call.name == "get_datetime") {
        finishCurrentCall(execDateTime());
    } else if (call.name == "file_create") {
        finishCurrentCall(execFileCreate(call.args));
    } else if (call.name == "file_read") {
        finishCurrentCall(execFileRead(call.args));
    } else if (call.name == "file_update") {
        finishCurrentCall(execFileUpdate(call.args));
    } else if (call.name == "file_delete") {
        finishCurrentCall(execFileDelete(call.args));
    } else if (call.name == "dir_create") {
        finishCurrentCall(execDirCreate(call.args));
    } else if (call.name == "dir_read") {
        finishCurrentCall(execDirRead(call.args));
    } else if (call.name == "dir_update") {
        finishCurrentCall(execDirUpdate(call.args));
    } else if (call.name == "dir_delete") {
        finishCurrentCall(execDirDelete(call.args));
    } else if (call.name == "web_fetch") {
        execWebFetch(call.args);
    } else if (call.name == "cmake_build") {
        execCmakeBuild(call.args);
    } else if (call.name == "qmllint_check") {
        execQmllint(call.args);
    } else {
        finishCurrentCall("Unknown tool: " + call.name);
    }
}
// --- Реализации инструментов ---
QString ToolController::execDateTime() const
{
    const QDateTime now = QDateTime::currentDateTime();
    return QString("Local: %1 (%2), UTC: %3, Weekday: %4")
        .arg(now.toString(Qt::ISODate),
            QString::fromUtf8(now.timeZone().id()),
            now.toUTC().toString(Qt::ISODate),
            now.toString("dddd"));
}
QString ToolController::execFileCreate(const QJsonObject& args)
{
    QString absPath, err;
    if (!resolveSandboxedPath(args["path"].toString(), absPath, err))
        return err;
    if (QFileInfo::exists(absPath)) {
        return "Error: File already exists. Use 'file_update' to modify existing files.";
    }
    QDir().mkpath(QFileInfo(absPath).absolutePath());
    QFile f(absPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return "Error: Cannot open file for writing: " + f.errorString();
    }
    f.write(args["content"].toString().toUtf8());
    return "Success: File created at " + args["path"].toString();
}
QString ToolController::execFileRead(const QJsonObject& args) const
{
    QString absPath, err;
    if (!resolveSandboxedPath(args["path"].toString(), absPath, err))
        return err;
    QFileInfo info(absPath);
    if (!info.exists() || !info.isFile())
        return "Error: File does not exist.";
    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return "Error: Cannot read file: " + f.errorString();
    return QString::fromUtf8(f.readAll());
}
QString ToolController::execFileUpdate(const QJsonObject& args)
{
    QString absPath, err;
    if (!resolveSandboxedPath(args["path"].toString(), absPath, err))
        return err;
    QFileInfo info(absPath);
    if (!info.exists() || !info.isFile())
        return "Error: Target file does not exist. Use 'file_create' for new files.";
    const bool fullRewrite = args["full_rewrite"].toBool(false);
    const QString oldText = args["old_text"].toString();
    const QString newText = args["new_text"].toString();
    // Жесткая защита от случайного затирания файла при путанице аргументов
    if (fullRewrite && !oldText.isEmpty()) {
        return "Validation error: 'full_rewrite=true' cannot be used when 'old_text' is provided. "
               "Set 'full_rewrite=false' for partial replacement, or omit 'old_text' to overwrite the entire file.";
    }
    if (fullRewrite) {
        QFile f(absPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            return "Error: Cannot write file: " + f.errorString();
        f.write(newText.toUtf8());
        return "Success: File completely rewritten.";
    }
    if (oldText.isEmpty()) {
        return "Validation error: 'old_text' cannot be empty when 'full_rewrite' is false.";
    }
    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return "Error: Cannot read file: " + f.errorString();
    QString content = QString::fromUtf8(f.readAll());
    f.close();
    const int occurrences = content.count(oldText);
    if (occurrences == 0) {
        return "Error: 'old_text' not found in file. Read the file first to get exact lines.";
    }
    if (occurrences > 1) {
        return QString("Error: 'old_text' matched %1 times. Provide more surrounding context so it matches uniquely.").arg(occurrences);
    }
    content.replace(oldText, newText);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return "Error: Cannot write file: " + f.errorString();
    f.write(content.toUtf8());
    return "Success: File updated.";
}
QString ToolController::execFileDelete(const QJsonObject& args)
{
    QString absPath, err;
    if (!resolveSandboxedPath(args["path"].toString(), absPath, err))
        return err;
    QFileInfo info(absPath);
    if (!info.exists() || !info.isFile())
        return "Error: File does not exist.";
    return QFile::remove(absPath) ? "Success: File deleted." : "Error: Failed to delete file.";
}
QString ToolController::execDirCreate(const QJsonObject& args)
{
    QString absPath, err;
    if (!resolveSandboxedPath(args["path"].toString(), absPath, err))
        return err;
    return QDir().mkpath(absPath) ? "Success: Directory created." : "Error: Failed to create directory.";
}
QString ToolController::execDirRead(const QJsonObject& args) const
{
    QString absPath, err;
    if (!resolveSandboxedPath(args["path"].toString(), absPath, err))
        return err;
    QDir dir(absPath);
    if (!dir.exists())
        return "Error: Directory does not exist.";
    const auto entries = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries, QDir::DirsFirst | QDir::Name);
    QStringList lines;
    for (const auto& e : entries) {
        lines.append(e.isDir() ? ("[DIR]  " + e.fileName() + "/") : ("[FILE] " + e.fileName() + QString(" (%1 B)").arg(e.size())));
    }
    return lines.isEmpty() ? "(empty directory)" : lines.join("\n");
}
QString ToolController::execDirUpdate(const QJsonObject& args)
{
    QString oldAbs, newAbs, err;
    if (!resolveSandboxedPath(args["old_path"].toString(), oldAbs, err))
        return err;
    if (!resolveSandboxedPath(args["new_path"].toString(), newAbs, err))
        return err;
    if (!QFileInfo(oldAbs).isDir())
        return "Error: Source directory does not exist.";
    return QDir().rename(oldAbs, newAbs) ? "Success: Directory moved/renamed." : "Error: Failed to rename/move directory.";
}
QString ToolController::execDirDelete(const QJsonObject& args)
{
    QString absPath, err;
    if (!resolveSandboxedPath(args["path"].toString(), absPath, err))
        return err;
    if (absPath == QFileInfo(m_workspaceRoot).canonicalFilePath()) {
        return "Access denied: cannot delete the workspace root itself.";
    }
    QDir dir(absPath);
    if (!dir.exists())
        return "Error: Directory does not exist.";
    return dir.removeRecursively() ? "Success: Directory deleted recursively." : "Error: Failed to delete directory.";
}
void ToolController::execWebFetch(const QJsonObject& args)
{
    m_fetcher->fetch(args["url"].toString(), [this](bool ok, const QString& result) {
        finishCurrentCall(ok ? result : ("Fetch error: " + result));
    });
}
void ToolController::execCmakeBuild(const QJsonObject& args)
{
    if (!cmakeValid()) {
        finishCurrentCall("Error: CMake executable is not configured.");
        return;
    }
    QString buildAbs, err;
    if (!resolveSandboxedPath(args["build_dir"].toString(), buildAbs, err)) {
        finishCurrentCall(err);
        return;
    }
    const QString rootAbs = QFileInfo(m_workspaceRoot).canonicalFilePath();
    const bool configure = args["configure"].toBool(false);
    const QString target = args["target"].toString().trimmed();
    auto runBuild = [this, buildAbs, target] {
        auto* proc = new QProcess(this);
        QStringList buildArgs { "--build", buildAbs, "--parallel" };
        if (!target.isEmpty()) {
            buildArgs << "--target" << target;
        }
        connect(proc, &QProcess::finished, this, [this, proc](int exitCode, QProcess::ExitStatus) {
            proc->deleteLater();
            const QString out = QString::fromUtf8(proc->readAllStandardOutput() + proc->readAllStandardError());
            finishCurrentCall(QString("CMake build exit code: %1\n%2").arg(exitCode).arg(out.left(12000)));
        });
        proc->start(m_cmakePath, buildArgs);
    };
    if (configure) {
        auto* cfgProc = new QProcess(this);
        connect(cfgProc, &QProcess::finished, this, [this, cfgProc, runBuild](int exitCode, QProcess::ExitStatus) {
            cfgProc->deleteLater();
            if (exitCode != 0) {
                const QString out = QString::fromUtf8(cfgProc->readAllStandardOutput() + cfgProc->readAllStandardError());
                finishCurrentCall(QString("CMake configure failed (code %1):\n%2").arg(exitCode).arg(out.left(12000)));
                return;
            }
            runBuild();
        });
        cfgProc->start(m_cmakePath, { "-S", rootAbs, "-B", buildAbs });
    } else {
        runBuild();
    }
}
void ToolController::execQmllint(const QJsonObject& args)
{
    if (!qmllintValid()) {
        finishCurrentCall("Error: qmllint executable is not configured.");
        return;
    }
    QStringList lintArgs;
    const QString qmlImportPath = QLibraryInfo::path(QLibraryInfo::QmlImportsPath);
    if (!qmlImportPath.isEmpty()) {
        lintArgs << "-I" << qmlImportPath;
    }
    for (const auto& fVal : args["files"].toArray()) {
        QString absFile, err;
        if (!resolveSandboxedPath(fVal.toString(), absFile, err)) {
            finishCurrentCall(err);
            return;
        }
        lintArgs << absFile;
    }
    if (lintArgs.size() <= 2) {
        finishCurrentCall("Error: No QML files provided.");
        return;
    }
    auto* proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc](int exitCode, QProcess::ExitStatus) {
        proc->deleteLater();
        const QString out = QString::fromUtf8(proc->readAllStandardOutput() + proc->readAllStandardError()).trimmed();
        if (exitCode == 0 && out.isEmpty()) {
            finishCurrentCall("qmllint: 0 warnings, 0 errors.");
        } else {
            finishCurrentCall(QString("qmllint exit code %1:\n%2").arg(exitCode).arg(out.left(12000)));
        }
    });
    proc->start(m_qmllintPath, lintArgs);
}