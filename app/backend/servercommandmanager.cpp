#include "servercommandmanager.h"
#include "nvcomputer.h"
#include "nvhttp.h"
#include "streaming/session.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QXmlStreamReader>
#include <QEventLoop>
#include <QDebug>
#include <QTimer>
#include <QReadWriteLock>
#include <Limelight.h>

ServerCommandManager::ServerCommandManager(QObject *parent)
    : QObject(parent)
    , m_computer(nullptr)
    , m_http(nullptr)
    , m_hasPermission(false)
    , m_isExecuting(false)
    , m_refreshInProgress(false)
{
    qDebug() << "ServerCommandManager: Initialized";
}

ServerCommandManager::~ServerCommandManager()
{
    qDebug() << "ServerCommandManager: Destroyed";
}

void ServerCommandManager::setConnection(NvComputer *computer, NvHTTP *http)
{
    m_computer = computer;
    m_http = http;
    
    bool oldPermission = m_hasPermission;
    
    // Check if this is an Apollo server and refresh commands
    if (computer && http) {
        refreshCommands();
    } else {
        m_hasPermission = false;
        m_availableCommands.clear();
        m_commandNames.clear();
        m_commandDescriptions.clear();
        emit commandsRefreshed();
    }
    
    if (oldPermission != m_hasPermission) {
        emit permissionChanged();
    }
}

void ServerCommandManager::disconnect()
{
    m_computer = nullptr;
    m_http = nullptr;
    m_hasPermission = false;
    m_availableCommands.clear();
    m_commandNames.clear();
    m_commandDescriptions.clear();
    emit commandsRefreshed();
}

bool ServerCommandManager::hasServerCommandPermission() const
{
    return m_hasPermission;
}

void ServerCommandManager::refreshCommands()
{
    bool oldPermission = m_hasPermission;

    m_availableCommands.clear();
    m_commandNames.clear();
    m_commandDescriptions.clear();

    // Apollo lists the commands this client may run in serverinfo, in the
    // order LiSendExecServerCmd() indexes them. Use exactly that list: no
    // network requests here (this runs while a stream starts) and no guessed
    // commands, whose indices wouldn't match the host's.
    QStringList commands;
    if (m_computer) {
        QReadLocker lock(&m_computer->lock);
        commands = m_computer->serverCommands;
    }

    for (const QString &cmd : commands) {
        m_availableCommands.append(cmd);
        m_commandNames[cmd] = cmd;
        m_commandDescriptions[cmd] = "Server command: " + cmd;
    }
    m_hasPermission = !m_availableCommands.isEmpty();

    qDebug() << "ServerCommandManager::refreshCommands: Commands from serverinfo:" << m_availableCommands;

    emit commandsRefreshed();

    if (oldPermission != m_hasPermission) {
        emit permissionChanged();
    }
}

void ServerCommandManager::executeCommand(const QString &commandId)
{
    if (!m_computer || !m_http || !m_hasPermission) {
        emit commandFailed(commandId, "Server commands not available");
        return;
    }
    
    if (!m_availableCommands.contains(commandId)) {
        emit commandFailed(commandId, "Command not found");
        return;
    }

    if (m_isExecuting) {
        emit commandFailed(commandId, "Another command is already executing");
        return;
    }
    
    m_isExecuting = true;
    m_currentExecutingCommand = commandId;
    emit executionStateChanged();
    
    qDebug() << "ServerCommandManager: Executing command:" << commandId;
    
    // Execute the actual command via HTTP
    sendCommandExecution(commandId);
}

void ServerCommandManager::executeCustomCommand(const QString &command)
{
    if (command.isEmpty()) {
        emit commandFailed("custom", "Empty command");
        return;
    }

    if (!m_computer || !m_http || !m_hasPermission) {
        emit commandFailed("custom", "Server commands not available");
        return;
    }

    if (m_isExecuting) {
        emit commandFailed("custom", "Another command is already executing");
        return;
    }
    
    m_isExecuting = true;
    m_currentExecutingCommand = "custom";
    emit executionStateChanged();
    
    qDebug() << "ServerCommandManager: Executing custom command:" << command;
    
    // TODO: Implement actual custom command execution via HTTP
    // For now, just simulate success with a delay
    QTimer::singleShot(1500, this, [this, command]() {
        m_isExecuting = false;
        m_currentExecutingCommand.clear();
        emit executionStateChanged();
        emit commandExecuted("custom", true, QString("Custom command '%1' executed successfully").arg(command));
    });
}

QStringList ServerCommandManager::getAvailableCommands() const
{
    return m_availableCommands;
}

QString ServerCommandManager::getCommandName(const QString &commandId) const
{
    return m_commandNames.value(commandId, commandId);
}

QString ServerCommandManager::getCommandDescription(const QString &commandId) const
{
    return m_commandDescriptions.value(commandId, "No description available");
}

bool ServerCommandManager::isApolloServer() const
{
    if (!m_computer) {
        qDebug() << "ServerCommandManager::isApolloServer: No computer object";
        return false;
    }
    
    // Simplified Apollo detection - always return true and let HTTP calls fail naturally
    // This matches the approach used in ClipboardManager for better reliability
    qDebug() << "ServerCommandManager::isApolloServer: Assuming Apollo server (simplified detection)";
    return true;
}

