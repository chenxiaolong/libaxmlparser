/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "ResourceType"
//#define LOG_NDEBUG 0

#include <androidfw/ResourceTypes.h>
#include <utils/Atomic.h>
#include <utils/ByteOrder.h>
#include <utils/Log.h>
#include <utils/String16.h>
#include <utils/String8.h>

#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <ctype.h>
#include <stdint.h>
#include <stddef.h>

#define STRING_POOL_NOISY(x) //x
#define XML_NOISY(x) //x

namespace android {

static status_t validate_chunk(const ResChunk_header* chunk,
                               size_t minSize,
                               const uint8_t* dataEnd,
                               const char* name)
{
    const uint16_t headerSize = dtohs(chunk->headerSize);
    const uint32_t size = dtohl(chunk->size);

    if (headerSize >= minSize) {
        if (headerSize <= size) {
            if (((headerSize|size)&0x3) == 0) {
                if ((size_t)size <= (size_t)(dataEnd-((const uint8_t*)chunk))) {
                    return NO_ERROR;
                }
                ALOGW("%s data size 0x%x extends beyond resource end %p.",
                     name, size, (void*)(dataEnd-((const uint8_t*)chunk)));
                return BAD_TYPE;
            }
            ALOGW("%s size 0x%x or headerSize 0x%x is not on an integer boundary.",
                 name, (int)size, (int)headerSize);
            return BAD_TYPE;
        }
        ALOGW("%s size 0x%x is smaller than header size 0x%x.",
             name, size, headerSize);
        return BAD_TYPE;
    }
    ALOGW("%s header size 0x%04x is too small.",
         name, headerSize);
    return BAD_TYPE;
}

inline void Res_value::copyFrom_dtoh(const Res_value& src)
{
    size = dtohs(src.size);
    res0 = src.res0;
    dataType = src.dataType;
    data = dtohl(src.data);
}

// --------------------------------------------------------------------
// --------------------------------------------------------------------
// --------------------------------------------------------------------

ResStringPool::ResStringPool()
    : mError(NO_INIT), mOwnedData(NULL), mHeader(NULL), mCache(NULL)
{
}

ResStringPool::ResStringPool(const void* data, size_t size, bool copyData)
    : mError(NO_INIT), mOwnedData(NULL), mHeader(NULL), mCache(NULL)
{
    setTo(data, size, copyData);
}

ResStringPool::~ResStringPool()
{
    uninit();
}

void ResStringPool::setToEmpty()
{
    uninit();

    mOwnedData = calloc(1, sizeof(ResStringPool_header));
    ResStringPool_header* header = (ResStringPool_header*) mOwnedData;
    mSize = 0;
    mEntries = NULL;
    mStrings = NULL;
    mStringPoolSize = 0;
    mEntryStyles = NULL;
    mStyles = NULL;
    mStylePoolSize = 0;
    mHeader = (const ResStringPool_header*) header;
}

status_t ResStringPool::setTo(const void* data, size_t size, bool copyData)
{
    if (!data || !size) {
        return (mError=BAD_TYPE);
    }

    uninit();

    const bool notDeviceEndian = htods(0xf0) != 0xf0;

    if (copyData || notDeviceEndian) {
        mOwnedData = malloc(size);
        if (mOwnedData == NULL) {
            return (mError=NO_MEMORY);
        }
        memcpy(mOwnedData, data, size);
        data = mOwnedData;
    }

    mHeader = (const ResStringPool_header*)data;

    if (notDeviceEndian) {
        ResStringPool_header* h = const_cast<ResStringPool_header*>(mHeader);
        h->header.headerSize = dtohs(mHeader->header.headerSize);
        h->header.type = dtohs(mHeader->header.type);
        h->header.size = dtohl(mHeader->header.size);
        h->stringCount = dtohl(mHeader->stringCount);
        h->styleCount = dtohl(mHeader->styleCount);
        h->flags = dtohl(mHeader->flags);
        h->stringsStart = dtohl(mHeader->stringsStart);
        h->stylesStart = dtohl(mHeader->stylesStart);
    }

    if (mHeader->header.headerSize > mHeader->header.size
            || mHeader->header.size > size) {
        ALOGW("Bad string block: header size %d or total size %d is larger than data size %d\n",
                (int)mHeader->header.headerSize, (int)mHeader->header.size, (int)size);
        return (mError=BAD_TYPE);
    }
    mSize = mHeader->header.size;
    mEntries = (const uint32_t*)
        (((const uint8_t*)data)+mHeader->header.headerSize);

    if (mHeader->stringCount > 0) {
        if ((mHeader->stringCount*sizeof(uint32_t) < mHeader->stringCount)  // uint32 overflow?
            || (mHeader->header.headerSize+(mHeader->stringCount*sizeof(uint32_t)))
                > size) {
            ALOGW("Bad string block: entry of %d items extends past data size %d\n",
                    (int)(mHeader->header.headerSize+(mHeader->stringCount*sizeof(uint32_t))),
                    (int)size);
            return (mError=BAD_TYPE);
        }

        size_t charSize;
        if (mHeader->flags&ResStringPool_header::UTF8_FLAG) {
            charSize = sizeof(uint8_t);
        } else {
            charSize = sizeof(uint16_t);
        }

        // There should be at least space for the smallest string
        // (2 bytes length, null terminator).
        if (mHeader->stringsStart >= (mSize - sizeof(uint16_t))) {
            ALOGW("Bad string block: string pool starts at %d, after total size %d\n",
                    (int)mHeader->stringsStart, (int)mHeader->header.size);
            return (mError=BAD_TYPE);
        }

        mStrings = (const void*)
            (((const uint8_t*)data) + mHeader->stringsStart);

        if (mHeader->styleCount == 0) {
            mStringPoolSize = (mSize - mHeader->stringsStart) / charSize;
        } else {
            // check invariant: styles starts before end of data
            if (mHeader->stylesStart >= (mSize - sizeof(uint16_t))) {
                ALOGW("Bad style block: style block starts at %d past data size of %d\n",
                    (int)mHeader->stylesStart, (int)mHeader->header.size);
                return (mError=BAD_TYPE);
            }
            // check invariant: styles follow the strings
            if (mHeader->stylesStart <= mHeader->stringsStart) {
                ALOGW("Bad style block: style block starts at %d, before strings at %d\n",
                    (int)mHeader->stylesStart, (int)mHeader->stringsStart);
                return (mError=BAD_TYPE);
            }
            mStringPoolSize =
                (mHeader->stylesStart-mHeader->stringsStart)/charSize;
        }

        // check invariant: stringCount > 0 requires a string pool to exist
        if (mStringPoolSize == 0) {
            ALOGW("Bad string block: stringCount is %d but pool size is 0\n", (int)mHeader->stringCount);
            return (mError=BAD_TYPE);
        }

        if (notDeviceEndian) {
            size_t i;
            uint32_t* e = const_cast<uint32_t*>(mEntries);
            for (i=0; i<mHeader->stringCount; i++) {
                e[i] = dtohl(mEntries[i]);
            }
            if (!(mHeader->flags&ResStringPool_header::UTF8_FLAG)) {
                const uint16_t* strings = (const uint16_t*)mStrings;
                uint16_t* s = const_cast<uint16_t*>(strings);
                for (i=0; i<mStringPoolSize; i++) {
                    s[i] = dtohs(strings[i]);
                }
            }
        }

        if ((mHeader->flags&ResStringPool_header::UTF8_FLAG &&
                ((uint8_t*)mStrings)[mStringPoolSize-1] != 0) ||
                (!mHeader->flags&ResStringPool_header::UTF8_FLAG &&
                ((uint16_t*)mStrings)[mStringPoolSize-1] != 0)) {
            ALOGW("Bad string block: last string is not 0-terminated\n");
            return (mError=BAD_TYPE);
        }
    } else {
        mStrings = NULL;
        mStringPoolSize = 0;
    }

    if (mHeader->styleCount > 0) {
        mEntryStyles = mEntries + mHeader->stringCount;
        // invariant: integer overflow in calculating mEntryStyles
        if (mEntryStyles < mEntries) {
            ALOGW("Bad string block: integer overflow finding styles\n");
            return (mError=BAD_TYPE);
        }

        if (((const uint8_t*)mEntryStyles-(const uint8_t*)mHeader) > (int)size) {
            ALOGW("Bad string block: entry of %d styles extends past data size %d\n",
                    (int)((const uint8_t*)mEntryStyles-(const uint8_t*)mHeader),
                    (int)size);
            return (mError=BAD_TYPE);
        }
        mStyles = (const uint32_t*)
            (((const uint8_t*)data)+mHeader->stylesStart);
        if (mHeader->stylesStart >= mHeader->header.size) {
            ALOGW("Bad string block: style pool starts %d, after total size %d\n",
                    (int)mHeader->stylesStart, (int)mHeader->header.size);
            return (mError=BAD_TYPE);
        }
        mStylePoolSize =
            (mHeader->header.size-mHeader->stylesStart)/sizeof(uint32_t);

        if (notDeviceEndian) {
            size_t i;
            uint32_t* e = const_cast<uint32_t*>(mEntryStyles);
            for (i=0; i<mHeader->styleCount; i++) {
                e[i] = dtohl(mEntryStyles[i]);
            }
            uint32_t* s = const_cast<uint32_t*>(mStyles);
            for (i=0; i<mStylePoolSize; i++) {
                s[i] = dtohl(mStyles[i]);
            }
        }

        const ResStringPool_span endSpan = {
            { htodl(ResStringPool_span::END) },
            htodl(ResStringPool_span::END), htodl(ResStringPool_span::END)
        };
        if (memcmp(&mStyles[mStylePoolSize-(sizeof(endSpan)/sizeof(uint32_t))],
                   &endSpan, sizeof(endSpan)) != 0) {
            ALOGW("Bad string block: last style is not 0xFFFFFFFF-terminated\n");
            return (mError=BAD_TYPE);
        }
    } else {
        mEntryStyles = NULL;
        mStyles = NULL;
        mStylePoolSize = 0;
    }

    return (mError=NO_ERROR);
}

status_t ResStringPool::getError() const
{
    return mError;
}

void ResStringPool::uninit()
{
    mError = NO_INIT;
    if (mHeader != NULL && mCache != NULL) {
        for (size_t x = 0; x < mHeader->stringCount; x++) {
            if (mCache[x] != NULL) {
                free(mCache[x]);
                mCache[x] = NULL;
            }
        }
        free(mCache);
        mCache = NULL;
    }
    if (mOwnedData) {
        free(mOwnedData);
        mOwnedData = NULL;
    }
}

/**
 * Strings in UTF-16 format have length indicated by a length encoded in the
 * stored data. It is either 1 or 2 characters of length data. This allows a
 * maximum length of 0x7FFFFFF (2147483647 bytes), but if you're storing that
 * much data in a string, you're abusing them.
 *
 * If the high bit is set, then there are two characters or 4 bytes of length
 * data encoded. In that case, drop the high bit of the first character and
 * add it together with the next character.
 */
static inline size_t
decodeLength(const uint16_t** str)
{
    size_t len = **str;
    if ((len & 0x8000) != 0) {
        (*str)++;
        len = ((len & 0x7FFF) << 16) | **str;
    }
    (*str)++;
    return len;
}

/**
 * Strings in UTF-8 format have length indicated by a length encoded in the
 * stored data. It is either 1 or 2 characters of length data. This allows a
 * maximum length of 0x7FFF (32767 bytes), but you should consider storing
 * text in another way if you're using that much data in a single string.
 *
 * If the high bit is set, then there are two characters or 2 bytes of length
 * data encoded. In that case, drop the high bit of the first character and
 * add it together with the next character.
 */
static inline size_t
decodeLength(const uint8_t** str)
{
    size_t len = **str;
    if ((len & 0x80) != 0) {
        (*str)++;
        len = ((len & 0x7F) << 8) | **str;
    }
    (*str)++;
    return len;
}

const char16_t* ResStringPool::stringAt(size_t idx, size_t* u16len) const
{
    if (mError == NO_ERROR && idx < mHeader->stringCount) {
        const bool isUTF8 = (mHeader->flags&ResStringPool_header::UTF8_FLAG) != 0;
        const uint32_t off = mEntries[idx]/(isUTF8?sizeof(uint8_t):sizeof(uint16_t));
        if (off < (mStringPoolSize-1)) {
            if (!isUTF8) {
                const uint16_t* strings = (uint16_t*)mStrings;
                const uint16_t* str = strings+off;

                *u16len = decodeLength(&str);
                if ((uint32_t)(str+*u16len-strings) < mStringPoolSize) {
                    return reinterpret_cast<const char16_t*>(str);
                } else {
                    ALOGW("Bad string block: string #%d extends to %d, past end at %d\n",
                            (int)idx, (int)(str+*u16len-strings), (int)mStringPoolSize);
                }
            } else {
                const uint8_t* strings = (uint8_t*)mStrings;
                const uint8_t* u8str = strings+off;

                *u16len = decodeLength(&u8str);
                size_t u8len = decodeLength(&u8str);

                // encLen must be less than 0x7FFF due to encoding.
                if ((uint32_t)(u8str+u8len-strings) < mStringPoolSize) {
                    std::lock_guard<std::mutex> lock(mDecodeLock);

                    if (mCache == NULL) {
#ifndef HAVE_ANDROID_OS
                        STRING_POOL_NOISY(ALOGI("CREATING STRING CACHE OF %d bytes",
                                mHeader->stringCount*sizeof(char16_t**)));
#else
                        // We do not want to be in this case when actually running Android.
                        ALOGV("CREATING STRING CACHE OF %d bytes",
                                mHeader->stringCount*sizeof(char16_t**));
#endif
                        mCache = (char16_t**)calloc(mHeader->stringCount, sizeof(char16_t**));
                        if (mCache == NULL) {
                            ALOGW("No memory trying to allocate decode cache table of %d bytes\n",
                                    (int)(mHeader->stringCount*sizeof(char16_t**)));
                            return NULL;
                        }
                    }

                    if (mCache[idx] != NULL) {
                        return mCache[idx];
                    }

                    ssize_t actualLen = utf8_to_utf16_length(u8str, u8len);
                    if (actualLen < 0 || (size_t)actualLen != *u16len) {
                        ALOGW("Bad string block: string #%lld decoded length is not correct "
                                "%lld vs %llu\n",
                                (long long)idx, (long long)actualLen, (long long)*u16len);
                        return NULL;
                    }

                    char16_t *u16str = (char16_t *)calloc(*u16len+1, sizeof(char16_t));
                    if (!u16str) {
                        ALOGW("No memory when trying to allocate decode cache for string #%d\n",
                                (int)idx);
                        return NULL;
                    }

                    STRING_POOL_NOISY(ALOGI("Caching UTF8 string: %s", u8str));
                    utf8_to_utf16(u8str, u8len, u16str);
                    mCache[idx] = u16str;
                    return u16str;
                } else {
                    ALOGW("Bad string block: string #%lld extends to %lld, past end at %lld\n",
                            (long long)idx, (long long)(u8str+u8len-strings),
                            (long long)mStringPoolSize);
                }
            }
        } else {
            ALOGW("Bad string block: string #%d entry is at %d, past end at %d\n",
                    (int)idx, (int)(off*sizeof(uint16_t)),
                    (int)(mStringPoolSize*sizeof(uint16_t)));
        }
    }
    return NULL;
}

const char* ResStringPool::string8At(size_t idx, size_t* outLen) const
{
    if (mError == NO_ERROR && idx < mHeader->stringCount) {
        if ((mHeader->flags&ResStringPool_header::UTF8_FLAG) == 0) {
            return NULL;
        }
        const uint32_t off = mEntries[idx]/sizeof(char);
        if (off < (mStringPoolSize-1)) {
            const uint8_t* strings = (uint8_t*)mStrings;
            const uint8_t* str = strings+off;
            *outLen = decodeLength(&str);
            size_t encLen = decodeLength(&str);
            if ((uint32_t)(str+encLen-strings) < mStringPoolSize) {
                return (const char*)str;
            } else {
                ALOGW("Bad string block: string #%d extends to %d, past end at %d\n",
                        (int)idx, (int)(str+encLen-strings), (int)mStringPoolSize);
            }
        } else {
            ALOGW("Bad string block: string #%d entry is at %d, past end at %d\n",
                    (int)idx, (int)(off*sizeof(uint16_t)),
                    (int)(mStringPoolSize*sizeof(uint16_t)));
        }
    }
    return NULL;
}

const String8 ResStringPool::string8ObjectAt(size_t idx) const
{
    size_t len;
    const char *str = string8At(idx, &len);
    if (str != NULL) {
        return String8(str, len);
    }

    const char16_t *str16 = stringAt(idx, &len);
    if (str16 != NULL) {
        return String8(str16, len);
    }
    return String8();
}

const ResStringPool_span* ResStringPool::styleAt(const ResStringPool_ref& ref) const
{
    return styleAt(ref.index);
}

const ResStringPool_span* ResStringPool::styleAt(size_t idx) const
{
    if (mError == NO_ERROR && idx < mHeader->styleCount) {
        const uint32_t off = (mEntryStyles[idx]/sizeof(uint32_t));
        if (off < mStylePoolSize) {
            return (const ResStringPool_span*)(mStyles+off);
        } else {
            ALOGW("Bad string block: style #%d entry is at %d, past end at %d\n",
                    (int)idx, (int)(off*sizeof(uint32_t)),
                    (int)(mStylePoolSize*sizeof(uint32_t)));
        }
    }
    return NULL;
}

ssize_t ResStringPool::indexOfString(const char16_t* str, size_t strLen) const
{
    if (mError != NO_ERROR) {
        return mError;
    }

    size_t len;

    if ((mHeader->flags&ResStringPool_header::UTF8_FLAG) != 0) {
        STRING_POOL_NOISY(ALOGI("indexOfString UTF-8: %s", String8(str, strLen).string()));

        // The string pool contains UTF 8 strings; we don't want to cause
        // temporary UTF-16 strings to be created as we search.
        if (mHeader->flags&ResStringPool_header::SORTED_FLAG) {
            // Do a binary search for the string...  this is a little tricky,
            // because the strings are sorted with strzcmp16().  So to match
            // the ordering, we need to convert strings in the pool to UTF-16.
            // But we don't want to hit the cache, so instead we will have a
            // local temporary allocation for the conversions.
            char16_t* convBuffer = (char16_t*)malloc(strLen+4);
            ssize_t l = 0;
            ssize_t h = mHeader->stringCount-1;

            ssize_t mid;
            while (l <= h) {
                mid = l + (h - l)/2;
                const uint8_t* s = (const uint8_t*)string8At(mid, &len);
                int c;
                if (s != NULL) {
                    char16_t* end = utf8_to_utf16_n(s, len, convBuffer, strLen+3);
                    *end = 0;
                    c = strzcmp16(convBuffer, end-convBuffer, str, strLen);
                } else {
                    c = -1;
                }
                STRING_POOL_NOISY(ALOGI("Looking at %s, cmp=%d, l/mid/h=%d/%d/%d\n",
                             (const char*)s, c, (int)l, (int)mid, (int)h));
                if (c == 0) {
                    STRING_POOL_NOISY(ALOGI("MATCH!"));
                    free(convBuffer);
                    return mid;
                } else if (c < 0) {
                    l = mid + 1;
                } else {
                    h = mid - 1;
                }
            }
            free(convBuffer);
        } else {
            // It is unusual to get the ID from an unsorted string block...
            // most often this happens because we want to get IDs for style
            // span tags; since those always appear at the end of the string
            // block, start searching at the back.
            String8 str8(str, strLen);
            const size_t str8Len = str8.size();
            for (int i=mHeader->stringCount-1; i>=0; i--) {
                const char* s = string8At(i, &len);
                STRING_POOL_NOISY(ALOGI("Looking at %s, i=%d\n",
                             String8(s).string(),
                             i));
                if (s && str8Len == len && memcmp(s, str8.string(), str8Len) == 0) {
                    STRING_POOL_NOISY(ALOGI("MATCH!"));
                    return i;
                }
            }
        }

    } else {
        STRING_POOL_NOISY(ALOGI("indexOfString UTF-16: %s", String8(str, strLen).string()));

        if (mHeader->flags&ResStringPool_header::SORTED_FLAG) {
            // Do a binary search for the string...
            ssize_t l = 0;
            ssize_t h = mHeader->stringCount-1;

            ssize_t mid;
            while (l <= h) {
                mid = l + (h - l)/2;
                const char16_t* s = stringAt(mid, &len);
                int c = s ? strzcmp16(s, len, str, strLen) : -1;
                STRING_POOL_NOISY(ALOGI("Looking at %s, cmp=%d, l/mid/h=%d/%d/%d\n",
                             String8(s).string(),
                             c, (int)l, (int)mid, (int)h));
                if (c == 0) {
                    STRING_POOL_NOISY(ALOGI("MATCH!"));
                    return mid;
                } else if (c < 0) {
                    l = mid + 1;
                } else {
                    h = mid - 1;
                }
            }
        } else {
            // It is unusual to get the ID from an unsorted string block...
            // most often this happens because we want to get IDs for style
            // span tags; since those always appear at the end of the string
            // block, start searching at the back.
            for (int i=mHeader->stringCount-1; i>=0; i--) {
                const char16_t* s = stringAt(i, &len);
                STRING_POOL_NOISY(ALOGI("Looking at %s, i=%d\n",
                             String8(s).string(),
                             i));
                if (s && strLen == len && strzcmp16(s, len, str, strLen) == 0) {
                    STRING_POOL_NOISY(ALOGI("MATCH!"));
                    return i;
                }
            }
        }
    }

    return NAME_NOT_FOUND;
}

size_t ResStringPool::size() const
{
    return (mError == NO_ERROR) ? mHeader->stringCount : 0;
}

size_t ResStringPool::styleCount() const
{
    return (mError == NO_ERROR) ? mHeader->styleCount : 0;
}

size_t ResStringPool::bytes() const
{
    return (mError == NO_ERROR) ? mHeader->header.size : 0;
}

bool ResStringPool::isSorted() const
{
    return (mHeader->flags&ResStringPool_header::SORTED_FLAG)!=0;
}

bool ResStringPool::isUTF8() const
{
    return (mHeader->flags&ResStringPool_header::UTF8_FLAG)!=0;
}

// --------------------------------------------------------------------
// --------------------------------------------------------------------
// --------------------------------------------------------------------

ResXMLParser::ResXMLParser(const ResXMLTree& tree)
    : mTree(tree), mEventCode(BAD_DOCUMENT)
{
}

void ResXMLParser::restart()
{
    mCurNode = NULL;
    mEventCode = mTree.mError == NO_ERROR ? START_DOCUMENT : BAD_DOCUMENT;
}
const ResStringPool& ResXMLParser::getStrings() const
{
    return mTree.mStrings;
}

ResXMLParser::event_code_t ResXMLParser::getEventType() const
{
    return mEventCode;
}

ResXMLParser::event_code_t ResXMLParser::next()
{
    if (mEventCode == START_DOCUMENT) {
        mCurNode = mTree.mRootNode;
        mCurExt = mTree.mRootExt;
        return (mEventCode=mTree.mRootCode);
    } else if (mEventCode >= FIRST_CHUNK_CODE) {
        return nextNode();
    }
    return mEventCode;
}

int32_t ResXMLParser::getCommentID() const
{
    return mCurNode != NULL ? dtohl(mCurNode->comment.index) : -1;
}

const char16_t* ResXMLParser::getComment(size_t* outLen) const
{
    int32_t id = getCommentID();
    return id >= 0 ? mTree.mStrings.stringAt(id, outLen) : NULL;
}

uint32_t ResXMLParser::getLineNumber() const
{
    return mCurNode != NULL ? dtohl(mCurNode->lineNumber) : -1;
}

int32_t ResXMLParser::getTextID() const
{
    if (mEventCode == TEXT) {
        return dtohl(((const ResXMLTree_cdataExt*)mCurExt)->data.index);
    }
    return -1;
}

const char16_t* ResXMLParser::getText(size_t* outLen) const
{
    int32_t id = getTextID();
    return id >= 0 ? mTree.mStrings.stringAt(id, outLen) : NULL;
}

ssize_t ResXMLParser::getTextValue(Res_value* outValue) const
{
    if (mEventCode == TEXT) {
        outValue->copyFrom_dtoh(((const ResXMLTree_cdataExt*)mCurExt)->typedData);
        return sizeof(Res_value);
    }
    return BAD_TYPE;
}

int32_t ResXMLParser::getNamespacePrefixID() const
{
    if (mEventCode == START_NAMESPACE || mEventCode == END_NAMESPACE) {
        return dtohl(((const ResXMLTree_namespaceExt*)mCurExt)->prefix.index);
    }
    return -1;
}

const char16_t* ResXMLParser::getNamespacePrefix(size_t* outLen) const
{
    int32_t id = getNamespacePrefixID();
    //printf("prefix=%d  event=%p\n", id, mEventCode);
    return id >= 0 ? mTree.mStrings.stringAt(id, outLen) : NULL;
}

int32_t ResXMLParser::getNamespaceUriID() const
{
    if (mEventCode == START_NAMESPACE || mEventCode == END_NAMESPACE) {
        return dtohl(((const ResXMLTree_namespaceExt*)mCurExt)->uri.index);
    }
    return -1;
}

const char16_t* ResXMLParser::getNamespaceUri(size_t* outLen) const
{
    int32_t id = getNamespaceUriID();
    //printf("uri=%d  event=%p\n", id, mEventCode);
    return id >= 0 ? mTree.mStrings.stringAt(id, outLen) : NULL;
}

int32_t ResXMLParser::getElementNamespaceID() const
{
    if (mEventCode == START_TAG) {
        return dtohl(((const ResXMLTree_attrExt*)mCurExt)->ns.index);
    }
    if (mEventCode == END_TAG) {
        return dtohl(((const ResXMLTree_endElementExt*)mCurExt)->ns.index);
    }
    return -1;
}

const char16_t* ResXMLParser::getElementNamespace(size_t* outLen) const
{
    int32_t id = getElementNamespaceID();
    return id >= 0 ? mTree.mStrings.stringAt(id, outLen) : NULL;
}

int32_t ResXMLParser::getElementNameID() const
{
    if (mEventCode == START_TAG) {
        return dtohl(((const ResXMLTree_attrExt*)mCurExt)->name.index);
    }
    if (mEventCode == END_TAG) {
        return dtohl(((const ResXMLTree_endElementExt*)mCurExt)->name.index);
    }
    return -1;
}

const char16_t* ResXMLParser::getElementName(size_t* outLen) const
{
    int32_t id = getElementNameID();
    return id >= 0 ? mTree.mStrings.stringAt(id, outLen) : NULL;
}

size_t ResXMLParser::getAttributeCount() const
{
    if (mEventCode == START_TAG) {
        return dtohs(((const ResXMLTree_attrExt*)mCurExt)->attributeCount);
    }
    return 0;
}

int32_t ResXMLParser::getAttributeNamespaceID(size_t idx) const
{
    if (mEventCode == START_TAG) {
        const ResXMLTree_attrExt* tag = (const ResXMLTree_attrExt*)mCurExt;
        if (idx < dtohs(tag->attributeCount)) {
            const ResXMLTree_attribute* attr = (const ResXMLTree_attribute*)
                (((const uint8_t*)tag)
                 + dtohs(tag->attributeStart)
                 + (dtohs(tag->attributeSize)*idx));
            return dtohl(attr->ns.index);
        }
    }
    return -2;
}

const char16_t* ResXMLParser::getAttributeNamespace(size_t idx, size_t* outLen) const
{
    int32_t id = getAttributeNamespaceID(idx);
    //printf("attribute namespace=%d  idx=%d  event=%p\n", id, idx, mEventCode);
    //XML_NOISY(printf("getAttributeNamespace 0x%x=0x%x\n", idx, id));
    return id >= 0 ? mTree.mStrings.stringAt(id, outLen) : NULL;
}

const char* ResXMLParser::getAttributeNamespace8(size_t idx, size_t* outLen) const
{
    int32_t id = getAttributeNamespaceID(idx);
    //printf("attribute namespace=%d  idx=%d  event=%p\n", id, idx, mEventCode);
    //XML_NOISY(printf("getAttributeNamespace 0x%x=0x%x\n", idx, id));
    return id >= 0 ? mTree.mStrings.string8At(id, outLen) : NULL;
}

int32_t ResXMLParser::getAttributeNameID(size_t idx) const
{
    if (mEventCode == START_TAG) {
        const ResXMLTree_attrExt* tag = (const ResXMLTree_attrExt*)mCurExt;
        if (idx < dtohs(tag->attributeCount)) {
            const ResXMLTree_attribute* attr = (const ResXMLTree_attribute*)
                (((const uint8_t*)tag)
                 + dtohs(tag->attributeStart)
                 + (dtohs(tag->attributeSize)*idx));
            return dtohl(attr->name.index);
        }
    }
    return -1;
}

const char16_t* ResXMLParser::getAttributeName(size_t idx, size_t* outLen) const
{
    int32_t id = getAttributeNameID(idx);
    //printf("attribute name=%d  idx=%d  event=%p\n", id, idx, mEventCode);
    //XML_NOISY(printf("getAttributeName 0x%x=0x%x\n", idx, id));
    return id >= 0 ? mTree.mStrings.stringAt(id, outLen) : NULL;
}

const char* ResXMLParser::getAttributeName8(size_t idx, size_t* outLen) const
{
    int32_t id = getAttributeNameID(idx);
    //printf("attribute name=%d  idx=%d  event=%p\n", id, idx, mEventCode);
    //XML_NOISY(printf("getAttributeName 0x%x=0x%x\n", idx, id));
    return id >= 0 ? mTree.mStrings.string8At(id, outLen) : NULL;
}

uint32_t ResXMLParser::getAttributeNameResID(size_t idx) const
{
    int32_t id = getAttributeNameID(idx);
    if (id >= 0 && (size_t)id < mTree.mNumResIds) {
        uint32_t resId = dtohl(mTree.mResIds[id]);
        return resId;
    }
    return 0;
}

int32_t ResXMLParser::getAttributeValueStringID(size_t idx) const
{
    if (mEventCode == START_TAG) {
        const ResXMLTree_attrExt* tag = (const ResXMLTree_attrExt*)mCurExt;
        if (idx < dtohs(tag->attributeCount)) {
            const ResXMLTree_attribute* attr = (const ResXMLTree_attribute*)
                (((const uint8_t*)tag)
                 + dtohs(tag->attributeStart)
                 + (dtohs(tag->attributeSize)*idx));
            return dtohl(attr->rawValue.index);
        }
    }
    return -1;
}

const char16_t* ResXMLParser::getAttributeStringValue(size_t idx, size_t* outLen) const
{
    int32_t id = getAttributeValueStringID(idx);
    //XML_NOISY(printf("getAttributeValue 0x%x=0x%x\n", idx, id));
    return id >= 0 ? mTree.mStrings.stringAt(id, outLen) : NULL;
}

int32_t ResXMLParser::getAttributeDataType(size_t idx) const
{
    if (mEventCode == START_TAG) {
        const ResXMLTree_attrExt* tag = (const ResXMLTree_attrExt*)mCurExt;
        if (idx < dtohs(tag->attributeCount)) {
            const ResXMLTree_attribute* attr = (const ResXMLTree_attribute*)
                (((const uint8_t*)tag)
                 + dtohs(tag->attributeStart)
                 + (dtohs(tag->attributeSize)*idx));
            uint8_t type = attr->typedValue.dataType;
            if (type != Res_value::TYPE_DYNAMIC_REFERENCE) {
                return type;
            }

            // This is a dynamic reference. We adjust those references
            // to regular references at this level, so lie to the caller.
            return Res_value::TYPE_REFERENCE;
        }
    }
    return Res_value::TYPE_NULL;
}

int32_t ResXMLParser::getAttributeData(size_t idx) const
{
    if (mEventCode == START_TAG) {
        const ResXMLTree_attrExt* tag = (const ResXMLTree_attrExt*)mCurExt;
        if (idx < dtohs(tag->attributeCount)) {
            const ResXMLTree_attribute* attr = (const ResXMLTree_attribute*)
                (((const uint8_t*)tag)
                 + dtohs(tag->attributeStart)
                 + (dtohs(tag->attributeSize)*idx));
            return dtohl(attr->typedValue.data);
        }
    }
    return 0;
}

ssize_t ResXMLParser::getAttributeValue(size_t idx, Res_value* outValue) const
{
    if (mEventCode == START_TAG) {
        const ResXMLTree_attrExt* tag = (const ResXMLTree_attrExt*)mCurExt;
        if (idx < dtohs(tag->attributeCount)) {
            const ResXMLTree_attribute* attr = (const ResXMLTree_attribute*)
                (((const uint8_t*)tag)
                 + dtohs(tag->attributeStart)
                 + (dtohs(tag->attributeSize)*idx));
            outValue->copyFrom_dtoh(attr->typedValue);
            return sizeof(Res_value);
        }
    }
    return BAD_TYPE;
}

ssize_t ResXMLParser::indexOfAttribute(const char* ns, const char* attr) const
{
    String16 nsStr(ns != NULL ? ns : "");
    String16 attrStr(attr);
    return indexOfAttribute(ns ? nsStr.string() : NULL, ns ? nsStr.size() : 0,
                            attrStr.string(), attrStr.size());
}

ssize_t ResXMLParser::indexOfAttribute(const char16_t* ns, size_t nsLen,
                                       const char16_t* attr, size_t attrLen) const
{
    if (mEventCode == START_TAG) {
        if (attr == NULL) {
            return NAME_NOT_FOUND;
        }
        const size_t N = getAttributeCount();
        if (mTree.mStrings.isUTF8()) {
            String8 ns8, attr8;
            if (ns != NULL) {
                ns8 = String8(ns, nsLen);
            }
            attr8 = String8(attr, attrLen);
            STRING_POOL_NOISY(ALOGI("indexOfAttribute UTF8 %s (%d) / %s (%d)", ns8.string(), nsLen,
                    attr8.string(), attrLen));
            for (size_t i=0; i<N; i++) {
                size_t curNsLen = 0, curAttrLen = 0;
                const char* curNs = getAttributeNamespace8(i, &curNsLen);
                const char* curAttr = getAttributeName8(i, &curAttrLen);
                STRING_POOL_NOISY(ALOGI("  curNs=%s (%d), curAttr=%s (%d)", curNs, curNsLen,
                        curAttr, curAttrLen));
                if (curAttr != NULL && curNsLen == nsLen && curAttrLen == attrLen
                        && memcmp(attr8.string(), curAttr, attrLen) == 0) {
                    if (ns == NULL) {
                        if (curNs == NULL) {
                            STRING_POOL_NOISY(ALOGI("  FOUND!"));
                            return i;
                        }
                    } else if (curNs != NULL) {
                        //printf(" --> ns=%s, curNs=%s\n",
                        //       String8(ns).string(), String8(curNs).string());
                        if (memcmp(ns8.string(), curNs, nsLen) == 0) {
                            STRING_POOL_NOISY(ALOGI("  FOUND!"));
                            return i;
                        }
                    }
                }
            }
        } else {
            STRING_POOL_NOISY(ALOGI("indexOfAttribute UTF16 %s (%d) / %s (%d)",
                    String8(ns, nsLen).string(), nsLen,
                    String8(attr, attrLen).string(), attrLen));
            for (size_t i=0; i<N; i++) {
                size_t curNsLen = 0, curAttrLen = 0;
                const char16_t* curNs = getAttributeNamespace(i, &curNsLen);
                const char16_t* curAttr = getAttributeName(i, &curAttrLen);
                STRING_POOL_NOISY(ALOGI("  curNs=%s (%d), curAttr=%s (%d)",
                        String8(curNs, curNsLen).string(), curNsLen,
                        String8(curAttr, curAttrLen).string(), curAttrLen));
                if (curAttr != NULL && curNsLen == nsLen && curAttrLen == attrLen
                        && (memcmp(attr, curAttr, attrLen*sizeof(char16_t)) == 0)) {
                    if (ns == NULL) {
                        if (curNs == NULL) {
                            STRING_POOL_NOISY(ALOGI("  FOUND!"));
                            return i;
                        }
                    } else if (curNs != NULL) {
                        //printf(" --> ns=%s, curNs=%s\n",
                        //       String8(ns).string(), String8(curNs).string());
                        if (memcmp(ns, curNs, nsLen*sizeof(char16_t)) == 0) {
                            STRING_POOL_NOISY(ALOGI("  FOUND!"));
                            return i;
                        }
                    }
                }
            }
        }
    }

    return NAME_NOT_FOUND;
}

ssize_t ResXMLParser::indexOfID() const
{
    if (mEventCode == START_TAG) {
        const ssize_t idx = dtohs(((const ResXMLTree_attrExt*)mCurExt)->idIndex);
        if (idx > 0) return (idx-1);
    }
    return NAME_NOT_FOUND;
}

ssize_t ResXMLParser::indexOfClass() const
{
    if (mEventCode == START_TAG) {
        const ssize_t idx = dtohs(((const ResXMLTree_attrExt*)mCurExt)->classIndex);
        if (idx > 0) return (idx-1);
    }
    return NAME_NOT_FOUND;
}

ssize_t ResXMLParser::indexOfStyle() const
{
    if (mEventCode == START_TAG) {
        const ssize_t idx = dtohs(((const ResXMLTree_attrExt*)mCurExt)->styleIndex);
        if (idx > 0) return (idx-1);
    }
    return NAME_NOT_FOUND;
}

ResXMLParser::event_code_t ResXMLParser::nextNode()
{
    if (mEventCode < 0) {
        return mEventCode;
    }

    do {
        const ResXMLTree_node* next = (const ResXMLTree_node*)
            (((const uint8_t*)mCurNode) + dtohl(mCurNode->header.size));
        //ALOGW("Next node: prev=%p, next=%p\n", mCurNode, next);

        if (((const uint8_t*)next) >= mTree.mDataEnd) {
            mCurNode = NULL;
            return (mEventCode=END_DOCUMENT);
        }

        if (mTree.validateNode(next) != NO_ERROR) {
            mCurNode = NULL;
            return (mEventCode=BAD_DOCUMENT);
        }

        mCurNode = next;
        const uint16_t headerSize = dtohs(next->header.headerSize);
        const uint32_t totalSize = dtohl(next->header.size);
        mCurExt = ((const uint8_t*)next) + headerSize;
        size_t minExtSize = 0;
        event_code_t eventCode = (event_code_t)dtohs(next->header.type);
        switch ((mEventCode=eventCode)) {
            case RES_XML_START_NAMESPACE_TYPE:
            case RES_XML_END_NAMESPACE_TYPE:
                minExtSize = sizeof(ResXMLTree_namespaceExt);
                break;
            case RES_XML_START_ELEMENT_TYPE:
                minExtSize = sizeof(ResXMLTree_attrExt);
                break;
            case RES_XML_END_ELEMENT_TYPE:
                minExtSize = sizeof(ResXMLTree_endElementExt);
                break;
            case RES_XML_CDATA_TYPE:
                minExtSize = sizeof(ResXMLTree_cdataExt);
                break;
            default:
                ALOGW("Unknown XML block: header type %d in node at %d\n",
                     (int)dtohs(next->header.type),
                     (int)(((const uint8_t*)next)-((const uint8_t*)mTree.mHeader)));
                continue;
        }

        if ((totalSize-headerSize) < minExtSize) {
            ALOGW("Bad XML block: header type 0x%x in node at 0x%x has size %d, need %d\n",
                 (int)dtohs(next->header.type),
                 (int)(((const uint8_t*)next)-((const uint8_t*)mTree.mHeader)),
                 (int)(totalSize-headerSize), (int)minExtSize);
            return (mEventCode=BAD_DOCUMENT);
        }

        //printf("CurNode=%p, CurExt=%p, headerSize=%d, minExtSize=%d\n",
        //       mCurNode, mCurExt, headerSize, minExtSize);

        return eventCode;
    } while (true);
}

void ResXMLParser::getPosition(ResXMLParser::ResXMLPosition* pos) const
{
    pos->eventCode = mEventCode;
    pos->curNode = mCurNode;
    pos->curExt = mCurExt;
}

void ResXMLParser::setPosition(const ResXMLParser::ResXMLPosition& pos)
{
    mEventCode = pos.eventCode;
    mCurNode = pos.curNode;
    mCurExt = pos.curExt;
}

// --------------------------------------------------------------------

static volatile int32_t gCount = 0;

ResXMLTree::ResXMLTree()
    : ResXMLParser(*this)
    , mError(NO_INIT), mOwnedData(NULL)
{
    //ALOGI("Creating ResXMLTree %p #%d\n", this, android_atomic_inc(&gCount)+1);
    restart();
}

ResXMLTree::~ResXMLTree()
{
    //ALOGI("Destroying ResXMLTree in %p #%d\n", this, android_atomic_dec(&gCount)-1);
    uninit();
}

status_t ResXMLTree::setTo(const void* data, size_t size, bool copyData)
{
    uninit();
    mEventCode = START_DOCUMENT;

    if (!data || !size) {
        return (mError=BAD_TYPE);
    }

    if (copyData) {
        mOwnedData = malloc(size);
        if (mOwnedData == NULL) {
            return (mError=NO_MEMORY);
        }
        memcpy(mOwnedData, data, size);
        data = mOwnedData;
    }

    mHeader = (const ResXMLTree_header*)data;
    mSize = dtohl(mHeader->header.size);
    if (dtohs(mHeader->header.headerSize) > mSize || mSize > size) {
        ALOGW("Bad XML block: header size %d or total size %d is larger than data size %d\n",
             (int)dtohs(mHeader->header.headerSize),
             (int)dtohl(mHeader->header.size), (int)size);
        mError = BAD_TYPE;
        restart();
        return mError;
    }
    mDataEnd = ((const uint8_t*)mHeader) + mSize;

    mStrings.uninit();
    mRootNode = NULL;
    mResIds = NULL;
    mNumResIds = 0;

    // First look for a couple interesting chunks: the string block
    // and first XML node.
    const ResChunk_header* chunk =
        (const ResChunk_header*)(((const uint8_t*)mHeader) + dtohs(mHeader->header.headerSize));
    const ResChunk_header* lastChunk = chunk;
    while (((const uint8_t*)chunk) < (mDataEnd-sizeof(ResChunk_header)) &&
           ((const uint8_t*)chunk) < (mDataEnd-dtohl(chunk->size))) {
        status_t err = validate_chunk(chunk, sizeof(ResChunk_header), mDataEnd, "XML");
        if (err != NO_ERROR) {
            mError = err;
            goto done;
        }
        const uint16_t type = dtohs(chunk->type);
        const size_t size = dtohl(chunk->size);
        XML_NOISY(printf("Scanning @ %p: type=0x%x, size=0x%x\n",
                     (void*)(((uint32_t)chunk)-((uint32_t)mHeader)), type, size));
        if (type == RES_STRING_POOL_TYPE) {
            mStrings.setTo(chunk, size);
        } else if (type == RES_XML_RESOURCE_MAP_TYPE) {
            mResIds = (const uint32_t*)
                (((const uint8_t*)chunk)+dtohs(chunk->headerSize));
            mNumResIds = (dtohl(chunk->size)-dtohs(chunk->headerSize))/sizeof(uint32_t);
        } else if (type >= RES_XML_FIRST_CHUNK_TYPE
                   && type <= RES_XML_LAST_CHUNK_TYPE) {
            if (validateNode((const ResXMLTree_node*)chunk) != NO_ERROR) {
                mError = BAD_TYPE;
                goto done;
            }
            mCurNode = (const ResXMLTree_node*)lastChunk;
            if (nextNode() == BAD_DOCUMENT) {
                mError = BAD_TYPE;
                goto done;
            }
            mRootNode = mCurNode;
            mRootExt = mCurExt;
            mRootCode = mEventCode;
            break;
        } else {
            XML_NOISY(printf("Skipping unknown chunk!\n"));
        }
        lastChunk = chunk;
        chunk = (const ResChunk_header*)
            (((const uint8_t*)chunk) + size);
    }

    if (mRootNode == NULL) {
        ALOGW("Bad XML block: no root element node found\n");
        mError = BAD_TYPE;
        goto done;
    }

    mError = mStrings.getError();

done:
    restart();
    return mError;
}

status_t ResXMLTree::getError() const
{
    return mError;
}

void ResXMLTree::uninit()
{
    mError = NO_INIT;
    mStrings.uninit();
    if (mOwnedData) {
        free(mOwnedData);
        mOwnedData = NULL;
    }
    restart();
}

status_t ResXMLTree::validateNode(const ResXMLTree_node* node) const
{
    const uint16_t eventCode = dtohs(node->header.type);

    status_t err = validate_chunk(
        &node->header, sizeof(ResXMLTree_node),
        mDataEnd, "ResXMLTree_node");

    if (err >= NO_ERROR) {
        // Only perform additional validation on START nodes
        if (eventCode != RES_XML_START_ELEMENT_TYPE) {
            return NO_ERROR;
        }

        const uint16_t headerSize = dtohs(node->header.headerSize);
        const uint32_t size = dtohl(node->header.size);
        const ResXMLTree_attrExt* attrExt = (const ResXMLTree_attrExt*)
            (((const uint8_t*)node) + headerSize);
        // check for sensical values pulled out of the stream so far...
        if ((size >= headerSize + sizeof(ResXMLTree_attrExt))
                && ((void*)attrExt > (void*)node)) {
            const size_t attrSize = ((size_t)dtohs(attrExt->attributeSize))
                * dtohs(attrExt->attributeCount);
            if ((dtohs(attrExt->attributeStart)+attrSize) <= (size-headerSize)) {
                return NO_ERROR;
            }
            ALOGW("Bad XML block: node attributes use 0x%x bytes, only have 0x%x bytes\n",
                    (unsigned int)(dtohs(attrExt->attributeStart)+attrSize),
                    (unsigned int)(size-headerSize));
        }
        else {
            ALOGW("Bad XML start block: node header size 0x%x, size 0x%x\n",
                (unsigned int)headerSize, (unsigned int)size);
        }
        return BAD_TYPE;
    }

    return err;

#if 0
    const bool isStart = dtohs(node->header.type) == RES_XML_START_ELEMENT_TYPE;

    const uint16_t headerSize = dtohs(node->header.headerSize);
    const uint32_t size = dtohl(node->header.size);

    if (headerSize >= (isStart ? sizeof(ResXMLTree_attrNode) : sizeof(ResXMLTree_node))) {
        if (size >= headerSize) {
            if (((const uint8_t*)node) <= (mDataEnd-size)) {
                if (!isStart) {
                    return NO_ERROR;
                }
                if ((((size_t)dtohs(node->attributeSize))*dtohs(node->attributeCount))
                        <= (size-headerSize)) {
                    return NO_ERROR;
                }
                ALOGW("Bad XML block: node attributes use 0x%x bytes, only have 0x%x bytes\n",
                        ((int)dtohs(node->attributeSize))*dtohs(node->attributeCount),
                        (int)(size-headerSize));
                return BAD_TYPE;
            }
            ALOGW("Bad XML block: node at 0x%x extends beyond data end 0x%x\n",
                    (int)(((const uint8_t*)node)-((const uint8_t*)mHeader)), (int)mSize);
            return BAD_TYPE;
        }
        ALOGW("Bad XML block: node at 0x%x header size 0x%x smaller than total size 0x%x\n",
                (int)(((const uint8_t*)node)-((const uint8_t*)mHeader)),
                (int)headerSize, (int)size);
        return BAD_TYPE;
    }
    ALOGW("Bad XML block: node at 0x%x header size 0x%x too small\n",
            (int)(((const uint8_t*)node)-((const uint8_t*)mHeader)),
            (int)headerSize);
    return BAD_TYPE;
#endif
}

// --------------------------------------------------------------------
// --------------------------------------------------------------------
// --------------------------------------------------------------------

StringPoolRef::StringPoolRef(const ResStringPool* pool, uint32_t index)
    : mPool(pool), mIndex(index) {}

StringPoolRef::StringPoolRef()
    : mPool(NULL), mIndex(0) {}

const char* StringPoolRef::string8(size_t* outLen) const {
    if (mPool != NULL) {
        return mPool->string8At(mIndex, outLen);
    }
    if (outLen != NULL) {
        *outLen = 0;
    }
    return NULL;
}

const char16_t* StringPoolRef::string16(size_t* outLen) const {
    if (mPool != NULL) {
        return mPool->stringAt(mIndex, outLen);
    }
    if (outLen != NULL) {
        *outLen = 0;
    }
    return NULL;
}

}   // namespace android
