#include "basic.h"

#include <QHash>
#include <QtTest>

class BasicTest : public QObject {
    Q_OBJECT
  private slots:
    void storesLinesAndRuns() {
        Basic basic;
        QCOMPARE(basic.submit("10 PRINT \"HELLO\""), QString());
        QCOMPARE(basic.submit("20 END"), QString());
        QCOMPARE(basic.lineCount(), 2);
        QCOMPARE(basic.submit("RUN"), QString("HELLO\n"));
        QVERIFY(basic.submit("LIST").startsWith("10 PRINT"));
        basic.submit("10");
        QCOMPARE(basic.lineCount(), 1);
    }

    void directModeAndNumbers() {
        Basic basic;
        QCOMPARE(basic.submit("PRINT 2+2"), QString(" 4\n"));
        QCOMPARE(basic.submit("PRINT 7/2"), QString(" 3.5\n"));
        QCOMPARE(basic.submit("PRINT \"A\"+\"B\""), QString("AB\n"));
        QCOMPARE(basic.submit("PRINT 2^10"), QString(" 1024\n"));
        QVERIFY(basic.submit("PRINT 1/0").startsWith("?DIVISION BY ZERO"));
        QVERIFY(basic.submit("PRINT 1+").startsWith("?SYNTAX ERROR"));
    }

    void printSeparatorsAndTabs() {
        Basic basic;
        QCOMPARE(basic.submit("PRINT 1;2;3"), QString(" 1 2 3\n"));
        QCOMPARE(basic.submit("PRINT \"X\";"), QString("X"));
        QCOMPARE(basic.submit("PRINT TAB(4);\"Y\""), QString("    Y\n"));
    }

    void loopsAndBranches() {
        Basic basic;
        basic.submit("10 FOR I = 1 TO 3");
        basic.submit("20 PRINT I");
        basic.submit("30 NEXT I");
        QCOMPARE(basic.submit("RUN"), QString(" 1\n 2\n 3\n"));

        basic.newProgram();
        basic.submit("10 LET A = 5");
        basic.submit("20 IF A > 3 THEN GOTO 40");
        basic.submit("30 PRINT \"SMALL\"");
        basic.submit("40 PRINT \"BIG\"");
        QCOMPARE(basic.submit("RUN"), QString("BIG\n"));

        basic.newProgram();
        basic.submit("10 FOR I = 3 TO 1 STEP -1");
        basic.submit("20 PRINT I;");
        basic.submit("30 NEXT I");
        QCOMPARE(basic.submit("RUN"), QString(" 3 2 1\n"));
    }

    void subroutinesAndData() {
        Basic basic;
        basic.submit("10 GOSUB 100");
        basic.submit("20 PRINT \"BACK\"");
        basic.submit("30 END");
        basic.submit("100 PRINT \"SUB\"");
        basic.submit("110 RETURN");
        QCOMPARE(basic.submit("RUN"), QString("SUB\nBACK\n"));

        basic.newProgram();
        basic.submit("10 DIM A(2)");
        basic.submit("20 FOR I = 0 TO 2");
        basic.submit("30 READ A(I)");
        basic.submit("40 NEXT I");
        basic.submit("50 PRINT A(0);A(1);A(2)");
        basic.submit("60 DATA 4,5,6");
        QCOMPARE(basic.submit("RUN"), QString(" 4 5 6\n"));

        basic.newProgram();
        basic.submit("10 DIM N(1)");
        QCOMPARE(basic.submit("RUN"), QString());
        QVERIFY(basic.submit("DIM N(3)").startsWith("?REDIMENSIONED"));
    }

