#include "Json.h"
#include "Paths.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <unordered_map>

namespace json {

namespace {

const std::string kEmptyString;
const Value::Array kEmptyArray;
const Value::Object kEmptyObject;
const Value kNull;

constexpr int kMaxDepth = 256;
constexpr size_t kLinearKeys = 16;  // objects up to this size check duplicate keys by scanning

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

class Parser {
public:
    explicit Parser(std::string_view text) : s_(text) {}

    std::optional<Value> Run(std::string* error) {
        if (s_.size() >= 3 && s_.compare(0, 3, "\xEF\xBB\xBF") == 0) pos_ = 3;
        Value v;
        bool ok = ParseValue(v, 0);
        if (ok) {
            SkipSpace();
            if (pos_ != s_.size()) ok = Fail("unexpected trailing characters");
        }
        if (!ok) {
            if (error) *error = Where() + error_;
            return std::nullopt;
        }
        return v;
    }

private:
    bool Fail(const char* msg) {
        if (error_.empty()) error_ = msg;
        return false;
    }

    std::string Where() const {
        size_t line = 1, col = 1;
        for (size_t i = 0; i < pos_ && i < s_.size(); ++i) {
            if (s_[i] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        return "line " + std::to_string(line) + ", column " + std::to_string(col) + ": ";
    }

    void SkipSpace() {
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++pos_;
        }
    }

    bool Literal(std::string_view word) {
        if (s_.substr(pos_, word.size()) != word) return Fail("invalid literal");
        pos_ += word.size();
        return true;
    }

    bool ParseValue(Value& out, int depth) {
        if (depth > kMaxDepth) return Fail("nesting too deep");
        SkipSpace();
        if (pos_ >= s_.size()) return Fail("unexpected end of input");
        switch (s_[pos_]) {
            case 'n':
                out = Value();
                return Literal("null");
            case 't':
                out = Value(true);
                return Literal("true");
            case 'f':
                out = Value(false);
                return Literal("false");
            case '"': {
                std::string str;
                if (!ParseString(str)) return false;
                out = Value(std::move(str));
                return true;
            }
            case '[':
                return ParseArray(out, depth);
            case '{':
                return ParseObject(out, depth);
            default:
                return ParseNumber(out);
        }
    }

    bool ParseNumber(Value& out) {
        // Validate the strict JSON grammar first; from_chars is more permissive.
        size_t start = pos_;
        if (pos_ < s_.size() && s_[pos_] == '-') ++pos_;
        if (pos_ >= s_.size()) return Fail("invalid number");
        if (s_[pos_] == '0') {
            ++pos_;
        } else if (s_[pos_] >= '1' && s_[pos_] <= '9') {
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        } else {
            return Fail("unexpected character");
        }
        if (pos_ < s_.size() && s_[pos_] == '.') {
            ++pos_;
            size_t digits = pos_;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
            if (pos_ == digits) return Fail("invalid number");
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            size_t digits = pos_;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
            if (pos_ == digits) return Fail("invalid number");
        }
        double d = 0;
        auto res = std::from_chars(s_.data() + start, s_.data() + pos_, d);
        if (res.ec == std::errc::result_out_of_range) {
            // 1e400 overflows to the largest double, 1e-400 underflows to zero.
            std::string_view num = s_.substr(start, pos_ - start);
            size_t e = num.find_first_of("eE");
            bool underflow = e != std::string_view::npos && e + 1 < num.size() && num[e + 1] == '-';
            d = underflow ? 0.0 : (num[0] == '-' ? -1.7976931348623157e308 : 1.7976931348623157e308);
        } else if (res.ec != std::errc()) {
            return Fail("invalid number");
        }
        out = Value(d);
        return true;
    }

    bool Hex4(uint32_t& out) {
        if (pos_ + 4 > s_.size()) return Fail("invalid \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9')
                out |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                out |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                out |= static_cast<uint32_t>(c - 'A' + 10);
            else
                return Fail("invalid \\u escape");
        }
        return true;
    }

    bool ParseString(std::string& out) {
        ++pos_;  // opening quote
        while (true) {
            if (pos_ >= s_.size()) return Fail("unterminated string");
            unsigned char c = static_cast<unsigned char>(s_[pos_]);
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c < 0x20) return Fail("control character in string");
            if (c != '\\') {
                // Copy a run of plain bytes at once.
                size_t run = pos_;
                while (run < s_.size()) {
                    unsigned char r = static_cast<unsigned char>(s_[run]);
                    if (r == '"' || r == '\\' || r < 0x20) break;
                    ++run;
                }
                out.append(s_.data() + pos_, run - pos_);
                pos_ = run;
                continue;
            }
            ++pos_;
            if (pos_ >= s_.size()) return Fail("unterminated string");
            char e = s_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!Hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        // High surrogate: must be followed by \uDC00..\uDFFF to form one code point.
                        uint32_t lo = 0;
                        if (pos_ + 1 < s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                            size_t save = pos_;
                            pos_ += 2;
                            if (!Hex4(lo)) return false;
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            } else {
                                cp = 0xFFFD;
                                pos_ = save;  // re-read the second escape on its own
                            }
                        } else {
                            cp = 0xFFFD;
                        }
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        cp = 0xFFFD;  // lone low surrogate
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default:
                    return Fail("invalid escape");
            }
        }
    }

