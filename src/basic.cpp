#include "basic.h"

#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
constexpr const char *ClsMarker = "\x01" "CLS";
const QString NumberPattern = QStringLiteral("^[0-9]+$");

QString trimmedNumber(double value)
{
    if (!std::isfinite(value))
        return value < 0 ? QStringLiteral("-1E+99") : QStringLiteral("1E+99");
    if (value == std::floor(value) && std::fabs(value) < 1e15)
        return QString::number(qint64(value));
    QString text = QString::number(value, 'g', 6);
    return text;
}

// Splits a program line on top-level colons, keeping quotes and parentheses
// intact. REM comments therefore swallow the rest of the line.
QStringList splitStatements(const QString &line)
{
    QStringList result;
    QString current;
    QChar quote;
    int depth = 0;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar ch = line[i];
        if (!quote.isNull()) {
            current += ch;
            if (ch == quote)
                quote = {};
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
            current += ch;
            continue;
        }
        if (ch == '(')
            ++depth;
        else if (ch == ')')
            depth = qMax(0, depth - 1);
        if (ch == ':' && depth == 0) {
            result << current;
            current.clear();
            continue;
        }
        current += ch;
    }
    result << current;
    while (!result.isEmpty() && result.last().trimmed().isEmpty())
        result.removeLast();
    for (auto &statement : result)
        statement = statement.trimmed();
    return result;
}

// Finds a case-insensitive whole word, used for THEN, TO, STEP and ELSE.
int findWord(const QString &text, const QString &word)
{
    for (int i = 0; i + word.size() <= text.size(); ++i) {
        if (text.mid(i, word.size()).compare(word, Qt::CaseInsensitive) != 0)
            continue;
        const bool leftOk = i == 0 || !text[i - 1].isLetterOrNumber();
        const int end = i + word.size();
        const bool rightOk = end >= text.size() || !text[end].isLetterOrNumber();
        if (leftOk && rightOk)
            return i;
    }
    return -1;
}

QString unquote(const QString &text)
{
    QString value = text.trimmed();
    if (value.size() >= 2 && (value.startsWith('"') || value.startsWith('\'')))
        return value.mid(1, value.size() - 2);
    return value;
}

// A token stream for BASIC expressions.
struct Token {
    enum Kind { Number, Text, Name, Symbol, End } kind = End;
    double number = 0;
    QString text;
};
} // namespace

class Basic::Parser {
  public:
    Parser(Basic &basic, const QString &text, int lineNumber)
        : m_basic(basic), m_text(text), m_line(lineNumber)
    {
        tokenize();
    }

    bool expression(Value &value, QString &error)
    {
        if (!orExpression(value, error))
            return false;
        if (m_index < m_tokens.size()) {
            error = errorFor("?SYNTAX ERROR");
            return false;
        }
        return true;
    }

    bool indexList(QVector<int> &indices, QString &error)
    {
        if (!accept("(")) {
            error = errorFor("?SYNTAX ERROR");
            return false;
        }
        while (true) {
            Value value;
            if (!orExpression(value, error))
                return false;
            if (value.text) {
                error = errorFor("?TYPE MISMATCH ERROR");
                return false;
            }
            indices << int(value.number);
            if (accept(","))
                continue;
            if (accept(")"))
                break;
            error = errorFor("?SYNTAX ERROR");
            return false;
        }
        return true;
    }

    QString errorFor(const QString &message) const
    {
        return message + QStringLiteral(" IN ") + QString::number(m_line);
    }

  private:
    Basic &m_basic;
    QString m_text;
    int m_line = 0;
    QVector<Token> m_tokens;
    int m_index = 0;

