/*
* Audacity: A Digital Audio Editor
*/
#include "dubbingjsonreader.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>

#include "log.h"

using namespace au::dubbing;

namespace {
//! Упорядоченное дерево ключей объектов (значения не хранятся):
//! порядок пар = порядку появления ключей в исходном тексте JSON.
struct OrderedObject;
using OrderedItems = std::vector<std::pair<std::string, OrderedObject> >;
struct OrderedObject
{
    OrderedItems items;
};

/*!
 * Структурный сканер JSON: обходит объекты/массивы, уважая кавычки строк,
 * и строит OrderedObject-дерево. Значения (строки/числа/литералы/массивы)
 * пропускаются — их читает QJsonDocument.
 */
class OrderScanner
{
public:
    explicit OrderScanner(const QByteArray& raw)
        : m_raw(raw) {}

    bool parse(OrderedObject& out, std::string& error)
    {
        skipWs();
        if (m_pos >= m_raw.size() || m_raw.at(m_pos) != '{') {
            error = "корень JSON должен быть объектом";
            return false;
        }
        if (!parseObject(out, error)) {
            return false;
        }
        skipWs();
        if (m_pos < m_raw.size()) {
            error = "лишние данные после корневого объекта";
            return false;
        }
        return true;
    }

private:
    void skipWs()
    {
        while (m_pos < m_raw.size()) {
            const char c = m_raw.at(m_pos);
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++m_pos;
            } else {
                break;
            }
        }
    }

    //! Читает строку JSON, начиная с открывающей кавычки.
    bool parseString(QString& out, std::string& error)
    {
        if (m_pos >= m_raw.size() || m_raw.at(m_pos) != '"') {
            error = "ожидалась строка (кавычка)";
            return false;
        }
        ++m_pos;
        QString result;
        QByteArray utf8;
        auto flushUtf8 = [&]() {
            if (!utf8.isEmpty()) {
                result += QString::fromUtf8(utf8);
                utf8.clear();
            }
        };
        while (m_pos < m_raw.size()) {
            const char c = m_raw.at(m_pos);
            if (c == '"') {
                ++m_pos;
                flushUtf8();
                out = result;
                return true;
            }
            if (c == '\\') {
                flushUtf8();
                ++m_pos;
                if (m_pos >= m_raw.size()) {
                    error = "обрыв экранированной последовательности";
                    return false;
                }
                const char e = m_raw.at(m_pos++);
                switch (e) {
                case '"': result += QLatin1Char('"'); break;
                case '\\': result += QLatin1Char('\\'); break;
                case '/': result += QLatin1Char('/'); break;
                case 'b': result += QLatin1Char('\b'); break;
                case 'f': result += QLatin1Char('\f'); break;
                case 'n': result += QLatin1Char('\n'); break;
                case 'r': result += QLatin1Char('\r'); break;
                case 't': result += QLatin1Char('\t'); break;
                case 'u': {
                    if (m_pos + 4 > m_raw.size()) {
                        error = "обрыв \\u-последовательности";
                        return false;
                    }
                    const QString hex = QString::fromLatin1(m_raw.mid(m_pos, 4));
                    bool ok = false;
                    const uint code = hex.toUInt(&ok, 16);
                    if (!ok) {
                        error = "неверная \\u-последовательность";
                        return false;
                    }
                    m_pos += 4;
                    result += QChar(static_cast<int>(code)); //!< суррогатные пары склеивает QString
                    break;
                }
                default:
                    error = "неизвестное экранирование в строке";
                    return false;
                }
                continue;
            }
            utf8.append(c);
            ++m_pos;
        }
        error = "незакрытая строка";
        return false;
    }

    //! Пропускает значение любого типа (строку, число, литерал, массив, объект).
    bool skipValue(std::string& error)
    {
        if (m_pos >= m_raw.size()) {
            error = "ожидалось значение";
            return false;
        }
        const char c = m_raw.at(m_pos);
        if (c == '"') {
            QString dummy;
            return parseString(dummy, error);
        }
        if (c == '{') {
            OrderedObject dummy;
            return parseObject(dummy, error);
        }
        if (c == '[') {
            return skipArray(error);
        }
        // число / true / false / null
        while (m_pos < m_raw.size()) {
            const char v = m_raw.at(m_pos);
            if (v == ',' || v == '}' || v == ']' || v == ' ' || v == '\t' || v == '\r' || v == '\n') {
                break;
            }
            ++m_pos;
        }
        return true;
    }

    bool skipArray(std::string& error)
    {
        ++m_pos; // '['
        skipWs();
        if (m_pos < m_raw.size() && m_raw.at(m_pos) == ']') {
            ++m_pos;
            return true;
        }
        while (m_pos < m_raw.size()) {
            if (!skipValue(error)) {
                return false;
            }
            skipWs();
            if (m_pos >= m_raw.size()) {
                error = "незакрытый массив";
                return false;
            }
            const char c = m_raw.at(m_pos);
            if (c == ',') {
                ++m_pos;
                skipWs();
                continue;
            }
            if (c == ']') {
                ++m_pos;
                return true;
            }
            error = "ожидалась ',' или ']' в массиве";
            return false;
        }
        error = "незакрытый массив";
        return false;
    }