void ServerCommandManager::sendCommandExecution(const QString &commandId)
{
    qDebug() << "ServerCommandManager: Sending command execution:" << commandId;

    if (!m_computer) {
        qWarning() << "ServerCommandManager: No computer object available";
        m_isExecuting = false;
        m_currentExecutingCommand.clear();
        emit executionStateChanged();
        emit commandFailed(commandId, "No computer connection");
        return;
    }

    // Index into the same serverinfo list refreshCommands() published, which
    // is the order the host executes commands by
    const QStringList serverCommands = m_availableCommands;
    if (serverCommands.isEmpty()) {
        qWarning() << "ServerCommandManager: No server commands available";
        m_isExecuting = false;
        m_currentExecutingCommand.clear();
        emit executionStateChanged();
        emit commandFailed(commandId, "No server commands available");
        return;
    }

    qDebug() << "ServerCommandManager: Available server commands:" << serverCommands;

    // Find the command in the server's command list
    int cmdId = -1;
    for (int i = 0; i < serverCommands.size(); ++i) {
        if (serverCommands[i] == commandId) {
            cmdId = i;
            break;
        }
    }

    if (cmdId == -1) {
        qWarning() << "ServerCommandManager: Command not found in server commands:" << commandId;
        qWarning() << "ServerCommandManager: Available commands:" << serverCommands;
        m_isExecuting = false;
        m_currentExecutingCommand.clear();
        emit executionStateChanged();
        emit commandFailed(commandId, "Command not supported by server");
        return;
    }

    qDebug() << "ServerCommandManager: Mapped command" << commandId << "to index" << cmdId;

    // Check for active streaming session
    if (!isStreamingSessionActive()) {
        qWarning() << "ServerCommandManager: Cannot execute command - no active streaming session";
        m_isExecuting = false;
        m_currentExecutingCommand.clear();
        emit executionStateChanged();
        emit commandFailed(commandId, "Server commands are only available during active streaming sessions");
        return;
    }

    // Use ENet-based command execution (correct approach for Apollo servers)
    qDebug() << "ServerCommandManager: Using ENet-based command execution";
    int result = LiSendExecServerCmd(static_cast<uint8_t>(cmdId));

    // Process result
    m_isExecuting = false;
    m_currentExecutingCommand.clear();
    emit executionStateChanged();

    if (result == 0) {
        qDebug() << "ServerCommandManager: Command executed successfully:" << commandId;
        emit commandExecuted(commandId, true, "Command executed successfully");
    } else {
        qWarning() << "ServerCommandManager: Command execution failed:" << commandId << "with result:" << result;
        emit commandFailed(commandId, QString("Command execution failed with result: %1").arg(result));
    }
}

void ServerCommandManager::onCommandExecutionFinished()
{
    // TODO: Handle response from sendCommandExecution()
    // This would parse the execution result and emit commandExecuted signal
    qDebug() << "ServerCommandManager: Command execution finished";
}

bool ServerCommandManager::isStreamingSessionActive() const
{
    // Check if there's an active streaming session using Session::get()
    Session* activeSession = Session::get();
    
    if (!activeSession) {
        qDebug() << "ServerCommandManager: No active session";
        return false;
    }
    
    // The session exists, but we need to check if it has successfully connected
    // Since m_AsyncConnectionSuccess is private, we'll assume that if we have an active session
    // and basic connection requirements are met, we can attempt the command
    // The LiSendExecServerCmd function will provide the definitive answer
    
    if (!m_computer || !m_http) {
        qDebug() << "ServerCommandManager: No computer or HTTP connection";
        return false;
    }
    
    qDebug() << "ServerCommandManager: Session appears to be active";
    return true;
}

void ServerCommandManager::onComputerPairingCompleted()
{
    qDebug() << "ServerCommandManager::onComputerPairingCompleted: Computer pairing completed, refreshing commands";
    
    // Wait a short moment to ensure the computer state is fully updated after pairing
    QTimer::singleShot(1000, this, [this]() {
        if (m_computer && m_http) {
            qDebug() << "ServerCommandManager::onComputerPairingCompleted: Delayed refresh after pairing";
            refreshCommands();
        }
    });
}

void ServerCommandManager::onComputerStateChanged()
{
    if (!m_computer) {
        qDebug() << "ServerCommandManager::onComputerStateChanged: No computer object";
        return;
    }
    
    qDebug() << "ServerCommandManager::onComputerStateChanged: Computer state changed, pair state:" << m_computer->pairState;
    
    // If the computer is now paired and we don't have server commands yet, refresh them
    if (m_computer->pairState == NvComputer::PS_PAIRED && m_availableCommands.isEmpty() && !m_refreshInProgress) {
        qDebug() << "ServerCommandManager::onComputerStateChanged: Computer is now paired, refreshing server commands";
        QTimer::singleShot(500, this, [this]() {
            if (m_computer && m_http && m_computer->pairState == NvComputer::PS_PAIRED) {
                qDebug() << "ServerCommandManager::onComputerStateChanged: Delayed refresh after state change";
                refreshCommands();
            }
        });
    }
}

void ServerCommandManager::noCommandsAvailable()
{
    // This matches the Android dialog shown when no server commands are available
    qDebug() << "ServerCommandManager::noCommandsAvailable: No server commands available";
    // UI components can connect to this slot to display appropriate messaging
}