    void tokenize()
    {
        const QString &text = m_text;
        for (qsizetype i = 0; i < text.size();) {
            const QChar ch = text[i];
            if (ch.isSpace()) {
                ++i;
                continue;
            }
            if (ch == '"') {
                QString value;
                ++i;
                while (i < text.size() && text[i] != '"')
                    value += text[i++];
                if (i < text.size())
                    ++i;
                Token token;
                token.kind = Token::Text;
                token.text = value;
                m_tokens << token;
                continue;
            }
            if (ch.isDigit() || (ch == '.' && i + 1 < text.size() && text[i + 1].isDigit())) {
                qsizetype start = i;
                while (i < text.size() && (text[i].isDigit() || text[i] == '.'))
                    ++i;
                if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
                    qsizetype exponent = i + 1;
                    if (exponent < text.size() && (text[exponent] == '+' || text[exponent] == '-'))
                        ++exponent;
                    if (exponent < text.size() && text[exponent].isDigit()) {
                        i = exponent;
                        while (i < text.size() && text[i].isDigit())
                            ++i;
                    }
                }
                Token token;
                token.kind = Token::Number;
                token.number = text.mid(start, i - start).toDouble();
                m_tokens << token;
                continue;
            }
            if (ch.isLetter() || ch == '_') {
                qsizetype start = i;
                while (i < text.size() && (text[i].isLetterOrNumber() || text[i] == '_'))
                    ++i;
                QString name = text.mid(start, i - start).toUpper();
                if (i < text.size() && text[i] == '$') {
                    name += '$';
                    ++i;
                }
                Token token;
                token.kind = Token::Name;
                token.text = name;
                m_tokens << token;
                continue;
            }
            if (ch == '<' || ch == '>') {
                QString symbol(ch);
                if (i + 1 < text.size() && (text[i + 1] == '=' || (ch == '<' && text[i + 1] == '>'))) {
                    symbol += text[i + 1];
                    ++i;
                }
                Token token;
                token.kind = Token::Symbol;
                token.text = symbol;
                m_tokens << token;
                ++i;
                continue;
            }
            Token token;
            token.kind = Token::Symbol;
            token.text = QString(ch);
            m_tokens << token;
            ++i;
        }
    }

    const Token &peek() const
    {
        static const Token empty;
        return m_index < m_tokens.size() ? m_tokens[m_index] : empty;
    }

    bool accept(const QString &symbol)
    {
        if (peek().kind == Token::Symbol && peek().text == symbol) {
            ++m_index;
            return true;
        }
        return false;
    }

    bool acceptWord(const QString &word)
    {
        if (peek().kind == Token::Name && peek().text == word) {
            ++m_index;
            return true;
        }
        return false;
    }

    bool orExpression(Value &value, QString &error)
    {
        if (!andExpression(value, error))
            return false;
        while (acceptWord("OR")) {
            Value right;
            if (!andExpression(right, error))
                return false;
            const bool leftTruth = value.text ? !value.string.isEmpty() : value.number != 0;
            const bool rightTruth = right.text ? !right.string.isEmpty() : right.number != 0;
            value = Value::of((leftTruth || rightTruth) ? -1 : 0);
        }
        return true;
    }

    bool andExpression(Value &value, QString &error)
    {
        if (!notExpression(value, error))
            return false;
        while (acceptWord("AND")) {
            Value right;
            if (!notExpression(right, error))
                return false;
            const bool leftTruth = value.text ? !value.string.isEmpty() : value.number != 0;
            const bool rightTruth = right.text ? !right.string.isEmpty() : right.number != 0;
            value = Value::of((leftTruth && rightTruth) ? -1 : 0);
        }
        return true;
    }

    bool notExpression(Value &value, QString &error)
    {
        if (acceptWord("NOT")) {
            if (!notExpression(value, error))
                return false;
            const bool truth = value.text ? !value.string.isEmpty() : value.number != 0;
            value = Value::of(truth ? 0 : -1);
            return true;
        }
        return relational(value, error);
    }

    bool relational(Value &value, QString &error)
    {
        if (!additive(value, error))
            return false;
        const Token &token = peek();
        if (token.kind != Token::Symbol)
            return true;
        static const QStringList operators = {"=", "<>", "<", ">", "<=", ">="};
        if (!operators.contains(token.text))
            return true;
        const QString op = token.text;
        ++m_index;
        Value right;
        if (!additive(right, error))
            return false;
        if (value.text != right.text) {
            error = errorFor("?TYPE MISMATCH ERROR");
            return false;
        }
        int comparison = 0;
        if (value.text) {
            comparison = QString::compare(value.string, right.string);
        } else {
            const double a = value.number, b = right.number;
            comparison = a < b ? -1 : a > b ? 1 : 0;
        }
        bool result = false;
        if (op == "=")
            result = comparison == 0;
        else if (op == "<>")
            result = comparison != 0;
        else if (op == "<")
            result = comparison < 0;
        else if (op == ">")
            result = comparison > 0;
        else if (op == "<=")
            result = comparison <= 0;
        else
            result = comparison >= 0;
        value = Value::of(result ? -1 : 0);
        return true;
    }

    bool additive(Value &value, QString &error)
    {
        if (!multiplicative(value, error))
            return false;
        while (true) {
            const bool plus = accept("+");
            if (!plus && !accept("-"))
                return true;
            Value right;
            if (!multiplicative(right, error))
                return false;
            if (plus) {
                if (value.text && right.text) {
                    value.string += right.string;
                } else if (value.text || right.text) {
                    error = errorFor("?TYPE MISMATCH ERROR");
                    return false;
                } else {
                    value.number += right.number;
                }
            } else {
                if (value.text || right.text) {
                    error = errorFor("?TYPE MISMATCH ERROR");
                    return false;
                }
                value.number -= right.number;
            }
        }
    }

    bool multiplicative(Value &value, QString &error)
    {
        if (!power(value, error))
            return false;
        while (true) {
            if (accept("*")) {
                Value right;
                if (!power(right, error))
                    return false;
                if (value.text || right.text) {
                    error = errorFor("?TYPE MISMATCH ERROR");
                    return false;
                }
                value.number *= right.number;
                continue;
            }
            if (accept("/")) {
                Value right;
                if (!power(right, error))
                    return false;
                if (value.text || right.text) {
                    error = errorFor("?TYPE MISMATCH ERROR");
                    return false;
                }
                if (right.number == 0) {
                    error = errorFor("?DIVISION BY ZERO");
                    return false;
                }
                value.number /= right.number;
                continue;
            }
            if (acceptWord("MOD")) {
                Value right;
                if (!power(right, error))
                    return false;
                if (value.text || right.text) {
                    error = errorFor("?TYPE MISMATCH ERROR");
                    return false;
                }
                if (right.number == 0) {
                    error = errorFor("?DIVISION BY ZERO");
                    return false;
                }
                value = Value::of(std::fmod(value.number, right.number));
                continue;
            }
            return true;
        }
    }

    bool power(Value &value, QString &error)
    {
        if (!unary(value, error))
            return false;
        while (accept("^")) {
            Value right;
            if (!unary(right, error))
                return false;
            if (value.text || right.text) {
                error = errorFor("?TYPE MISMATCH ERROR");
                return false;
            }
            value = Value::of(std::pow(value.number, right.number));
        }
        return true;
    }

    bool unary(Value &value, QString &error)
    {
        if (accept("-")) {
            if (!unary(value, error))
                return false;
            if (value.text) {
                error = errorFor("?TYPE MISMATCH ERROR");
                return false;
            }
            value.number = -value.number;
            return true;
        }
        if (accept("+"))
            return unary(value, error);
        return primary(value, error);
    }

    bool primary(Value &value, QString &error)
    {
        const Token token = peek();
        if (token.kind == Token::Number) {
            ++m_index;
            value = Value::of(token.number);
            return true;
        }
        if (token.kind == Token::Text) {
            ++m_index;
            value = Value::of(token.text);
            return true;
        }
        if (token.kind == Token::Symbol && token.text == "(") {
            ++m_index;
            if (!orExpression(value, error))
                return false;
            if (!accept(")")) {
                error = errorFor("?SYNTAX ERROR");
                return false;
            }
            return true;
        }
        if (token.kind != Token::Name) {
            error = errorFor("?SYNTAX ERROR");
            return false;
        }
        ++m_index;
        const QString name = token.text;
        if (isFunction(name))
            return function(name, value, error);
        QVector<int> indices;
        if (peek().kind == Token::Symbol && peek().text == "(") {
            if (!indexList(indices, error))
                return false;
            return arrayValue(name, indices, value, error);
        }
        value = m_basic.m_variables.value(name);
        return true;
    }

    static bool isFunction(const QString &name)
    {
        static const QSet<QString> functions = {
            "ABS", "SGN", "INT", "SQR", "SIN", "COS", "TAN", "ATN", "EXP", "LOG", "RND",
            "LEN", "VAL", "ASC", "CHR$", "STR$", "LEFT$", "RIGHT$", "MID$", "SPACE$",
            "STRING$", "HEX$", "OCT$", "PI"};
        return functions.contains(name);
    }

    bool function(const QString &name, Value &value, QString &error)
    {
        QVector<Value> arguments;
        if (name == "PI") {
            value = Value::of(M_PI);
            return true;
        }
        if (name == "RND") {
            if (accept("(")) {
                if (!accept(")")) {
                    Value ignored;
                    if (!orExpression(ignored, error))
                        return false;
                    if (!accept(")")) {
                        error = errorFor("?SYNTAX ERROR");
                        return false;
                    }
                }
            }
            value = Value::of(double(QRandomGenerator::global()->generate() % 1000000) / 1000000.0);
            return true;
        }
        if (accept("(")) {
            while (true) {
                Value argument;
                if (!orExpression(argument, error))
                    return false;
                arguments << argument;
                if (accept(","))
                    continue;
                if (accept(")"))
                    break;
                error = errorFor("?SYNTAX ERROR");
                return false;
            }
        }
        const auto numberAt = [&](int index, double &out) {
            if (index >= arguments.size() || arguments[index].text)
                return false;
            out = arguments[index].number;
            return true;
        };
        const auto textAt = [&](int index, QString &out) {
            if (index >= arguments.size() || !arguments[index].text)
                return false;
            out = arguments[index].string;
            return true;
        };
        double a = 0, b = 0, c = 0;
        QString at;
        if (name == "ABS" || name == "SGN" || name == "INT" || name == "SQR" || name == "SIN" ||
            name == "COS" || name == "TAN" || name == "ATN" || name == "EXP" || name == "LOG") {
            if (!numberAt(0, a)) {
                error = errorFor("?TYPE MISMATCH ERROR");
                return false;
            }
            if (name == "ABS")
                value = Value::of(std::fabs(a));
            else if (name == "SGN")
                value = Value::of(a < 0 ? -1 : a > 0 ? 1 : 0);
            else if (name == "INT")
                value = Value::of(std::floor(a));
            else if (name == "SQR") {
                if (a < 0) {
                    error = errorFor("?ILLEGAL QUANTITY ERROR");
                    return false;
                }
                value = Value::of(std::sqrt(a));
            } else if (name == "SIN")
                value = Value::of(std::sin(a));
            else if (name == "COS")
                value = Value::of(std::cos(a));
            else if (name == "TAN")
                value = Value::of(std::tan(a));
            else if (name == "ATN")
                value = Value::of(std::atan(a));
            else if (name == "EXP")
                value = Value::of(std::exp(a));
            else {
                if (a <= 0) {
                    error = errorFor("?ILLEGAL QUANTITY ERROR");
                    return false;
                }
                value = Value::of(std::log(a));
            }
            return true;
        }
        if (name == "LEN") {
            if (!textAt(0, at)) {
                error = errorFor("?TYPE MISMATCH ERROR");
                return false;
            }
            value = Value::of(at.size());
            return true;
        }
        if (name == "VAL") {
            if (!textAt(0, at)) {
                error = errorFor("?TYPE MISMATCH ERROR");
                return false;
            }
            bool ok = false;
            const double parsed = at.trimmed().toDouble(&ok);
            value = Value::of(ok ? parsed : 0);
            return true;
        }
        if (name == "ASC") {
            if (!textAt(0, at) || at.isEmpty()) {
                error = errorFor("?ILLEGAL QUANTITY ERROR");
                return false;
            }
            value = Value::of(at.at(0).unicode());
            return true;
        }
        if (name == "CHR$") {
            if (!numberAt(0, a)) {
                error = errorFor("?TYPE MISMATCH ERROR");
                return false;
            }
            value = Value::of(QString(QChar(int(a) & 0xFFFF)));
            return true;
        }
        if (name == "STR$") {
            if (!numberAt(0, a)) {
                error = errorFor("?TYPE MISMATCH ERROR");
                return false;
            }
            value = Value::of(trimmedNumber(a));
            return true;
        }
        if (name == "SPACE$") {
            if (!numberAt(0, a) || a < 0 || a > 255) {
                error = errorFor("?ILLEGAL QUANTITY ERROR");
                return false;
            }
            value = Value::of(QString(int(a), ' '));
            return true;
        }
        if (name == "STRING$") {
            QString character;
            if (!numberAt(0, a) || !textAt(1, character) || character.isEmpty() || a < 0 || a > 255) {
                error = errorFor("?ILLEGAL QUANTITY ERROR");
                return false;
            }
            value = Value::of(QString(int(a), character.at(0)));
            return true;
        }
        if (name == "HEX$" || name == "OCT$") {
            if (!numberAt(0, a)) {
                error = errorFor("?TYPE MISMATCH ERROR");
                return false;
            }
            const qint64 integer = qint64(a);
            value = Value::of(QString::number(integer, name == "HEX$" ? 16 : 8).toUpper());
            return true;
        }
        if (name == "LEFT$" || name == "RIGHT$") {
            if (!textAt(0, at) || !numberAt(1, a)) {
                error = errorFor("?TYPE MISMATCH ERROR");
                return false;
            }
            const int count = int(qBound(0.0, a, double(at.size())));
            value = Value::of(name == "LEFT$" ? at.left(count) : at.right(count));
            return true;
        }
        if (name == "MID$") {
            if (!textAt(0, at) || !numberAt(1, a)) {
                error = errorFor("?TYPE MISMATCH ERROR");
                return false;
            }
            int start = int(a);
            if (start < 1) {
                error = errorFor("?ILLEGAL QUANTITY ERROR");
                return false;
            }
            int count = at.size();
            if (arguments.size() >= 3 && !numberAt(2, c)) {
                error = errorFor("?TYPE MISMATCH ERROR");
                return false;
            }
            if (arguments.size() >= 3)
                count = int(qMax(0.0, c));
            Q_UNUSED(b);
            value = Value::of(at.mid(start - 1, count));
            return true;
        }
        error = errorFor("?SYNTAX ERROR");
        return false;
    }

  public:
    bool arrayValue(const QString &name, const QVector<int> &indices, Value &value, QString &error)
    {
        const auto array = m_basic.m_arrays.constFind(name);
        if (array == m_basic.m_arrays.constEnd()) {
            error = errorFor("?UNDIMENSIONED ARRAY ERROR");
            return false;
        }
        const int slot = position(*array, indices);
        if (slot < 0) {
            error = errorFor("?SUBSCRIPT OUT OF RANGE");
            return false;
        }
        if (array->text)
            value = Value::of(array->strings.value(slot));
        else
            value = Value::of(array->numbers.value(slot));
        return true;
    }

    static int position(const ArrayFrame &frame, const QVector<int> &indices)
    {
        if (indices.size() != frame.dimensions.size())
            return -1;
        int position = 0;
        for (int i = 0; i < indices.size(); ++i) {
            if (indices[i] < 0 || indices[i] > frame.dimensions[i])
                return -1;
            position = position * (frame.dimensions[i] + 1) + indices[i];
        }
        return position;
    }
};