    bool parseObject(OrderedObject& out, std::string& error)
    {
        ++m_pos; // '{'
        skipWs();
        if (m_pos < m_raw.size() && m_raw.at(m_pos) == '}') {
            ++m_pos;
            return true;
        }
        while (m_pos < m_raw.size()) {
            QString key;
            if (!parseString(key, error)) {
                return false;
            }
            skipWs();
            if (m_pos >= m_raw.size() || m_raw.at(m_pos) != ':') {
                error = "ожидалось ':' после ключа";
                return false;
            }
            ++m_pos;
            skipWs();
            OrderedObject child;
            if (m_pos < m_raw.size() && m_raw.at(m_pos) == '{') {
                if (!parseObject(child, error)) {
                    return false;
                }
            } else if (!skipValue(error)) {
                return false;
            }
            out.items.emplace_back(key.toStdString(), std::move(child));
            skipWs();
            if (m_pos >= m_raw.size()) {
                error = "незакрытый объект";
                return false;
            }
            const char c = m_raw.at(m_pos);
            if (c == ',') {
                ++m_pos;
                skipWs();
                continue;
            }
            if (c == '}') {
                ++m_pos;
                return true;
            }
            error = "ожидалась ',' или '}' в объекте";
            return false;
        }
        error = "незакрытый объект";
        return false;
    }

    const QByteArray& m_raw;
    qsizetype m_pos = 0;
};

QJsonObject lineObject(const QJsonObject& root, const std::string& fileId, const std::string& questId, const std::string& guid)
{
    return root.value(QString::fromStdString(fileId)).toObject()
    .value(QString::fromStdString(questId)).toObject()
    .value(QString::fromStdString(guid)).toObject();
}

std::string stringField(const QJsonObject& obj, const char* key)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    return v.isString() ? v.toString().toStdString() : std::string();
}
}

bool DubbingJsonReader::read(const muse::io::path_t& path, std::vector<GameFile>& outFiles,
                             std::vector<std::string>& errors) const
{
    outFiles.clear();

    QFile file(path.toQString());
    if (!file.open(QIODevice::ReadOnly)) {
        errors.push_back("не удалось открыть файл: " + path.toString().toStdString());
        return false;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        errors.push_back("некорректный JSON: " + parseError.errorString().toStdString());
        return false;
    }
    const QJsonObject root = doc.object();

    OrderedObject orderedRoot;
    {
        OrderScanner scanner(raw);
        std::string scanError;
        if (!scanner.parse(orderedRoot, scanError)) {
            errors.push_back("ошибка структуры JSON: " + scanError);
            return false;
        }
    }

    for (const auto& [fileId, fileNode] : orderedRoot.items) {
        GameFile file;
        file.fileId = fileId;

        for (const auto& [questId, sceneNode] : fileNode.items) {
            Scene scene;
            scene.questId = questId;

            for (const auto& [guid, lineNode] : sceneNode.items) {
                if (lineNode.items.empty()) {
                    errors.push_back("реплика " + guid + ": значение должно быть объектом, пропущена");
                    continue;
                }

                const QJsonObject lineObj = lineObject(root, fileId, questId, guid);
                if (lineObj.isEmpty()) {
                    errors.push_back("реплика " + guid + ": не найдена в документе, пропущена");
                    continue;
                }

                const QJsonValue durValue = lineObj.value(QStringLiteral("dur"));
                if (!durValue.isDouble()) {
                    errors.push_back("реплика " + guid + ": поле dur отсутствует или не число, пропущена");
                    continue;
                }

                Line line;
                line.guid = guid;
                line.en = stringField(lineObj, "en");
                line.ru = stringField(lineObj, "ru");
                line.speakerInternal = stringField(lineObj, "speaker_internal");
                line.speakerName = stringField(lineObj, "speaker_name");
                if (line.speakerName.empty()) {
                    line.speakerName = "UNKNOWN"; //!< §6.2: пустое имя -> UNKNOWN
                }
                line.dur = durValue.toDouble();
                line.orderIndex = static_cast<int>(scene.lines.size());
                line.status = LineStatus::New;

                const bool duplicate = std::any_of(scene.lines.cbegin(), scene.lines.cend(),
                                                   [&line](const Line& l) { return l.guid == line.guid; });
                if (duplicate) {
                    errors.push_back("реплика " + guid + ": дубликат guid внутри сцены, пропущена");
                    continue;
                }
                scene.lines.push_back(std::move(line));
            }

            if (!scene.lines.empty()) {
                file.scenes.push_back(std::move(scene));
            }
        }

        outFiles.push_back(std::move(file));
    }

    LOGI() << "dubbing json read: " << outFiles.size() << " files, errors: " << errors.size();
    return true;
}