    bool ParseArray(Value& out, int depth) {
        ++pos_;
        Value::Array arr;
        SkipSpace();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            out = Value(std::move(arr));
            return true;
        }
        while (true) {
            Value item;
            if (!ParseValue(item, depth + 1)) return false;
            arr.push_back(std::move(item));
            SkipSpace();
            if (pos_ >= s_.size()) return Fail("unterminated array");
            char c = s_[pos_++];
            if (c == ']') break;
            if (c != ',') return Fail("expected ',' or ']'");
        }
        out = Value(std::move(arr));
        return true;
    }

    bool ParseObject(Value& out, int depth) {
        ++pos_;
        Value::Object obj;
        std::unordered_map<std::string, size_t> index;  // key -> member, for large objects only
        SkipSpace();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            out = Value(std::move(obj));
            return true;
        }
        while (true) {
            SkipSpace();
            if (pos_ >= s_.size() || s_[pos_] != '"') return Fail("expected string key");
            std::string key;
            if (!ParseString(key)) return false;
            SkipSpace();
            if (pos_ >= s_.size() || s_[pos_] != ':') return Fail("expected ':'");
            ++pos_;
            Value item;
            if (!ParseValue(item, depth + 1)) return false;
            // Duplicate keys: last one wins, position of the first is kept. Large objects (a skin,
            // a 4 MB server answer) look keys up in a hash map, or the check alone is O(n^2).
            size_t at = obj.size();
            if (obj.size() < kLinearKeys) {
                for (size_t i = 0; i < obj.size(); ++i)
                    if (obj[i].key == key) {
                        at = i;
                        break;
                    }
            } else {
                if (index.empty())
                    for (size_t i = 0; i < obj.size(); ++i) index.emplace(obj[i].key, i);
                at = index.try_emplace(key, obj.size()).first->second;
            }
            if (at < obj.size())
                obj[at].value = std::move(item);
            else
                obj.push_back(Member{std::move(key), std::move(item)});
            SkipSpace();
            if (pos_ >= s_.size()) return Fail("unterminated object");
            char c = s_[pos_++];
            if (c == '}') break;
            if (c != ',') return Fail("expected ',' or '}'");
        }
        out = Value(std::move(obj));
        return true;
    }

    std::string_view s_;
    size_t pos_ = 0;
    std::string error_;
};

void WriteString(std::string& out, const std::string& s) {
    static const char kHex[] = "0123456789abcdef";
    out += '"';
    for (char ch : s) {
        unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += kHex[c >> 4];
                    out += kHex[c & 0xF];
                } else {
                    out += ch;
                }
        }
    }
    out += '"';
}

void Indent(std::string& out, int level) {
    out += '\n';
    out.append(static_cast<size_t>(level) * 2, ' ');
}