// ---------------------------------------------------------------- utilities

Basic::Basic() = default;

QString Basic::Value::display() const
{
    return text ? string : trimmedNumber(number);
}

void Basic::writeOutput(const QString &text)
{
    if (m_truncated)
        return;
    const QByteArray bytes = text.toUtf8();
    if (m_output.size() + bytes.size() > Limits::OutputBytes) {
        const int room = qMax(0, Limits::OutputBytes - m_output.size());
        m_output += bytes.left(room);
        m_output += "\n[output truncated]\n";
        m_truncated = true;
        return;
    }
    m_output += bytes;
    for (QChar ch : text) {
        if (ch == '\n')
            m_column = 0;
        else if (ch == '\t')
            m_column = (m_column / 8 + 1) * 8;
        else
            ++m_column;
    }
}

void Basic::writeLine(const QString &text)
{
    writeOutput(text);
    newline();
}

void Basic::newline()
{
    if (m_truncated)
        return;
    m_output += '\n';
    m_column = 0;
}

void Basic::finish()
{
    // A program that already ended its last line does not need another one.
    if (!m_exited && !m_truncated && m_column > 0)
        m_output += '\n';
}

void Basic::resetState(bool keepProgram)
{
    if (!keepProgram)
        m_lines.clear();
    m_variables.clear();
    m_arrays.clear();
    m_loops.clear();
    m_gosub.clear();
    m_data.clear();
    m_dataIndex = 0;
    m_output.clear();
    m_column = 0;
    m_instructions = 0;
    m_truncated = false;
    m_exited = false;
    m_inputPending = false;
    m_inputTarget.clear();
    m_jumpPc = -1;
    m_jumpStmt = 0;
}

