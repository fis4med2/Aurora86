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
    void inputValuesReachCallbacksAndPersist() {
        QTemporaryDir state;
        SimSystem system(state.path());
        QVERIFY(system.load());
        LuaRuntime lua(&system);
        auto result = lua.run(R"(
            aurora.window {
                title = 'Notebook',
                inputs = {
                    {id='name', label='Name', value='Guest'},
                    {id='note', label='Note', multiline=true, value='First line\nSecond line'}
                },
                buttons = {{label='Save', action=function()
                    assert(aurora.write('/home/guest/docs/note.txt', aurora.input('note')))
                    aurora.text('Saved by '..aurora.input('name'))
                    assert(aurora.input('note', 'Ready for another note'))
                    local bad, err = aurora.input('missing')
                    assert(bad == nil and err)
                end}}
            }
        )");
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.inputs.size(), 2);
        QCOMPARE(result.inputs[0].id, QString("name"));
        QCOMPARE(result.inputs[0].label, QString("Name"));
        QVERIFY(!result.inputs[0].multiline);
        QCOMPARE(result.inputs[1].value, QString("First line\nSecond line"));
        QVERIFY(result.inputs[1].multiline);
        QVERIFY(lua.setInput("name", "Aurora"));
        QVERIFY(lua.setInput("note", QString::fromUtf8("Café\nPersistent note")));
        QVERIFY(!lua.setInput("unknown", "ignored"));
        QVERIFY(!lua.setInput("note", QString(4097, 'a')));
        QVERIFY(!lua.setInput("note", QString(1, QChar::Null)));
        result = lua.click(0);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.text, QString("Saved by Aurora"));
        QCOMPARE(result.inputs[1].value, QString("Ready for another note"));
        SimSystem reopened(state.path());
        QVERIFY(reopened.load());
        QCOMPARE(reopened.read("/home/guest/docs/note.txt"), QByteArray("Café\nPersistent note"));
        result = lua.run("aurora.window{inputs={{id='fresh'}}} aurora.window{title='Empty'}");
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(result.inputs.isEmpty());
        QVERIFY(!lua.setInput("fresh", "old form"));
    }
    void inputDefinitionsAndValuesAreBounded() {
        QTemporaryDir state;
        SimSystem system(state.path());
        QVERIFY(system.load());
        LuaRuntime lua(&system);
        const QList<QByteArray> invalid = {
            "aurora.window{inputs='invalid'}",
            "local inputs={} for i=1,9 do inputs[i]={id='i'..i} end aurora.window{inputs=inputs}",
            "aurora.window{inputs={false}}",
            "aurora.window{inputs={{id='invalid-id'}}}",
            "aurora.window{inputs={{id='same'},{id='same'}}}",
            "aurora.window{inputs={{id='long',value=string.rep('a',4097)}}}",
            "aurora.window{inputs={{id='long',label=string.rep('a',81)}}}",
            "aurora.window{inputs={{id='nul',value='a'..string.char(0)}}}",
            "aurora.window{inputs={{id='bad',value=string.char(255)}}}",
            "aurora.window{inputs={{id='bad',value=string.char(195)}}}",
            "aurora.window{inputs={{id='bad',multiline='true'}}}"
        };
        for (const auto &source : invalid) {
            const auto result = lua.run(source);
            QVERIFY2(!result.ok, source.constData());
            QVERIFY2(!result.error.isEmpty(), source.constData());
            QVERIFY2(result.inputs.isEmpty(), source.constData());
        }
        auto result = lua.run(R"(
            local fields={}
            for i=1,8 do fields[i]={id='i'..i, value=string.rep('a',4096)} end
            aurora.window{inputs=fields}
            local bad, err=aurora.input('i1',string.rep('x',4097))
            assert(bad==nil and err)
            assert(#aurora.input('i1')==4096)
            bad, err=aurora.input('i1',string.char(255))
            assert(bad==nil and err)
            bad, err=aurora.input('i1','bad'..string.char(0))
            assert(bad==nil and err)
        )");
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.inputs.size(), 8);
        QVERIFY(!lua.setInput("i1", QString(2049, QChar(0x00e9))));
        QVERIFY(lua.setInput("i1", QString(2048, QChar(0x00e9))));
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
