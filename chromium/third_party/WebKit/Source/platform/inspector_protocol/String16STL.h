// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef String16STL_h
#define String16STL_h

#include <cstring>
#include <string>
#include <vector>

using UChar = uint16_t;
using UChar32 = uint32_t;
using LChar = unsigned char;
// presubmit: allow wstring
using wstring = std::basic_string<UChar>;
const size_t kNotFound = static_cast<size_t>(-1);

namespace blink {
namespace protocol {

class String16 {
public:
    String16() { }
    String16(const String16& other) : m_impl(other.m_impl) { }
    // presubmit: allow wstring
    String16(const wstring& impl) : m_impl(impl) { }
    String16(const UChar* characters) : m_impl(characters) { }
    String16(const char* characters) : String16(characters, std::strlen(characters)) { }
    String16(const char* characters, size_t size)
    {
        m_impl.resize(size);
        for (size_t i = 0; i < size; ++i)
            m_impl[i] = characters[i];
    }
    String16(const UChar* characters, size_t size) : m_impl(characters, size) { }
    ~String16() { }

    unsigned sizeInBytes() const { return m_impl.size() * sizeof(UChar); }
    const UChar* characters16() const { return m_impl.c_str(); }
    std::string utf8() const;
    static String16 number(int i) { return String16(std::to_string(i).c_str()); }
    static String16 fromDouble(double d) { return String16(std::to_string(d).c_str()); }
    static String16 fromDoubleFixedPrecision(double d, int len) { return String16(std::to_string(d).c_str()); }

    static double charactersToDouble(const UChar* characters, size_t length, bool* ok = 0)
    {
        size_t idx;
        std::string str;
        str.resize(length);
        for (size_t i = 0; i < length; ++i)
            str[i] = static_cast<char>(characters[i]);
        double result = stod(str, &idx);
        if (ok)
            *ok = idx == length;
        return result;
    }

    String16 substring(unsigned pos, unsigned len = 0xFFFFFFFF) const
    {
        return String16(m_impl.substr(pos, len));
    }

    String16 stripWhiteSpace() const;

    int toInt(bool* ok = 0) const
    {
        size_t length = m_impl.length();
        size_t idx;
        std::string str;
        str.resize(length);
        for (size_t i = 0; i < length; ++i)
            str[i] = static_cast<char>(m_impl[i]);
        int result = stoi(str, &idx);
        if (ok)
            *ok = idx == length;
        return result;
    }

    size_t length() const { return m_impl.length(); }
    bool isEmpty() const { return !m_impl.length(); }
    UChar operator[](unsigned index) const { return m_impl[index]; }

    size_t find(UChar c, unsigned start = 0) const
    {
        return m_impl.find(c, start);
    }

    size_t find(const String16& str, unsigned start = 0) const
    {
        return m_impl.find(str.m_impl, start);
    }

    size_t reverseFind(const String16& str, unsigned start = 0xFFFFFFFF) const
    {
        return m_impl.rfind(str.m_impl, start);
    }

    bool endsWith(UChar character) const
    {
        return m_impl.length() && m_impl[m_impl.length() - 1] == character;
    }

    // presubmit: allow wstring
    const wstring& impl() const { return m_impl; }

    std::size_t hash() const
    {
        if (!has_hash) {
            size_t hash = 0;
            for (size_t i = 0; i < length(); ++i)
                hash = 31 * hash + m_impl[i];
            hash_code = hash;
            has_hash = true;
        }
        return hash_code;
    }

private:
    // presubmit: allow wstring
    wstring m_impl;
    mutable bool has_hash = false;
    mutable std::size_t hash_code;
};

static inline bool isSpaceOrNewline(UChar c)
{
    return false;
}

class String16Builder {
public:
    String16Builder() { }

    void append(const String16& str)
    {
        m_impl += str.impl();
    }

    void append(UChar c)
    {
        m_impl += c;
    }

    void append(LChar c)
    {
        m_impl += c;
    }

    void append(char c)
    {
        m_impl += c;
    }

    void appendNumber(int i)
    {
        m_impl = m_impl + String16(std::to_string(i).c_str()).impl();
    }

    void append(const UChar* c, size_t length)
    {
        // presubmit: allow wstring
        m_impl += wstring(c, length);
    }

    void append(const char* c, size_t length)
    {
        m_impl += String16(c, length).impl();
    }

    String16 toString()
    {
        return String16(m_impl);
    }

    void reserveCapacity(unsigned newCapacity)
    {
    }

private:
    // presubmit: allow wstring
    wstring m_impl;
};

inline bool operator==(const String16& a, const String16& b) { return a.impl() == b.impl(); }
inline bool operator!=(const String16& a, const String16& b) { return a.impl() != b.impl(); }
inline bool operator==(const String16& a, const char* b) { return a.impl() == String16(b).impl(); }

inline String16 operator+(const String16& a, const char* b)
{
    return String16(a.impl() + String16(b).impl());
}

inline String16 operator+(const char* a, const String16& b)
{
    return String16(String16(a).impl() + b.impl());
}

inline String16 operator+(const String16& a, const String16& b)
{
    return String16(a.impl() + b.impl());
}

} // namespace protocol
} // namespace blink

using String16 = blink::protocol::String16;
using String16Builder = blink::protocol::String16Builder;


namespace WTF {
// Interim solution for those headers that reference WTF::String for overrides.
// It does nothing. If the code actually relies on WTF:String, it will not
// compile!
// TODO(eostroukhov): Eradicate
class String {
public:
    String() {};
    String(const String16& other) {};
    operator String16() const { return String16(); };
};
} // namespace WTF

using String = WTF::String;

namespace std {
template<>
struct hash<String16> {
std::size_t operator()(const String16& k) const
{
    return k.hash();
}
};
}

#endif // !defined(String16STL_h)