QString Basic::source() const
{
    QStringList rows;
    for (auto it = m_lines.constBegin(); it != m_lines.constEnd(); ++it)
        rows << QString::number(it.key()) + " " + it.value();
    return rows.join('\n');
}

bool Basic::load(const QByteArray &source, QString *error)
{
    const auto fail = [error](const QString &text) {
        if (error)
            *error = text;
        return false;
    };
    if (source.size() > Limits::SourceBytes)
        return fail("?PROGRAM FILE TOO LARGE");
    QMap<int, QString> parsed;
    const QString text = QString::fromUtf8(source);
    int lineIndex = 0;
    for (const QString &raw : text.split('\n')) {
        ++lineIndex;
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        if (!line.at(0).isDigit())
            return fail(QString("?SYNTAX ERROR IN FILE LINE %1").arg(lineIndex));
        qsizetype end = 0;
        while (end < line.size() && line[end].isDigit())
            ++end;
        bool ok = false;
        const int number = line.left(end).toInt(&ok);
        if (!ok || number < 0 || number > 63999)
            return fail(QString("?LINE NUMBER OUT OF RANGE IN FILE LINE %1").arg(lineIndex));
        const QString body = line.mid(end).trimmed();
        if (body.isEmpty())
            return fail(QString("?SYNTAX ERROR IN FILE LINE %1").arg(lineIndex));
        if (body.size() > Limits::LineLength)
            return fail(QString("?LINE TOO LONG IN FILE LINE %1").arg(lineIndex));
        if (!parsed.contains(number) && parsed.size() >= Limits::ProgramLines)
            return fail("?PROGRAM FULL");
        parsed.insert(number, body);
    }
    if (parsed.isEmpty())
        return fail("?EMPTY PROGRAM FILE");
    m_lines = parsed;
    return true;
}

QString Basic::programLine(int number) const { return m_lines.value(number); }

bool Basic::setProgramLine(int number, const QString &body, QString *error)
{
    const auto fail = [error](const QString &text) {
        if (error)
            *error = text;
        return false;
    };
    if (number < 0 || number > 63999)
        return fail("?LINE NUMBER OUT OF RANGE");
    if (body.size() > Limits::LineLength)
        return fail("?LINE TOO LONG");
    const QString trimmed = body.trimmed();
    if (trimmed.isEmpty())
        m_lines.remove(number);
    else {
        if (!m_lines.contains(number) && m_lines.size() >= Limits::ProgramLines)
            return fail("?PROGRAM FULL");
        m_lines.insert(number, trimmed);
    }
    return true;
}

void Basic::removeProgramLine(int number) { m_lines.remove(number); }

void Basic::newProgram()
{
    resetState(false);
    m_output.clear();
}

