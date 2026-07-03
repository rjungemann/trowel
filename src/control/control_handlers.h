#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QPointer>
#include <QString>

#include <functional>

namespace trowel {

class MainWindow;

namespace control {

class ControlConnection;
struct ControlError;

// A Reply is called exactly once per request — either sync (before the
// handler returns) or async (from a signal/timer callback).
using Reply = std::function<void(const QJsonValue& result,
                                 const ControlError* error)>;

// Dispatch a command. The handler must guarantee `reply` is called exactly
// once. `conn` is a QPointer so async handlers can check for disconnection.
void Dispatch(MainWindow* window,
              QPointer<ControlConnection> conn,
              const QString& cmd,
              const QJsonObject& args,
              Reply reply);

}
}