void Write(std::string& out, const Value& v, bool pretty, int level) {
    switch (v.type()) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += v.AsBool() ? "true" : "false"; break;
        case Type::Number: out += FormatNumber(v.AsNumber()); break;
        case Type::String: WriteString(out, v.AsString()); break;
        case Type::Array: {
            const auto& arr = v.AsArray();
            out += '[';
            for (size_t i = 0; i < arr.size(); ++i) {
                if (i) out += ',';
                if (pretty) Indent(out, level + 1);
                Write(out, arr[i], pretty, level + 1);
            }
            if (pretty && !arr.empty()) Indent(out, level);
            out += ']';
            break;
        }
        case Type::Object: {
            const auto& obj = v.AsObject();
            out += '{';
            for (size_t i = 0; i < obj.size(); ++i) {
                if (i) out += ',';
                if (pretty) Indent(out, level + 1);
                WriteString(out, obj[i].key);
                out += pretty ? ": " : ":";
                Write(out, obj[i].value, pretty, level + 1);
            }
            if (pretty && !obj.empty()) Indent(out, level);
            out += '}';
            break;
        }
    }
}

}  // namespace

// --- Value -------------------------------------------------------------------------------

Value::Value(Array a) : v_(std::move(a)) {}
Value::Value(Object o) : v_(std::move(o)) {}
Value Value::MakeArray() { return Value(Array{}); }
Value Value::MakeObject() { return Value(Object{}); }

bool Value::AsBool(bool fallback) const {
    return IsBool() ? std::get<bool>(v_) : fallback;
}

double Value::AsNumber(double fallback) const {
    return IsNumber() ? std::get<double>(v_) : fallback;
}

int64_t Value::AsInt(int64_t fallback) const {
    if (!IsNumber()) return fallback;
    double d = std::get<double>(v_);
    if (!std::isfinite(d)) return fallback;
    if (d >= 9.2e18) return INT64_MAX;
    if (d <= -9.2e18) return INT64_MIN;
    return static_cast<int64_t>(std::llround(d));
}

const std::string& Value::AsString() const {
    return IsString() ? std::get<std::string>(v_) : kEmptyString;
}

std::wstring Value::AsWString() const {
    return ToWide(AsString());
}

const Value::Array& Value::AsArray() const {
    return IsArray() ? std::get<Array>(v_) : kEmptyArray;
}

const Value::Object& Value::AsObject() const {
    return IsObject() ? std::get<Object>(v_) : kEmptyObject;
}

Value::Array& Value::MutArray() {
    if (!IsArray()) v_ = Array{};
    return std::get<Array>(v_);
}

Value::Object& Value::MutObject() {
    if (!IsObject()) v_ = Object{};
    return std::get<Object>(v_);
}

size_t Value::size() const {
    if (IsArray()) return std::get<Array>(v_).size();
    if (IsObject()) return std::get<Object>(v_).size();
    return 0;
}

const Value* Value::Find(std::string_view key) const {
    if (!IsObject()) return nullptr;
    for (const Member& m : std::get<Object>(v_))
        if (m.key == key) return &m.value;
    return nullptr;
}

Value* Value::Find(std::string_view key) {
    if (!IsObject()) return nullptr;
    for (Member& m : std::get<Object>(v_))
        if (m.key == key) return &m.value;
    return nullptr;
}

Value& Value::Ensure(std::string_view key) {
    if (Value* found = Find(key)) return *found;
    Object& obj = MutObject();
    obj.push_back(Member{std::string(key), Value()});
    return obj.back().value;
}

void Value::Set(std::string_view key, Value value) {
    Ensure(key) = std::move(value);
}

bool Value::Erase(std::string_view key) {
    if (!IsObject()) return false;
    auto& obj = std::get<Object>(v_);
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (it->key == key) {
            obj.erase(it);
            return true;
        }
    }
    return false;
}

void Value::Push(Value value) {
    MutArray().push_back(std::move(value));
}

const Value& Value::operator[](size_t index) const {
    const Array& arr = AsArray();
    return index < arr.size() ? arr[index] : kNull;
}

std::string Value::GetString(std::string_view key, std::string_view fallback) const {
    const Value* v = Find(key);
    return v && v->IsString() ? v->AsString() : std::string(fallback);
}