QString Basic::submit(const QString &line)
{
    m_output.clear();
    m_column = 0;
    m_truncated = false;
    m_exited = false;
    if (line.trimmed().isEmpty())
        return {};

    if (m_inputPending) {
        m_inputPending = false;
        Value value;
        if (m_inputText) {
            QString raw = line.trimmed();
            if (raw.size() >= 2 && (raw.startsWith('"') || raw.startsWith('\'')))
                raw = raw.mid(1, raw.size() - 2);
            value = Value::of(raw);
        } else {
            QString error;
            if (!Parser(*this, line, 0).expression(value, error)) {
                m_inputPending = true;
                return "?REDO FROM START\n" + QStringLiteral("? ");
            }
            if (value.text)
                value = Value::of(value.string.toDouble());
        }
        m_variables.insert(m_inputTarget, value);
        runFrom(m_resumeLine, m_resumeStatement);
        return QString::fromUtf8(m_output);
    }

    // A leading number stores or deletes a program line.
    qsizetype index = 0;
    while (index < line.size() && line[index].isSpace())
        ++index;
    if (index < line.size() && line[index].isDigit()) {
        qsizetype start = index;
        while (index < line.size() && line[index].isDigit())
            ++index;
        const int number = line.mid(start, index - start).toInt();
        const QString body = line.mid(index).trimmed();
        QString error;
        if (!setProgramLine(number, body, &error))
            return error;
        return {};
    }

    const QString statement = line.trimmed();
    const QString keyword = statement.section(QRegularExpression("\\s+"), 0, 0).toUpper();
    if (keyword == "RUN") {
        return run();
    }
    if (keyword == "LIST") {
        const QString range = statement.mid(4).trimmed();
        QStringList rows;
        if (range.isEmpty()) {
            for (auto it = m_lines.constBegin(); it != m_lines.constEnd(); ++it)
                rows << QString::number(it.key()) + " " + it.value();
        } else if (range.contains('-')) {
            bool okFirst = true, okLast = true;
            const int first = range.section('-', 0, 0).trimmed().toInt(&okFirst);
            const QString tail = range.section('-', 1, 1).trimmed();
            const int last = tail.isEmpty() ? 63999 : tail.toInt(&okLast);
            if (!okFirst || !okLast)
                return "?SYNTAX ERROR";
            for (auto it = m_lines.constBegin(); it != m_lines.constEnd(); ++it)
                if (it.key() >= first && it.key() <= last)
                    rows << QString::number(it.key()) + " " + it.value();
        } else {
            bool ok = false;
            const int number = range.toInt(&ok);
            if (!ok)
                return "?SYNTAX ERROR";
            if (m_lines.contains(number))
                rows << QString::number(number) + " " + m_lines.value(number);
        }
        return rows.isEmpty() ? QStringLiteral("?EMPTY PROGRAM") : rows.join('\n');
    }
    if (keyword == "NEW") {
        newProgram();
        return {};
    }
    if (keyword == "SAVE" || keyword == "LOAD") {
        executeDirect(statement);
        return QString::fromUtf8(m_output);
    }
    if (keyword == "CONT")
        return QStringLiteral("?CAN'T CONTINUE");
    if (keyword == "SYSTEM" || keyword == "EXIT" || keyword == "BYE") {
        m_exited = true;
        return QStringLiteral("Leaving A16 BASIC.");
    }

    return executeDirect(statement);
}

QString Basic::executeDirect(const QString &statement)
{
    m_output.clear();
    m_column = 0;
    m_truncated = false;
    const QStringList statements = splitStatements(statement);
    m_program.clear();
    for (const auto &part : statements) {
        m_jumpPc = -1;
        bool jumped = false;
        executeStatement(part, 0, 0, 0, jumped);
        if (m_jumpPc >= 0)
            break;
        if (m_inputPending)
            break;
    }
    const QString text = QString::fromUtf8(m_output);
    return text;
}

QString Basic::run()
{
    m_output.clear();
    m_column = 0;
    m_instructions = 0;
    m_truncated = false;
    m_exited = false;
    m_inputPending = false;
    m_variables.clear();
    m_arrays.clear();
    m_loops.clear();
    m_gosub.clear();
    m_program.clear();
    for (auto it = m_lines.constBegin(); it != m_lines.constEnd(); ++it)
        m_program.append({it.key(), splitStatements(it.value())});
    parseData();
    m_dataIndex = 0;
    m_jumpPc = -1;
    runFrom(0, 0);
    if (m_truncated && !m_output.endsWith('\n'))
        m_output += '\n';
    return QString::fromUtf8(m_output);
}

int Basic::findLine(int number) const
{
    for (int i = 0; i < m_program.size(); ++i)
        if (m_program[i].number == number)
            return i;
    return -1;
}

void Basic::parseData()
{
    m_data.clear();
    for (const auto &line : m_program) {
        for (const auto &statement : line.statements) {
            if (!statement.startsWith("DATA", Qt::CaseInsensitive))
                continue;
            QString body = statement.mid(4);
            QString current;
            QChar quote;
            for (QChar ch : body) {
                if (!quote.isNull()) {
                    if (ch == quote)
                        quote = {};
                    else
                        current += ch;
                    continue;
                }
                if (ch == '"' || ch == '\'') {
                    quote = ch;
                    continue;
                }
                if (ch == ',') {
                    m_data << current.trimmed();
                    current.clear();
                    continue;
                }
                current += ch;
            }
            m_data << current.trimmed();
        }
    }
}

void Basic::runFrom(int pc, int statement)
{
    if (m_program.isEmpty()) {
        writeLine("?EMPTY PROGRAM");
        return;
    }
    const auto normalise = [this](int &p, int &s) {
        while (p < m_program.size() && s >= m_program[p].statements.size()) {
            ++p;
            s = 0;
        }
    };
    normalise(pc, statement);
    while (pc < m_program.size()) {
        if (++m_instructions > Limits::Instructions) {
            writeLine("?PROGRAM TOOK TOO LONG - HALTED");
            return;
        }
        const Program &line = m_program[pc];
        if (statement >= line.statements.size()) {
            ++pc;
            statement = 0;
            continue;
        }
        const QString text = line.statements[statement];
        m_jumpPc = -1;
        bool jumped = false;
        executeStatement(text, line.number, pc, statement, jumped);
        if (m_truncated)
            return;
        if (m_inputPending) {
            m_resumeLine = pc;
            m_resumeStatement = statement + 1;
            return;
        }
        if (m_exited)
            return;
        if (m_jumpPc >= 0) {
            pc = m_jumpPc;
            statement = m_jumpStmt;
            m_jumpPc = -1;
            normalise(pc, statement);
            continue;
        }
        ++statement;
        normalise(pc, statement);
    }
    finish();
}

