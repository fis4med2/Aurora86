#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <memory>
class SimSystem;
struct LuaResult {
    bool ok = false;
    QString output, error, title, text;
    QStringList buttons;
};
class LuaRuntime final {
  public:
    explicit LuaRuntime(SimSystem *system);
    ~LuaRuntime();
    LuaRuntime(const LuaRuntime &) = delete;
    LuaRuntime &operator=(const LuaRuntime &) = delete;
    LuaResult run(const QByteArray &source, const QString &chunkName = {});
    LuaResult click(int buttonIndex);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
