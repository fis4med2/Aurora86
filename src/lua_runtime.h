#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <memory>
class SimSystem;
struct LuaInput {
    QString id, label, value;
    bool multiline = false;
};
struct LuaResult {
    bool ok = false;
    QString output, error, title, text;
    QStringList buttons;
    QList<LuaInput> inputs = {};
};
class LuaRuntime final {
  public:
    explicit LuaRuntime(SimSystem *system);
    ~LuaRuntime();
    LuaRuntime(const LuaRuntime &) = delete;
    LuaRuntime &operator=(const LuaRuntime &) = delete;
    LuaResult run(const QByteArray &source, const QString &chunkName = {});
    LuaResult click(int buttonIndex);
    bool setInput(const QString &id, const QString &value);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