void Basic::executeStatement(const QString &rawStatement, int lineNumber, int lineIndex, int statementIndex,
                             bool &jumped)
{
    Q_UNUSED(jumped);
    QString statement = rawStatement.trimmed();
    if (statement.isEmpty())
        return;
    const auto fail = [&](const QString &message) {
        writeLine(message.contains(" IN ") ? message
                                         : message + QStringLiteral(" IN ") + QString::number(lineNumber));
        m_jumpPc = m_program.size();
        m_jumpStmt = 0;
    };

    const auto upperFirst = [&](const QString &text) {
        qsizetype split = 0;
        while (split < text.size() && !text[split].isSpace())
            ++split;
        return text.mid(0, split).toUpper();
    };
    QString keyword = upperFirst(statement);
    if (keyword == "REM" || statement.startsWith('\''))
        return;
    if (keyword == "DATA")
        return;
    if (keyword == "ELSE")
        return;

    // Assignment without LET.
    static const QRegularExpression assignment(
        QStringLiteral("^([A-Za-z][A-Za-z0-9]*\\$?)\\s*(\\([^)]*\\))?\\s*=\\s*(.+)$"));
    if (keyword != "LET") {
        const auto match = assignment.match(statement);
        if (match.hasMatch() && !QStringList{"IF", "FOR", "PRINT", "INPUT", "DIM", "ON"}.contains(keyword)) {
            keyword = "LET";
        }
    }

    if (keyword == "LET") {
        QString body = statement.mid(3).trimmed();
        const auto match = assignment.match(body);
        if (!match.hasMatch()) {
            fail("?SYNTAX ERROR");
            return;
        }
        const QString name = match.captured(1).toUpper();
        const QString indexText = match.captured(2);
        const QString expression = match.captured(3);
        Value value;
        QString error;
        if (!Parser(*this, expression, lineNumber).expression(value, error)) {
            fail(error);
            return;
        }
        if (indexText.isEmpty()) {
            if (name.endsWith('$')) {
                if (!value.text)
                    value = Value::of(value.display());
                m_variables.insert(name, value);
            } else {
                if (value.text) {
                    fail("?TYPE MISMATCH ERROR IN " + QString::number(lineNumber));
                    return;
                }
                m_variables.insert(name, value);
            }
            return;
        }
        QVector<int> indices;
        Parser indexParser(*this, indexText, lineNumber);
        if (!indexParser.indexList(indices, error)) {
            fail(error);
            return;
        }
        auto array = m_arrays.find(name);
        if (array == m_arrays.end()) {
            fail("?UNDIMENSIONED ARRAY ERROR IN " + QString::number(lineNumber));
            return;
        }
        const int position = Parser::position(*array, indices);
        if (position < 0) {
            fail("?SUBSCRIPT OUT OF RANGE IN " + QString::number(lineNumber));
            return;
        }
        if (array->text) {
            array->strings[position] = value.display();
        } else {
            if (value.text) {
                fail("?TYPE MISMATCH ERROR IN " + QString::number(lineNumber));
                return;
            }
            array->numbers[position] = value.number;
        }
        return;
    }

    if (keyword == "PRINT" || keyword == "?") {
        const QString body = keyword == "PRINT" ? statement.mid(5) : statement.mid(1);
        if (body.trimmed().isEmpty()) {
            newline();
            return;
        }
        // Split the list into (item, separator that follows it) pairs.
        QVector<QPair<QString, QChar>> parts;
        QString current;
        QChar quote;
        int depth = 0;
        bool trailingSeparator = false;
        for (qsizetype i = 0; i < body.size(); ++i) {
            const QChar ch = body[i];
            if (!quote.isNull()) {
                current += ch;
                if (ch == quote)
                    quote = {};
                continue;
            }
            if (ch == '"' || ch == '\'') {
                quote = ch;
                current += ch;
                continue;
            }
            if (ch == '(')
                ++depth;
            else if (ch == ')')
                depth = qMax(0, depth - 1);
            if (depth == 0 && (ch == ';' || ch == ',')) {
                parts.append({current, ch});
                current.clear();
                trailingSeparator = true;
                continue;
            }
            current += ch;
            trailingSeparator = false;
        }
        if (!current.trimmed().isEmpty() || !trailingSeparator)
            parts.append({current, QChar()});
        const bool endsWithSeparator = !parts.isEmpty() && !parts.last().second.isNull();
        for (const auto &part : parts) {
            const QString item = part.first.trimmed();
            if (!item.isEmpty()) {
                if (item.startsWith("TAB(", Qt::CaseInsensitive) && item.endsWith(')')) {
                    Value position;
                    QString error;
                    if (!Parser(*this, item.mid(4, item.size() - 5), lineNumber).expression(position, error) ||
                        position.text) {
                        fail(error.isEmpty() ? "?TYPE MISMATCH ERROR IN " + QString::number(lineNumber) : error);
                        return;
                    }
                    const int column = qBound(0, int(position.number), 255);
                    if (column > m_column)
                        writeOutput(QString(column - m_column, ' '));
                } else {
                    Value value;
                    QString error;
                    if (!Parser(*this, item, lineNumber).expression(value, error)) {
                        fail(error);
                        return;
                    }
                    const QString text = value.display();
                    writeOutput(value.text ? text : (value.number >= 0 ? " " + text : text));
                }
            }
            if (part.second == ',')
                writeOutput(QString(8 - (m_column % 8), ' '));
        }
        if (!endsWithSeparator)
            newline();
        return;
    }

    if (keyword == "INPUT") {
        QString body = statement.mid(5).trimmed();
        QString prompt;
        if (body.startsWith('"') || body.startsWith('\'')) {
            const QChar quote = body.at(0);
            const int end = body.indexOf(quote, 1);
            if (end < 0) {
                fail("?SYNTAX ERROR IN " + QString::number(lineNumber));
                return;
            }
            prompt = body.mid(1, end - 1);
            body = body.mid(end + 1).trimmed();
            if (body.startsWith(';') || body.startsWith(','))
                body = body.mid(1).trimmed();
        }
        const QString name = body.toUpper();
        static const QRegularExpression variable(QStringLiteral("^[A-Za-z][A-Za-z0-9]*\\$?$"));
        if (!variable.match(name).hasMatch()) {
            fail("?SYNTAX ERROR IN " + QString::number(lineNumber));
            return;
        }
        writeOutput(prompt.isEmpty() ? QStringLiteral("? ") : prompt + "? ");
        m_inputPending = true;
        m_inputTarget = name;
        m_inputText = name.endsWith('$');
        m_resumeLine = lineIndex;
        m_resumeStatement = statementIndex + 1;
        return;
    }

    if (keyword == "IF") {
        const QString body = statement.mid(2).trimmed();
        const int thenIndex = findWord(body, "THEN");
        if (thenIndex < 0) {
            fail("?SYNTAX ERROR IN " + QString::number(lineNumber));
            return;
        }
        const QString condition = body.left(thenIndex);
        QString rest = body.mid(thenIndex + 4).trimmed();
        Value value;
        QString error;
        if (!Parser(*this, condition, lineNumber).expression(value, error)) {
            fail(error);
            return;
        }
        const bool truth = value.text ? !value.string.isEmpty() : value.number != 0;
        if (!truth) {
            const int elseIndex = findWord(rest, "ELSE");
            if (elseIndex < 0)
                return;
            rest = rest.mid(elseIndex + 4).trimmed();
            if (rest.isEmpty()) {
                fail("?SYNTAX ERROR IN " + QString::number(lineNumber));
                return;
            }
        }
        // "THEN 100" means GOTO 100.
        static const QRegularExpression lineNumberPattern(QStringLiteral("^[0-9]+$"));
        if (lineNumberPattern.match(rest).hasMatch()) {
            const int target = findLine(rest.toInt());
            if (target < 0) {
                fail("?LINE " + rest + " NOT FOUND");
                return;
            }
            m_jumpPc = target;
            m_jumpStmt = 0;
            return;
        }
        executeStatement(rest, lineNumber, lineIndex, statementIndex, jumped);
        return;
    }

    if (keyword == "GOTO") {
        bool ok = false;
        const int target = statement.mid(4).trimmed().toInt(&ok);
        if (!ok) {
            fail("?SYNTAX ERROR IN " + QString::number(lineNumber));
            return;
        }
        const int index = findLine(target);
        if (index < 0) {
            fail("?LINE " + QString::number(target) + " NOT FOUND");
            return;
        }
        m_jumpPc = index;
        m_jumpStmt = 0;
        return;
    }

    if (keyword == "GOSUB") {
        bool ok = false;
        const int target = statement.mid(5).trimmed().toInt(&ok);
        if (!ok) {
            fail("?SYNTAX ERROR IN " + QString::number(lineNumber));
            return;
        }
        const int index = findLine(target);
        if (index < 0) {
            fail("?LINE " + QString::number(target) + " NOT FOUND");
            return;
        }
        if (m_gosub.size() > 64) {
            fail("?OUT OF MEMORY ERROR IN " + QString::number(lineNumber));
            return;
        }
        // Store the return position as a single encoded value (line, statement).
        m_gosub.append(lineIndex * 1000 + qMin(999, statementIndex + 1));
        m_jumpPc = index;
        m_jumpStmt = 0;
        return;
    }

    if (keyword == "RETURN") {
        if (m_gosub.isEmpty()) {
            fail("?RETURN WITHOUT GOSUB IN " + QString::number(lineNumber));
            return;
        }
        const int encoded = m_gosub.takeLast();
        m_jumpPc = encoded / 1000;
        m_jumpStmt = encoded % 1000;
        return;
    }

    if (keyword == "FOR") {
        const QString body = statement.mid(3);
        const int equals = body.indexOf('=');
        if (equals < 0) {
            fail("?SYNTAX ERROR IN " + QString::number(lineNumber));
            return;
        }
        const QString name = body.left(equals).trimmed().toUpper();
        const QString remainder = body.mid(equals + 1);
        const int toIndex = findWord(remainder, "TO");
        if (toIndex < 0) {
            fail("?SYNTAX ERROR IN " + QString::number(lineNumber));
            return;
        }
        const QString startText = remainder.left(toIndex);
        QString rest = remainder.mid(toIndex + 2);
        QString stepText = "1";
        const int stepIndex = findWord(rest, "STEP");
        if (stepIndex >= 0) {
            stepText = rest.mid(stepIndex + 4);
            rest = rest.left(stepIndex);
        }
        Value start, limit, step;
        QString error;
        if (!Parser(*this, startText, lineNumber).expression(start, error) ||
            !Parser(*this, rest, lineNumber).expression(limit, error) ||
            !Parser(*this, stepText, lineNumber).expression(step, error)) {
            fail(error.isEmpty() ? "?SYNTAX ERROR IN " + QString::number(lineNumber) : error);
            return;
        }
        if (start.text || limit.text || step.text || name.endsWith('$')) {
            fail("?TYPE MISMATCH ERROR IN " + QString::number(lineNumber));
            return;
        }
        if (step.number == 0) {
            fail("?ILLEGAL QUANTITY ERROR IN " + QString::number(lineNumber));
            return;
        }
        m_variables.insert(name, start);
        LoopFrame frame;
        frame.variable = name;
        frame.limit = limit.number;
        frame.step = step.number;
        // Loop body starts after the FOR statement.
        if (statementIndex + 1 < m_program.value(lineIndex).statements.size()) {
            frame.lineIndex = lineIndex;
            frame.statement = statementIndex + 1;
        } else {
            frame.lineIndex = lineIndex + 1;
            frame.statement = 0;
        }
        frame.used = true;
        if (!m_loops.isEmpty() && m_loops.last().variable == name)
            m_loops.removeLast();
        m_loops.append(frame);
        return;
    }

    if (keyword == "NEXT") {
        const QString name = statement.mid(4).trimmed().toUpper();
        int index = m_loops.size() - 1;
        if (!name.isEmpty()) {
            while (index >= 0 && m_loops[index].variable != name)
                --index;
            if (index < 0) {
                fail("?NEXT WITHOUT FOR IN " + QString::number(lineNumber));
                return;
            }
        } else if (index < 0) {
            fail("?NEXT WITHOUT FOR IN " + QString::number(lineNumber));
            return;
        }
        LoopFrame &frame = m_loops[index];
        Value value = m_variables.value(frame.variable);
        value.number += frame.step;
        m_variables.insert(frame.variable, value);
        const bool again = frame.step > 0 ? value.number <= frame.limit : value.number >= frame.limit;
        if (again) {
            m_jumpPc = frame.lineIndex;
            m_jumpStmt = frame.statement;
            return;
        }
        m_loops.removeAt(index);
        return;
    }

    if (keyword == "DIM") {
        const QString body = statement.mid(3).trimmed();
        const int open = body.indexOf('(');
        const int close = body.lastIndexOf(')');
        if (open < 0 || close < open) {
            fail("?SYNTAX ERROR IN " + QString::number(lineNumber));
            return;
        }
        const QString name = body.left(open).trimmed().toUpper();
        const QStringList parts = body.mid(open + 1, close - open - 1).split(',');
        QVector<int> dimensions;
        qint64 total = 1;
        for (const auto &part : parts) {
            Value value;
            QString error;
            if (!Parser(*this, part, lineNumber).expression(value, error) || value.text) {
                fail(error.isEmpty() ? "?SYNTAX ERROR IN " + QString::number(lineNumber) : error);
                return;
            }
            const int size = int(value.number);
            if (size < 0 || size > 255) {
                fail("?ILLEGAL QUANTITY ERROR IN " + QString::number(lineNumber));
                return;
            }
            dimensions << size;
            total *= (size + 1);
        }
        if (total > Limits::ArrayElements) {
            fail("?OUT OF MEMORY ERROR IN " + QString::number(lineNumber));
            return;
        }
        // Already dimensioned: classic BASIC rejects the second DIM.
        if (m_arrays.contains(name)) {
            fail("?REDIMENSIONED ARRAY ERROR IN " + QString::number(lineNumber));
            return;
        }
        ArrayFrame frame;
        frame.name = name;
        frame.text = name.endsWith('$');
        frame.dimensions = dimensions;
        if (frame.text)
            frame.strings = QVector<QString>(int(total));
        else
            frame.numbers = QVector<double>(int(total), 0);
        m_arrays.insert(name, frame);
        return;
    }

    if (keyword == "READ") {
        const QString body = statement.mid(4);
        const QStringList names = body.split(',');
        for (const auto &raw : names) {
            const QString item = raw.trimmed();
            if (item.isEmpty())
                continue;
            QString name = item;
            QString indexText;
            const int open = item.indexOf('(');
            if (open > 0 && item.endsWith(')')) {
                name = item.left(open);
                indexText = item.mid(open);
            }
            name = name.toUpper();
            if (m_dataIndex >= m_data.size()) {
                fail("?OUT OF DATA ERROR IN " + QString::number(lineNumber));
                return;
            }
            const QString text = m_data[m_dataIndex++];
            Value value;
            if (name.endsWith('$'))
                value = Value::of(text);
            else {
                bool ok = false;
                const double number = text.toDouble(&ok);
                // Classic BASIC reads a missing value as zero instead of failing.
                value = Value::of(ok ? number : 0);
            }
            if (indexText.isEmpty()) {
                m_variables.insert(name, value);
                continue;
            }
            QVector<int> indices;
            QString error;
            if (!Parser(*this, indexText, lineNumber).indexList(indices, error)) {
                fail(error);
                return;
            }
            auto array = m_arrays.find(name);
            if (array == m_arrays.end()) {
                fail("?UNDIMENSIONED ARRAY ERROR IN " + QString::number(lineNumber));
                return;
            }
            const int slot = Parser::position(*array, indices);
            if (slot < 0) {
                fail("?SUBSCRIPT OUT OF RANGE IN " + QString::number(lineNumber));
                return;
            }
            if (array->text)
                array->strings[slot] = value.display();
            else if (value.text)
                fail("?TYPE MISMATCH ERROR IN " + QString::number(lineNumber));
            else
                array->numbers[slot] = value.number;
            if (m_jumpPc >= 0)
                return;
        }
        return;
    }

    if (keyword == "RESTORE") {
        m_dataIndex = 0;
        return;
    }

    if (keyword == "END" || keyword == "STOP") {
        m_jumpPc = m_program.size();
        if (keyword == "STOP")
            writeLine("BREAK IN " + QString::number(lineNumber));
        return;
    }

    if (keyword == "CLS") {
        writeOutput(QString::fromUtf8(ClsMarker));
        m_column = 0;
        return;
    }

    if (keyword == "BEEP") {
        if (m_sound)
            m_sound("beep");
        return;
    }

    if (keyword == "SOUND" || keyword == "PLAY") {
        if (m_sound)
            m_sound("sound");
        return;
    }

    if (keyword == "RANDOMIZE") {
        return;
    }

    if (keyword == "SYSTEM" || keyword == "EXIT" || keyword == "BYE") {
        m_exited = true;
        m_jumpPc = m_program.size();
        return;
    }

    if (keyword == "SAVE" || keyword == "LOAD") {
        const QString argument = statement.mid(4).trimmed();
        if (argument.isEmpty()) {
            fail("?MISSING FILE NAME IN " + QString::number(lineNumber));
            return;
        }
        const QString name = unquote(argument);
        if (keyword == "SAVE") {
            if (!m_writeFile) {
                fail("?DEVICE UNAVAILABLE IN " + QString::number(lineNumber));
                return;
            }
            if (!m_writeFile(name, source().toUtf8())) {
                fail("?SAVE FAILED IN " + QString::number(lineNumber));
                return;
            }
        } else {
            if (!m_readFile) {
                fail("?DEVICE UNAVAILABLE IN " + QString::number(lineNumber));
                return;
            }
            QString error;
            if (!load(m_readFile(name), &error)) {
                fail(error + " IN " + QString::number(lineNumber));
                return;
            }
        }
        return;
    }

    if (keyword == "TRON" || keyword == "TROFF")
        return;

    fail("?SYNTAX ERROR");
}