    void stringsAndFunctions() {
        Basic basic;
        QCOMPARE(basic.submit("PRINT LEFT$(\"AURORA\",3)"), QString("AUR\n"));
        QCOMPARE(basic.submit("PRINT RIGHT$(\"AURORA\",2)"), QString("RA\n"));
        QCOMPARE(basic.submit("PRINT MID$(\"AURORA\",2,3)"), QString("URO\n"));
        QCOMPARE(basic.submit("PRINT LEN(\"A16\")"), QString(" 3\n"));
        QCOMPARE(basic.submit("PRINT CHR$(65)"), QString("A\n"));
        QCOMPARE(basic.submit("PRINT ASC(\"A\")"), QString(" 65\n"));
        QCOMPARE(basic.submit("PRINT INT(3.9)"), QString(" 3\n"));
        QCOMPARE(basic.submit("PRINT SQR(81)"), QString(" 9\n"));
    }

    void inputResumesTheProgram() {
        Basic basic;
        basic.submit("10 INPUT \"NAME\"; N$");
        basic.submit("20 PRINT \"HI \";N$");
        QCOMPARE(basic.submit("RUN"), QString("NAME? "));
        QVERIFY(basic.awaitingInput());
        QCOMPARE(basic.submit("Ada"), QString("HI Ada\n"));
        QVERIFY(!basic.awaitingInput());
    }

    void runtimeErrorsCarryLineNumbers() {
        Basic basic;
        basic.submit("10 PRINT )");
        QCOMPARE(basic.submit("RUN"), QString("?SYNTAX ERROR IN 10\n"));

        basic.newProgram();
        basic.submit("10 GOTO 500");
        QCOMPARE(basic.submit("RUN"), QString("?LINE 500 NOT FOUND IN 10\n"));

        basic.newProgram();
        basic.submit("10 RETURN");
        QVERIFY(basic.submit("RUN").startsWith("?RETURN WITHOUT GOSUB"));
    }

    void runawayProgramIsStopped() {
        Basic basic;
        basic.submit("10 GOTO 10");
        QElapsedTimer timer;
        timer.start();
        const QString output = basic.submit("RUN");
        QVERIFY(output.startsWith("?PROGRAM TOOK TOO LONG"));
        QVERIFY(timer.elapsed() < 15000);
    }

    void outputIsBounded() {
        Basic basic;
        basic.submit("10 FOR I = 1 TO 20000");
        basic.submit("20 PRINT \"0123456789012345678901234567890123456789\"");
        basic.submit("30 NEXT I");
        const QString output = basic.submit("RUN");
        QVERIFY(output.contains("[output truncated]"));
        QVERIFY(output.toUtf8().size() < Basic::Limits::OutputBytes + 256);
    }

    void saveAndLoadUseTheInjectedDisk() {
        Basic basic;
        QHash<QString, QByteArray> disk;
        basic.setFileWriter([&disk](const QString &name, const QByteArray &data) {
            disk.insert(name, data);
            return true;
        });
        basic.setFileReader([&disk](const QString &name) { return disk.value(name); });
        basic.submit("10 PRINT \"SAVED\"");
        QCOMPARE(basic.submit("SAVE \"DEMO.BAS\""), QString());
        QVERIFY(disk.value("DEMO.BAS").contains("PRINT \"SAVED\""));

        Basic other;
        QString error;
        QVERIFY(other.load(disk.value("DEMO.BAS"), &error));
        QCOMPARE(other.lineCount(), 1);
        QCOMPARE(other.submit("RUN"), QString("SAVED\n"));
        QVERIFY(!other.load("nonsense\n", &error));
        QVERIFY(!error.isEmpty());
    }

    void clsAndBeepReachTheHooks() {
        Basic basic;
        QStringList sounds;
        basic.setSoundHandler([&sounds](const QString &event) { sounds << event; });
        basic.submit("10 CLS");
        basic.submit("20 BEEP");
        const QString output = basic.submit("RUN");
        QVERIFY(output.contains(QStringLiteral("\x01") + "CLS"));
        QCOMPARE(sounds, QStringList{"beep"});
    }

    void exitLeavesBasicMode() {
        Basic basic;
        QVERIFY(basic.submit("SYSTEM").contains("Leaving"));
        QVERIFY(basic.exited());
        basic.clearExit();
        QVERIFY(!basic.exited());
    }
};

QTEST_GUILESS_MAIN(BasicTest)
#include "basic_test.moc"
