#pragma once

#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

// A16 BASIC v1.0
//
// A small, self-contained BASIC interpreter for the Aurora workstation. It
// follows the 1986 conventions: numbered lines, direct mode, single-letter
// variables with optional $ suffix, line-oriented errors such as
// "?SYNTAX ERROR IN 30" and a hard instruction budget so a runaway loop can
// never freeze the simulator.
//
// The interpreter has no dependency on Qt Widgets: the terminal, the Lua
// bridge and the test suite all drive it through submit().
class Basic {
  public:
    using TextSink = std::function<void(const QString &)>;

    struct Limits {
        static constexpr int OutputBytes = 64 * 1024;
        static constexpr int Instructions = 250000;
        static constexpr int ProgramLines = 2000;
        static constexpr int LineLength = 200;
        static constexpr int SourceBytes = 64 * 1024;
        static constexpr int ArrayElements = 4096;
    };

    Basic();

    // Feeds one typed line. Program lines are stored; anything else runs in
    // direct mode. Returns the text the terminal should display.
    QString submit(const QString &line);

    // True while RUN/INPUT is waiting for the next typed value.
    bool awaitingInput() const { return m_inputPending; }

    // True after SYSTEM/EXIT: the shell should leave BASIC mode.
    bool exited() const { return m_exited; }
    void clearExit() { m_exited = false; }

    // Program helpers (used by SAVE/LOAD, the Lua bridge and the tests).
    QString source() const;
    bool load(const QByteArray &source, QString *error = nullptr);
    QString programLine(int number) const;
    bool setProgramLine(int number, const QString &body, QString *error = nullptr);
    void removeProgramLine(int number);
    void newProgram();
    int lineCount() const { return m_lines.size(); }

    // Runs the stored program and returns its output.
    QString run();

    // Executes a single direct-mode statement (no program storage).
    QString executeDirect(const QString &statement);

    void setSoundHandler(TextSink sink) { m_sound = std::move(sink); }
    void setFileReader(std::function<QByteArray(const QString &)> reader)
    {
        m_readFile = std::move(reader);
    }
    void setFileWriter(std::function<bool(const QString &, const QByteArray &)> writer)
    {
        m_writeFile = std::move(writer);
    }

  private:
    struct Value {
        bool text = false;
        double number = 0;
        QString string;
        static Value of(double n) { return Value{false, n, {}}; }
        static Value of(const QString &s) { return Value{true, 0, s}; }
        QString display() const;
    };
    struct LoopFrame {
        QString variable;
        double limit = 0;
        double step = 1;
        int lineIndex = 0;
        int statement = 0;
        bool used = false;
    };
    struct ArrayFrame {
        QString name;
        bool text = false;
        QVector<int> dimensions;
        QVector<double> numbers;
        QVector<QString> strings;
    };
    struct Program {
        int number = 0;
        QStringList statements;
    };

    QMap<int, QString> m_lines;
    QVector<Program> m_program;
    QList<LoopFrame> m_loops;
    QList<int> m_gosub;
    QHash<QString, Value> m_variables;
    QHash<QString, ArrayFrame> m_arrays;
    QStringList m_data;
    int m_dataIndex = 0;
    QByteArray m_output;
    int m_column = 0;
    int m_instructions = 0;
    bool m_truncated = false;
    bool m_exited = false;
    bool m_inputPending = false;
    QString m_inputTarget;
    bool m_inputText = false;
    int m_resumeLine = 0;
    int m_resumeStatement = 0;
    int m_jumpPc = -1;
    int m_jumpStmt = 0;
    TextSink m_sound;
    std::function<QByteArray(const QString &)> m_readFile;
    std::function<bool(const QString &, const QByteArray &)> m_writeFile;
    mutable QString m_lastError;

    // output helpers
    void writeOutput(const QString &text);
    void writeLine(const QString &text = {});
    void newline();
    void finish();

    // statements
    void runFrom(int pc, int statement);
    void executeStatement(const QString &statement, int lineNumber, int lineIndex, int statementIndex,
                          bool &jumped);
    int findLine(int number) const;
    void parseData();
    void resetState(bool keepProgram);

    // expression parsing
    class Parser;
    friend class Parser;
};