std::wstring Value::GetWString(std::string_view key, std::wstring_view fallback) const {
    const Value* v = Find(key);
    return v && v->IsString() ? ToWide(v->AsString()) : std::wstring(fallback);
}

double Value::GetNumber(std::string_view key, double fallback) const {
    const Value* v = Find(key);
    return v ? v->AsNumber(fallback) : fallback;
}

int64_t Value::GetInt(std::string_view key, int64_t fallback) const {
    const Value* v = Find(key);
    return v ? v->AsInt(fallback) : fallback;
}

bool Value::GetBool(std::string_view key, bool fallback) const {
    const Value* v = Find(key);
    return v ? v->AsBool(fallback) : fallback;
}

bool Value::operator==(const Value& other) const {
    if (type() != other.type()) return false;
    switch (type()) {
        case Type::Null: return true;
        case Type::Bool: return AsBool() == other.AsBool();
        case Type::Number: return AsNumber() == other.AsNumber();
        case Type::String: return AsString() == other.AsString();
        case Type::Array: return AsArray() == other.AsArray();
        case Type::Object: {
            // Order-sensitive on purpose: the DOM preserves insertion order.
            const auto& a = AsObject();
            const auto& b = other.AsObject();
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i].key != b[i].key || a[i].value != b[i].value) return false;
            return true;
        }
    }
    return false;
}

// --- Free functions ----------------------------------------------------------------------

std::optional<Value> Parse(std::string_view text, std::string* error) {
    return Parser(text).Run(error);
}

std::string Serialize(const Value& value, bool pretty) {
    std::string out;
    Write(out, value, pretty, 0);
    if (pretty) out += '\n';
    return out;
}

std::string FormatNumber(double d) {
    if (!std::isfinite(d)) return "null";
    if (d == 0) return "0";  // also normalizes -0
    if (std::fabs(d) < 9007199254740992.0 && d == std::floor(d)) {
        char buf[32];
        auto res = std::to_chars(buf, buf + sizeof(buf), static_cast<long long>(d));
        return std::string(buf, res.ptr);
    }
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), d);  // shortest round-trip form
    return std::string(buf, res.ptr);
}

std::wstring ToWide(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

unsigned LegacyCodePage() {
    DWORD cp = 0;
    if (!GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                         reinterpret_cast<LPWSTR>(&cp), sizeof(cp) / sizeof(wchar_t)) ||
        cp == 0 || cp == CP_UTF8)
        cp = 1252;  // Unicode-only locales have no ANSI code page
    return cp;
}

std::string ToUtf8(std::wstring_view s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

bool ReadFileUtf8(const std::wstring& path, std::string* out) {
    if (!out) return false;
    out->clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart < (1LL << 31);
    if (ok) {
        out->resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = size.QuadPart == 0 ||
             (ReadFile(f, out->data(), static_cast<DWORD>(size.QuadPart), &read, nullptr) &&
              read == static_cast<DWORD>(size.QuadPart));
    }
    CloseHandle(f);
    if (!ok) {
        out->clear();
        return false;
    }
    if (out->size() >= 3 && out->compare(0, 3, "\xEF\xBB\xBF") == 0) out->erase(0, 3);
    return true;
}

bool WriteFileAtomic(const std::wstring& path, std::string_view data) {
    if (DataDirReadOnly() && IsInDataDir(path)) {  // another Lumen owns the data folder
        SetLastError(ERROR_ACCESS_DENIED);
        return false;
    }
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos && slash > 0) {
        std::wstring dir = path.substr(0, slash);
        DWORD attr = GetFileAttributesW(dir.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    }

    std::wstring temp = path + L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
                        std::to_wstring(GetCurrentThreadId()) + L".tmp";
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;

    bool ok = true;
    size_t done = 0;
    while (ok && done < data.size()) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(data.size() - done, 1u << 30));
        DWORD written = 0;
        ok = WriteFile(f, data.data() + done, chunk, &written, nullptr) && written == chunk;
        done += written;
    }
    ok = ok && FlushFileBuffers(f);
    CloseHandle(f);

    if (ok) ok = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(temp.c_str());
    return ok;
}

}  // namespace json
