#pragma once
// Small JSON DOM for Lumen's settings and library files.
//
// - Numbers are doubles (integers up to 2^53 round-trip exactly).
// - Strings are UTF-8 std::string; \uXXXX escapes (incl. surrogate pairs) are decoded on parse.
// - Objects keep insertion order; lookup is linear, which is fine for config-sized objects.
// - Nothing here throws on bad input: Parse returns std::nullopt and an error message.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace json {

enum class Type { Null, Bool, Number, String, Array, Object };

struct Member;

class Value {
public:
    using Array = std::vector<Value>;
    using Object = std::vector<Member>;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : v_(b) {}
    Value(double d) : v_(d) {}
    Value(int i) : v_(static_cast<double>(i)) {}
    Value(unsigned i) : v_(static_cast<double>(i)) {}
    Value(long i) : v_(static_cast<double>(i)) {}           // LONG
    Value(unsigned long i) : v_(static_cast<double>(i)) {}  // DWORD
    Value(int64_t i) : v_(static_cast<double>(i)) {}
    Value(uint64_t i) : v_(static_cast<double>(i)) {}
    Value(const char* s) : v_(std::string(s ? s : "")) {}
    Value(std::string s) : v_(std::move(s)) {}
    Value(std::string_view s) : v_(std::string(s)) {}
    Value(Array a);   // defined out of line: Member is incomplete here
    Value(Object o);

    static Value MakeArray();
    static Value MakeObject();

    Type type() const { return static_cast<Type>(v_.index()); }
    bool IsNull() const { return type() == Type::Null; }
    bool IsBool() const { return type() == Type::Bool; }
    bool IsNumber() const { return type() == Type::Number; }
    bool IsString() const { return type() == Type::String; }
    bool IsArray() const { return type() == Type::Array; }
    bool IsObject() const { return type() == Type::Object; }

    // Typed reads; return the fallback when the type does not match.
    bool AsBool(bool fallback = false) const;
    double AsNumber(double fallback = 0) const;
    int64_t AsInt(int64_t fallback = 0) const;  // rounds to nearest
    const std::string& AsString() const;        // empty string when not a string
    std::wstring AsWString() const;

    // Containers. The const versions return a shared empty container on a type mismatch; the
    // mutable ones (and operator[](key), Set, Push) turn any non-container value into an empty
    // container first, so `Value v; v.Push(1);` and `Value v; v["a"] = 1;` work.
    const Array& AsArray() const;
    const Object& AsObject() const;
    Array& MutArray();
    Object& MutObject();

    size_t size() const;  // elements / members; 0 for scalars

    // Object access.
    const Value* Find(std::string_view key) const;
    Value* Find(std::string_view key);
    Value& Ensure(std::string_view key);  // the member, inserted as null when missing
    // v["key"] == v.Ensure("key"). A template so that v[0] picks the index overload below
    // instead of turning 0 into a null string_view.
    template <typename K, std::enable_if_t<std::is_convertible_v<const K&, std::string_view> &&
                                               !std::is_integral_v<K> && !std::is_null_pointer_v<K>,
                                           int> = 0>
    Value& operator[](const K& key) {
        return Ensure(std::string_view(key));
    }
    void Set(std::string_view key, Value value);
    bool Erase(std::string_view key);

    // Array access.
    void Push(Value value);
    const Value& operator[](size_t index) const;  // null when out of range
    const Value& operator[](int index) const {
        return (*this)[index < 0 ? static_cast<size_t>(-1) : static_cast<size_t>(index)];
    }

    // Member helpers with defaults (missing key or wrong type -> fallback).
    std::string GetString(std::string_view key, std::string_view fallback = {}) const;
    std::wstring GetWString(std::string_view key, std::wstring_view fallback = {}) const;
    double GetNumber(std::string_view key, double fallback = 0) const;
    int64_t GetInt(std::string_view key, int64_t fallback = 0) const;
    bool GetBool(std::string_view key, bool fallback = false) const;

    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }

private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> v_;
};

struct Member {
    std::string key;
    Value value;
};

// Parses a complete JSON document (a UTF-8 BOM is skipped). On failure returns nullopt and,
// when `error` is given, a message like "line 3, column 7: expected ':'".
std::optional<Value> Parse(std::string_view text, std::string* error = nullptr);

// Compact or 2-space-indented output. Non-ASCII is written as raw UTF-8; NaN/Inf become null.
std::string Serialize(const Value& value, bool pretty = false);

// Shortest decimal text that round-trips the double (integers print without exponent/fraction).
std::string FormatNumber(double d);

// UTF-8 <-> UTF-16 conversion (invalid sequences become U+FFFD).
std::wstring ToWide(std::string_view utf8);
std::string ToUtf8(std::wstring_view wide);

// Whole-file read; strips a UTF-8 BOM. False when the file cannot be opened or read.
bool ReadFileUtf8(const std::wstring& path, std::string* out);

// Writes to "<path>.<pid>.tmp" in the same directory, flushes, then renames over `path` with
// MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH). Creates the parent folder.
// A crash mid-write leaves the old file intact.
bool WriteFileAtomic(const std::wstring& path, std::string_view data);

}  // namespace json
