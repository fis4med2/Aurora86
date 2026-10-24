#include "lua_runtime.h"
#include "mods.h"
#include "system.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
class LuaTest final : public QObject {
    Q_OBJECT
  private slots:
    void programReadsWritesAndCallsButtons() {
        QTemporaryDir state;
        SimSystem system(state.path());
        QVERIFY(system.load());
        LuaRuntime lua(&system);
        const auto run = lua.run(R"(
            assert(aurora.write('/home/guest/docs/lua.txt','persistent'))
            assert(aurora.read('/home/guest/docs/lua.txt') == 'persistent')
            local n=0
            aurora.window{title='My app',text='Ready',buttons={{label='Count',action=function()
                n=n+1; aurora.text('Clicks: '..n);print('clicked',n)
            end}}}
            print('AURORA Lua')
        )",
                                 "test.lua");
        QVERIFY2(run.ok, qPrintable(run.error));
        QCOMPARE(run.title, QString("My app"));
        QCOMPARE(run.text, QString("Ready"));
        QCOMPARE(run.buttons, QStringList{"Count"});
        auto click = lua.click(0);
        QVERIFY2(click.ok, qPrintable(click.error));
        QCOMPARE(click.text, QString("Clicks: 1"));
        QVERIFY(click.output.contains("clicked\t1"));
        click = lua.click(0);
        QCOMPARE(click.text, QString("Clicks: 2"));
        SimSystem reopened(state.path());
        QVERIFY(reopened.load());
        QCOMPARE(reopened.read("/home/guest/docs/lua.txt"), QByteArray("persistent"));
    }
    void vmRejectsHostAccessAndUnsafeSettings() {
        QTemporaryDir state;
        SimSystem system(state.path());
        QVERIFY(system.load());
        LuaRuntime lua(&system);
        auto result = lua.run(R"(
            assert(io==nil and os==nil and package==nil and debug==nil)
            assert(load==nil and loadfile==nil and dofile==nil and require==nil)
            assert(pcall==nil and xpcall==nil and setmetatable==nil and getmetatable==nil)
            assert(string.match==nil and string.dump==nil and table.move==nil)
            local bad,err=aurora.write('/etc/release','modified'); assert(bad==nil and err)
            assert(aurora.setting('loginCredentials','leak')==nil)
            assert(aurora.setting('username','root')==nil)
            assert(aurora.theme('amber'))
            assert(aurora.setting('fontSize',18))
            assert(aurora.setting('scanlines',false))
            assert(aurora.setting('scanlines')==false)
            assert(aurora.setting('fontSize')==18)
        )");
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(system.setting("theme").toString(), QString("amber"));
        QCOMPARE(system.setting("fontSize").toInt(), 18);
        QVERIFY(!lua.run(QByteArray("\x1bLua", 4)).ok);
        QVERIFY(!lua.run("print(").ok);
    }
    void loopsAndMemoryAreBounded() {
        QTemporaryDir state;
        SimSystem system(state.path());
        QVERIFY(system.load());
        LuaRuntime lua(&system);
        QElapsedTimer timer;
        timer.start();
        auto result = lua.run("while true do end");
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("instruction limit"));
        QVERIFY(timer.elapsed() < 2000);
        result = lua.run("local s=string.rep('x',2*1024*1024)");
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("memory"));
        result = lua.run("for i=1,20000 do print('0123456789') end");
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(result.output.size() < 66000);
        QVERIFY(result.output.contains("truncated"));
        result = lua.run("aurora.window{buttons={{label='Loop',action=function() while true do end end}}}");
        QVERIFY(result.ok);
        QVERIFY(!lua.click(0).ok);
        result = lua.run("local t={} for i=1,4097 do t[i]=i end table.insert(t,0)");
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("table operation limit"));
        result = lua.run("for i=1,1000 do aurora.theme('green') end");
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("instruction limit"));
        result = lua.run("print('recovered')");
        QVERIFY(result.ok);
        QCOMPARE(result.output, QString("recovered\n"));
    }
    void localModsInstallPersistAndRemove() {
        QTemporaryDir host, state;
        SimSystem system(state.path());
        QVERIFY(system.load());
        system.setSetting("modsDirectory", host.path());
        QFile file(host.filePath("hello.lua"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray source =
            "-- @name hello\n-- @version 1.2\n-- @description My first mod\nprint('Hello')\n";
        file.write(source);
        file.close();
        QString error;
        auto found = availableMods(&system, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().version, QString("1.2"));
        QVERIFY(!installMod(&system, "hello").startsWith("error:"));
        QVERIFY(isModInstalled(&system, "hello"));
        QCOMPARE(installedModSource(&system, "hello"), source);
        SimSystem reopened(state.path());
        QVERIFY(reopened.load());
        QVERIFY(isModInstalled(&reopened, "hello"));
        QCOMPARE(installedMods(&reopened).size(), 1);
        LuaRuntime lua(&reopened);
        auto result = lua.run(installedModSource(&reopened, "hello"));
        QVERIFY(result.ok);
        QVERIFY(result.output.contains("Hello"));
        QVERIFY(!removeMod(&reopened, "hello").startsWith("error:"));
        QVERIFY(!isModInstalled(&reopened, "hello"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), source);
        QVERIFY(installMod(&reopened, "../escape").startsWith("error:"));
    }
    void invalidAndQuotaFailuresPreserveMods() {
        QTemporaryDir host, state;
        SimSystem system(state.path());
        QVERIFY(system.load());
        system.setSetting("modsDirectory", host.path());
        QFile file(host.filePath("item.lua"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("print('first')");
        file.close();
        QVERIFY(!installMod(&system, "item").startsWith("error:"));
        QByteArray original = installedModSource(&system, "item");
        QVERIFY(!original.isEmpty());
        const QString fill = "/home/guest/full";
        const int available = SimSystem::DiskBytes - system.usedBytes() - 64 - fill.toUtf8().size();
        QVERIFY(system.write(fill, QByteArray(available, 'x')));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QByteArray("-- larger comment\n") + QByteArray(1000, ' ') + "print('second')");
        file.close();
        QVERIFY(installMod(&system, "item").startsWith("error:"));
        QCOMPARE(installedModSource(&system, "item"), original);
        QVERIFY(system.remove(fill));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("-- @name invalid_name\nprint('bad')");
        file.close();
        QCOMPARE(availableMods(&system).size(), 0);
        QVERIFY(system.write("/home/guest/.mods/item.lua", QByteArray("\0not text", 9)));
        QString error;
        QVERIFY(installedMods(&system, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(!isModInstalled(&system, "item"));
    }
};
QTEST_GUILESS_MAIN(LuaTest)
#include "lua_test.moc"
