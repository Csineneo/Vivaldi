// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/inspector_protocol/String16STL.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <locale>

namespace blink {
namespace protocol {

const UChar replacementCharacter = 0xFFFD;

template<typename CharType> inline bool isASCII(CharType c)
{
    return !(c & ~0x7F);
}

template<typename CharType> inline bool isASCIIAlpha(CharType c)
{
    return (c | 0x20) >= 'a' && (c | 0x20) <= 'z';
}

template<typename CharType> inline bool isASCIIDigit(CharType c)
{
    return c >= '0' && c <= '9';
}

template<typename CharType> inline bool isASCIIAlphanumeric(CharType c)
{
    return isASCIIDigit(c) || isASCIIAlpha(c);
}

template<typename CharType> inline bool isASCIIHexDigit(CharType c)
{
    return isASCIIDigit(c) || ((c | 0x20) >= 'a' && (c | 0x20) <= 'f');
}

template<typename CharType> inline bool isASCIIOctalDigit(CharType c)
{
    return (c >= '0') & (c <= '7');
}

template<typename CharType> inline bool isASCIIPrintable(CharType c)
{
    return c >= ' ' && c <= '~';
}

/*
 Statistics from a run of Apple's page load test for callers of isASCIISpace:

 character          count
 ---------          -----
 non-spaces         689383
 20  space          294720
 0A  \n             89059
 09  \t             28320
 0D  \r             0
 0C  \f             0
 0B  \v             0
 */
template<typename CharType> inline bool isASCIISpace(CharType c)
{
    return c <= ' ' && (c == ' ' || (c <= 0xD && c >= 0x9));
}

extern const LChar ASCIICaseFoldTable[256] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
    0x40, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
    0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

template<typename CharType> inline int toASCIIHexValue(CharType c)
{
    ASSERT(isASCIIHexDigit(c));
    return c < 'A' ? c - '0' : (c - 'A' + 10) & 0xF;
}

template<typename CharType> inline int toASCIIHexValue(CharType upperValue, CharType lowerValue)
{
    ASSERT(isASCIIHexDigit(upperValue) && isASCIIHexDigit(lowerValue));
    return ((toASCIIHexValue(upperValue) << 4) & 0xF0) | toASCIIHexValue(lowerValue);
}

inline char lowerNibbleToASCIIHexDigit(char c)
{
    char nibble = c & 0xF;
    return nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
}

inline char upperNibbleToASCIIHexDigit(char c)
{
    char nibble = (c >> 4) & 0xF;
    return nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
}

template<typename CharType> inline bool isASCIIAlphaCaselessEqual(CharType cssCharacter, char character)
{
    // This function compares a (preferrably) constant ASCII
    // lowercase letter to any input character.
    ASSERT(character >= 'a' && character <= 'z');
    return LIKELY(toASCIILowerUnchecked(cssCharacter) == character);
}

inline int inlineUTF8SequenceLengthNonASCII(char b0)
{
    if ((b0 & 0xC0) != 0xC0)
        return 0;
    if ((b0 & 0xE0) == 0xC0)
        return 2;
    if ((b0 & 0xF0) == 0xE0)
        return 3;
    if ((b0 & 0xF8) == 0xF0)
        return 4;
    return 0;
}

inline int inlineUTF8SequenceLength(char b0)
{
    return isASCII(b0) ? 1 : inlineUTF8SequenceLengthNonASCII(b0);
}

// Once the bits are split out into bytes of UTF-8, this is a mask OR-ed
// into the first byte, depending on how many bytes follow.  There are
// as many entries in this table as there are UTF-8 sequence types.
// (I.e., one byte sequence, two byte... etc.). Remember that sequences
// for *legal* UTF-8 will be 4 or fewer bytes total.
static const unsigned char firstByteMark[7] = { 0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC };

typedef enum {
    conversionOK, // conversion successful
    sourceExhausted, // partial character in source, but hit end
    targetExhausted, // insuff. room in target for conversion
    sourceIllegal // source sequence is illegal/malformed
} ConversionResult;

ConversionResult convertLatin1ToUTF8(
    const LChar** sourceStart, const LChar* sourceEnd,
    char** targetStart, char* targetEnd)
{
    ConversionResult result = conversionOK;
    const LChar* source = *sourceStart;
    char* target = *targetStart;
    while (source < sourceEnd) {
        UChar32 ch;
        unsigned short bytesToWrite = 0;
        const UChar32 byteMask = 0xBF;
        const UChar32 byteMark = 0x80;
        const LChar* oldSource = source; // In case we have to back up because of target overflow.
        ch = static_cast<unsigned short>(*source++);

        // Figure out how many bytes the result will require
        if (ch < (UChar32)0x80)
            bytesToWrite = 1;
        else
            bytesToWrite = 2;

        target += bytesToWrite;
        if (target > targetEnd) {
            source = oldSource; // Back up source pointer!
            target -= bytesToWrite;
            result = targetExhausted;
            break;
        }
        switch (bytesToWrite) { // note: everything falls through.
        case 2:
            *--target = (char)((ch | byteMark) & byteMask);
            ch >>= 6;
        case 1:
            *--target =  (char)(ch | firstByteMark[bytesToWrite]);
        }
        target += bytesToWrite;
    }
    *sourceStart = source;
    *targetStart = target;
    return result;
}

ConversionResult convertUTF16ToUTF8(
    const UChar** sourceStart, const UChar* sourceEnd,
    char** targetStart, char* targetEnd, bool strict)
{
    ConversionResult result = conversionOK;
    const UChar* source = *sourceStart;
    char* target = *targetStart;
    while (source < sourceEnd) {
        UChar32 ch;
        unsigned short bytesToWrite = 0;
        const UChar32 byteMask = 0xBF;
        const UChar32 byteMark = 0x80;
        const UChar* oldSource = source; // In case we have to back up because of target overflow.
        ch = static_cast<unsigned short>(*source++);
        // If we have a surrogate pair, convert to UChar32 first.
        if (ch >= 0xD800 && ch <= 0xDBFF) {
            // If the 16 bits following the high surrogate are in the source buffer...
            if (source < sourceEnd) {
                UChar32 ch2 = static_cast<unsigned short>(*source);
                // If it's a low surrogate, convert to UChar32.
                if (ch2 >= 0xDC00 && ch2 <= 0xDFFF) {
                    ch = ((ch - 0xD800) << 10) + (ch2 - 0xDC00) + 0x0010000;
                    ++source;
                } else if (strict) { // it's an unpaired high surrogate
                    --source; // return to the illegal value itself
                    result = sourceIllegal;
                    break;
                }
            } else { // We don't have the 16 bits following the high surrogate.
                --source; // return to the high surrogate
                result = sourceExhausted;
                break;
            }
        } else if (strict) {
            // UTF-16 surrogate values are illegal in UTF-32
            if (ch >= 0xDC00 && ch <= 0xDFFF) {
                --source; // return to the illegal value itself
                result = sourceIllegal;
                break;
            }
        }
        // Figure out how many bytes the result will require
        if (ch < (UChar32)0x80) {
            bytesToWrite = 1;
        } else if (ch < (UChar32)0x800) {
            bytesToWrite = 2;
        } else if (ch < (UChar32)0x10000) {
            bytesToWrite = 3;
        } else if (ch < (UChar32)0x110000) {
            bytesToWrite = 4;
        } else {
            bytesToWrite = 3;
            ch = replacementCharacter;
        }

        target += bytesToWrite;
        if (target > targetEnd) {
            source = oldSource; // Back up source pointer!
            target -= bytesToWrite;
            result = targetExhausted;
            break;
        }
        switch (bytesToWrite) { // note: everything falls through.
        case 4:
            *--target = (char)((ch | byteMark) & byteMask);
            ch >>= 6;
        case 3:
            *--target = (char)((ch | byteMark) & byteMask);
            ch >>= 6;
        case 2:
            *--target = (char)((ch | byteMark) & byteMask);
            ch >>= 6;
        case 1:
            *--target =  (char)(ch | firstByteMark[bytesToWrite]);
        }
        target += bytesToWrite;
    }
    *sourceStart = source;
    *targetStart = target;
    return result;
}

// Helper to write a three-byte UTF-8 code point to the buffer, caller must check room is available.
static inline void putUTF8Triple(char*& buffer, UChar ch)
{
    ASSERT(ch >= 0x0800);
    *buffer++ = static_cast<char>(((ch >> 12) & 0x0F) | 0xE0);
    *buffer++ = static_cast<char>(((ch >> 6) & 0x3F) | 0x80);
    *buffer++ = static_cast<char>((ch & 0x3F) | 0x80);
}

// trim from start
static inline wstring &ltrim(wstring &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
        return s;
}

// trim from end
static inline wstring &rtrim(wstring &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
        return s;
}

// trim from both ends
static inline wstring &trim(wstring &s)
{
    return ltrim(rtrim(s));
}

std::string String16::utf8() const
{
    unsigned length = this->length();

    if (!length)
        return std::string("");

    // Allocate a buffer big enough to hold all the characters
    // (an individual UTF-16 UChar can only expand to 3 UTF-8 bytes).
    // Optimization ideas, if we find this function is hot:
    //  * We could speculatively create a CStringBuffer to contain 'length'
    //    characters, and resize if necessary (i.e. if the buffer contains
    //    non-ascii characters). (Alternatively, scan the buffer first for
    //    ascii characters, so we know this will be sufficient).
    //  * We could allocate a CStringBuffer with an appropriate size to
    //    have a good chance of being able to write the string into the
    //    buffer without reallocing (say, 1.5 x length).
    if (length > std::numeric_limits<unsigned>::max() / 3)
        return std::string();
    std::vector<char> bufferVector(length * 3);
    char* buffer = bufferVector.data();
    const UChar* characters = m_impl.data();

    bool strict = false;
    ConversionResult result = convertUTF16ToUTF8(&characters, characters + length, &buffer, buffer + bufferVector.size(), strict);
    ASSERT(result != targetExhausted); // (length * 3) should be sufficient for any conversion

    // Only produced from strict conversion.
    if (result == sourceIllegal) {
        ASSERT(strict);
        return std::string();
    }

    // Check for an unconverted high surrogate.
    if (result == sourceExhausted) {
        if (strict)
            return std::string();
        // This should be one unpaired high surrogate. Treat it the same
        // was as an unpaired high surrogate would have been handled in
        // the middle of a string with non-strict conversion - which is
        // to say, simply encode it to UTF-8.
        ASSERT((characters + 1) == (m_impl.data() + length));
        ASSERT((*characters >= 0xD800) && (*characters <= 0xDBFF));
        // There should be room left, since one UChar hasn't been
        // converted.
        ASSERT((buffer + 3) <= (buffer + bufferVector.size()));
        putUTF8Triple(buffer, *characters);
    }

    return std::string(bufferVector.data(), buffer - bufferVector.data());
}

String16 String16::stripWhiteSpace() const
{
    wstring result(m_impl);
    trim(result);
    return result;
}

} // namespace protocol
} // namespace blink
