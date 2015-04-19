/*
 * Copyright (C) 2005 The Android Open Source Project
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

//
// Definitions of resource data structures.
//
#ifndef _LIBS_UTILS_RESOURCE_TYPES_H
#define _LIBS_UTILS_RESOURCE_TYPES_H

#include <mutex>

#include <utils/String16.h>

#include <stdint.h>
#include <sys/types.h>

namespace android {

/**
 * In C++11, char16_t is defined as *at least* 16 bits. We do a lot of
 * casting on raw data and expect char16_t to be exactly 16 bits.
 */
#if __cplusplus >= 201103L
struct __assertChar16Size {
    static_assert(sizeof(char16_t) == sizeof(uint16_t), "char16_t is not 16 bits");
    static_assert(alignof(char16_t) == alignof(uint16_t), "char16_t is not 16-bit aligned");
};
#endif

/** ********************************************************************
 *  Base Types
 *
 *  These are standard types that are shared between multiple specific
 *  resource types.
 *
 *********************************************************************** */

/**
 * Header that appears at the front of every data chunk in a resource.
 */
struct ResChunk_header
{
    // Type identifier for this chunk.  The meaning of this value depends
    // on the containing chunk.
    uint16_t type;

    // Size of the chunk header (in bytes).  Adding this value to
    // the address of the chunk allows you to find its associated data
    // (if any).
    uint16_t headerSize;

    // Total size of this chunk (in bytes).  This is the chunkSize plus
    // the size of any data associated with the chunk.  Adding this value
    // to the chunk allows you to completely skip its contents (including
    // any child chunks).  If this value is the same as chunkSize, there is
    // no data associated with the chunk.
    uint32_t size;
};

enum {
    RES_NULL_TYPE               = 0x0000,
    RES_STRING_POOL_TYPE        = 0x0001,
    RES_TABLE_TYPE              = 0x0002,
    RES_XML_TYPE                = 0x0003,

    // Chunk types in RES_XML_TYPE
    RES_XML_FIRST_CHUNK_TYPE    = 0x0100,
    RES_XML_START_NAMESPACE_TYPE= 0x0100,
    RES_XML_END_NAMESPACE_TYPE  = 0x0101,
    RES_XML_START_ELEMENT_TYPE  = 0x0102,
    RES_XML_END_ELEMENT_TYPE    = 0x0103,
    RES_XML_CDATA_TYPE          = 0x0104,
    RES_XML_LAST_CHUNK_TYPE     = 0x017f,
    // This contains a uint32_t array mapping strings in the string
    // pool back to resource identifiers.  It is optional.
    RES_XML_RESOURCE_MAP_TYPE   = 0x0180,

    // Chunk types in RES_TABLE_TYPE
    RES_TABLE_PACKAGE_TYPE      = 0x0200,
    RES_TABLE_TYPE_TYPE         = 0x0201,
    RES_TABLE_TYPE_SPEC_TYPE    = 0x0202,
    RES_TABLE_LIBRARY_TYPE      = 0x0203
};

/**
 * Macros for building/splitting resource identifiers.
 */
#define Res_VALIDID(resid) (resid != 0)
#define Res_CHECKID(resid) ((resid&0xFFFF0000) != 0)
#define Res_MAKEID(package, type, entry) \
    (((package+1)<<24) | (((type+1)&0xFF)<<16) | (entry&0xFFFF))
#define Res_GETPACKAGE(id) ((id>>24)-1)
#define Res_GETTYPE(id) (((id>>16)&0xFF)-1)
#define Res_GETENTRY(id) (id&0xFFFF)

#define Res_INTERNALID(resid) ((resid&0xFFFF0000) != 0 && (resid&0xFF0000) == 0)
#define Res_MAKEINTERNAL(entry) (0x01000000 | (entry&0xFFFF))
#define Res_MAKEARRAY(entry) (0x02000000 | (entry&0xFFFF))

#define Res_MAXPACKAGE 255
#define Res_MAXTYPE 255

/**
 * Representation of a value in a resource, supplying type
 * information.
 */
struct Res_value
{
    // Number of bytes in this structure.
    uint16_t size;

    // Always set to 0.
    uint8_t res0;
        
    // Type of the data value.
    enum {
        // The 'data' is either 0 or 1, specifying this resource is either
        // undefined or empty, respectively.
        TYPE_NULL = 0x00,
        // The 'data' holds a ResTable_ref, a reference to another resource
        // table entry.
        TYPE_REFERENCE = 0x01,
        // The 'data' holds an attribute resource identifier.
        TYPE_ATTRIBUTE = 0x02,
        // The 'data' holds an index into the containing resource table's
        // global value string pool.
        TYPE_STRING = 0x03,
        // The 'data' holds a single-precision floating point number.
        TYPE_FLOAT = 0x04,
        // The 'data' holds a complex number encoding a dimension value,
        // such as "100in".
        TYPE_DIMENSION = 0x05,
        // The 'data' holds a complex number encoding a fraction of a
        // container.
        TYPE_FRACTION = 0x06,
        // The 'data' holds a dynamic ResTable_ref, which needs to be
        // resolved before it can be used like a TYPE_REFERENCE.
        TYPE_DYNAMIC_REFERENCE = 0x07,

        // Beginning of integer flavors...
        TYPE_FIRST_INT = 0x10,

        // The 'data' is a raw integer value of the form n..n.
        TYPE_INT_DEC = 0x10,
        // The 'data' is a raw integer value of the form 0xn..n.
        TYPE_INT_HEX = 0x11,
        // The 'data' is either 0 or 1, for input "false" or "true" respectively.
        TYPE_INT_BOOLEAN = 0x12,

        // Beginning of color integer flavors...
        TYPE_FIRST_COLOR_INT = 0x1c,

        // The 'data' is a raw integer value of the form #aarrggbb.
        TYPE_INT_COLOR_ARGB8 = 0x1c,
        // The 'data' is a raw integer value of the form #rrggbb.
        TYPE_INT_COLOR_RGB8 = 0x1d,
        // The 'data' is a raw integer value of the form #argb.
        TYPE_INT_COLOR_ARGB4 = 0x1e,
        // The 'data' is a raw integer value of the form #rgb.
        TYPE_INT_COLOR_RGB4 = 0x1f,

        // ...end of integer flavors.
        TYPE_LAST_COLOR_INT = 0x1f,

        // ...end of integer flavors.
        TYPE_LAST_INT = 0x1f
    };
    uint8_t dataType;

    // Structure of complex data values (TYPE_UNIT and TYPE_FRACTION)
    enum {
        // Where the unit type information is.  This gives us 16 possible
        // types, as defined below.
        COMPLEX_UNIT_SHIFT = 0,
        COMPLEX_UNIT_MASK = 0xf,

        // TYPE_DIMENSION: Value is raw pixels.
        COMPLEX_UNIT_PX = 0,
        // TYPE_DIMENSION: Value is Device Independent Pixels.
        COMPLEX_UNIT_DIP = 1,
        // TYPE_DIMENSION: Value is a Scaled device independent Pixels.
        COMPLEX_UNIT_SP = 2,
        // TYPE_DIMENSION: Value is in points.
        COMPLEX_UNIT_PT = 3,
        // TYPE_DIMENSION: Value is in inches.
        COMPLEX_UNIT_IN = 4,
        // TYPE_DIMENSION: Value is in millimeters.
        COMPLEX_UNIT_MM = 5,

        // TYPE_FRACTION: A basic fraction of the overall size.
        COMPLEX_UNIT_FRACTION = 0,
        // TYPE_FRACTION: A fraction of the parent size.
        COMPLEX_UNIT_FRACTION_PARENT = 1,

        // Where the radix information is, telling where the decimal place
        // appears in the mantissa.  This give us 4 possible fixed point
        // representations as defined below.
        COMPLEX_RADIX_SHIFT = 4,
        COMPLEX_RADIX_MASK = 0x3,

        // The mantissa is an integral number -- i.e., 0xnnnnnn.0
        COMPLEX_RADIX_23p0 = 0,
        // The mantissa magnitude is 16 bits -- i.e, 0xnnnn.nn
        COMPLEX_RADIX_16p7 = 1,
        // The mantissa magnitude is 8 bits -- i.e, 0xnn.nnnn
        COMPLEX_RADIX_8p15 = 2,
        // The mantissa magnitude is 0 bits -- i.e, 0x0.nnnnnn
        COMPLEX_RADIX_0p23 = 3,

        // Where the actual value is.  This gives us 23 bits of
        // precision.  The top bit is the sign.
        COMPLEX_MANTISSA_SHIFT = 8,
        COMPLEX_MANTISSA_MASK = 0xffffff
    };

    // Possible data values for TYPE_NULL.
    enum {
        // The value is not defined.
        DATA_NULL_UNDEFINED = 0,
        // The value is explicitly defined as empty.
        DATA_NULL_EMPTY = 1
    };

    // The data for this item, as interpreted according to dataType.
    uint32_t data;

    void copyFrom_dtoh(const Res_value& src);
};

/**
 *  This is a reference to a unique entry (a ResTable_entry structure)
 *  in a resource table.  The value is structured as: 0xpptteeee,
 *  where pp is the package index, tt is the type index in that
 *  package, and eeee is the entry index in that type.  The package
 *  and type values start at 1 for the first item, to help catch cases
 *  where they have not been supplied.
 */
struct ResTable_ref
{
    uint32_t ident;
};

/**
 * Reference to a string in a string pool.
 */
struct ResStringPool_ref
{
    // Index into the string pool table (uint32_t-offset from the indices
    // immediately after ResStringPool_header) at which to find the location
    // of the string data in the pool.
    uint32_t index;
};

/** ********************************************************************
 *  String Pool
 *
 *  A set of strings that can be references by others through a
 *  ResStringPool_ref.
 *
 *********************************************************************** */

/**
 * Definition for a pool of strings.  The data of this chunk is an
 * array of uint32_t providing indices into the pool, relative to
 * stringsStart.  At stringsStart are all of the UTF-16 strings
 * concatenated together; each starts with a uint16_t of the string's
 * length and each ends with a 0x0000 terminator.  If a string is >
 * 32767 characters, the high bit of the length is set meaning to take
 * those 15 bits as a high word and it will be followed by another
 * uint16_t containing the low word.
 *
 * If styleCount is not zero, then immediately following the array of
 * uint32_t indices into the string table is another array of indices
 * into a style table starting at stylesStart.  Each entry in the
 * style table is an array of ResStringPool_span structures.
 */
struct ResStringPool_header
{
    struct ResChunk_header header;

    // Number of strings in this pool (number of uint32_t indices that follow
    // in the data).
    uint32_t stringCount;

    // Number of style span arrays in the pool (number of uint32_t indices
    // follow the string indices).
    uint32_t styleCount;

    // Flags.
    enum {
        // If set, the string index is sorted by the string values (based
        // on strcmp16()).
        SORTED_FLAG = 1<<0,

        // String pool is encoded in UTF-8
        UTF8_FLAG = 1<<8
    };
    uint32_t flags;

    // Index from header of the string data.
    uint32_t stringsStart;

    // Index from header of the style data.
    uint32_t stylesStart;
};

/**
 * This structure defines a span of style information associated with
 * a string in the pool.
 */
struct ResStringPool_span
{
    enum {
        END = 0xFFFFFFFF
    };

    // This is the name of the span -- that is, the name of the XML
    // tag that defined it.  The special value END (0xFFFFFFFF) indicates
    // the end of an array of spans.
    ResStringPool_ref name;

    // The range of characters in the string that this span applies to.
    uint32_t firstChar, lastChar;
};

/**
 * Convenience class for accessing data in a ResStringPool resource.
 */
class ResStringPool
{
public:
    ResStringPool();
    ResStringPool(const void* data, size_t size, bool copyData=false);
    ~ResStringPool();

    void setToEmpty();
    status_t setTo(const void* data, size_t size, bool copyData=false);

    status_t getError() const;

    void uninit();

    // Return string entry as UTF16; if the pool is UTF8, the string will
    // be converted before returning.
    inline const char16_t* stringAt(const ResStringPool_ref& ref, size_t* outLen) const {
        return stringAt(ref.index, outLen);
    }
    const char16_t* stringAt(size_t idx, size_t* outLen) const;

    // Note: returns null if the string pool is not UTF8.
    const char* string8At(size_t idx, size_t* outLen) const;

    // Return string whether the pool is UTF8 or UTF16.  Does not allow you
    // to distinguish null.
    const String8 string8ObjectAt(size_t idx) const;

    const ResStringPool_span* styleAt(const ResStringPool_ref& ref) const;
    const ResStringPool_span* styleAt(size_t idx) const;

    ssize_t indexOfString(const char16_t* str, size_t strLen) const;

    size_t size() const;
    size_t styleCount() const;
    size_t bytes() const;

    bool isSorted() const;
    bool isUTF8() const;

private:
    status_t                    mError;
    void*                       mOwnedData;
    const ResStringPool_header* mHeader;
    size_t                      mSize;
    mutable std::mutex          mDecodeLock;
    const uint32_t*             mEntries;
    const uint32_t*             mEntryStyles;
    const void*                 mStrings;
    char16_t mutable**          mCache;
    uint32_t                    mStringPoolSize;    // number of uint16_t
    const uint32_t*             mStyles;
    uint32_t                    mStylePoolSize;    // number of uint32_t
};

/**
 * Wrapper class that allows the caller to retrieve a string from
 * a string pool without knowing which string pool to look.
 */
class StringPoolRef {
public:
    StringPoolRef();
    StringPoolRef(const ResStringPool* pool, uint32_t index);

    const char* string8(size_t* outLen) const;
    const char16_t* string16(size_t* outLen) const;

private:
    const ResStringPool*        mPool;
    uint32_t                    mIndex;
};

/** ********************************************************************
 *  XML Tree
 *
 *  Binary representation of an XML document.  This is designed to
 *  express everything in an XML document, in a form that is much
 *  easier to parse on the device.
 *
 *********************************************************************** */

/**
 * XML tree header.  This appears at the front of an XML tree,
 * describing its content.  It is followed by a flat array of
 * ResXMLTree_node structures; the hierarchy of the XML document
 * is described by the occurrance of RES_XML_START_ELEMENT_TYPE
 * and corresponding RES_XML_END_ELEMENT_TYPE nodes in the array.
 */
struct ResXMLTree_header
{
    struct ResChunk_header header;
};

/**
 * Basic XML tree node.  A single item in the XML document.  Extended info
 * about the node can be found after header.headerSize.
 */
struct ResXMLTree_node
{
    struct ResChunk_header header;

    // Line number in original source file at which this element appeared.
    uint32_t lineNumber;

    // Optional XML comment that was associated with this element; -1 if none.
    struct ResStringPool_ref comment;
};

/**
 * Extended XML tree node for CDATA tags -- includes the CDATA string.
 * Appears header.headerSize bytes after a ResXMLTree_node.
 */
struct ResXMLTree_cdataExt
{
    // The raw CDATA character data.
    struct ResStringPool_ref data;
    
    // The typed value of the character data if this is a CDATA node.
    struct Res_value typedData;
};

/**
 * Extended XML tree node for namespace start/end nodes.
 * Appears header.headerSize bytes after a ResXMLTree_node.
 */
struct ResXMLTree_namespaceExt
{
    // The prefix of the namespace.
    struct ResStringPool_ref prefix;
    
    // The URI of the namespace.
    struct ResStringPool_ref uri;
};

/**
 * Extended XML tree node for element start/end nodes.
 * Appears header.headerSize bytes after a ResXMLTree_node.
 */
struct ResXMLTree_endElementExt
{
    // String of the full namespace of this element.
    struct ResStringPool_ref ns;
    
    // String name of this node if it is an ELEMENT; the raw
    // character data if this is a CDATA node.
    struct ResStringPool_ref name;
};

/**
 * Extended XML tree node for start tags -- includes attribute
 * information.
 * Appears header.headerSize bytes after a ResXMLTree_node.
 */
struct ResXMLTree_attrExt
{
    // String of the full namespace of this element.
    struct ResStringPool_ref ns;
    
    // String name of this node if it is an ELEMENT; the raw
    // character data if this is a CDATA node.
    struct ResStringPool_ref name;
    
    // Byte offset from the start of this structure where the attributes start.
    uint16_t attributeStart;
    
    // Size of the ResXMLTree_attribute structures that follow.
    uint16_t attributeSize;
    
    // Number of attributes associated with an ELEMENT.  These are
    // available as an array of ResXMLTree_attribute structures
    // immediately following this node.
    uint16_t attributeCount;
    
    // Index (1-based) of the "id" attribute. 0 if none.
    uint16_t idIndex;
    
    // Index (1-based) of the "class" attribute. 0 if none.
    uint16_t classIndex;
    
    // Index (1-based) of the "style" attribute. 0 if none.
    uint16_t styleIndex;
};

struct ResXMLTree_attribute
{
    // Namespace of this attribute.
    struct ResStringPool_ref ns;
    
    // Name of this attribute.
    struct ResStringPool_ref name;

    // The original raw string value of this attribute.
    struct ResStringPool_ref rawValue;
    
    // Processesd typed value of this attribute.
    struct Res_value typedValue;
};

class ResXMLTree;

class ResXMLParser
{
public:
    ResXMLParser(const ResXMLTree& tree);

    enum event_code_t {
        BAD_DOCUMENT = -1,
        START_DOCUMENT = 0,
        END_DOCUMENT = 1,
        
        FIRST_CHUNK_CODE = RES_XML_FIRST_CHUNK_TYPE, 
        
        START_NAMESPACE = RES_XML_START_NAMESPACE_TYPE,
        END_NAMESPACE = RES_XML_END_NAMESPACE_TYPE,
        START_TAG = RES_XML_START_ELEMENT_TYPE,
        END_TAG = RES_XML_END_ELEMENT_TYPE,
        TEXT = RES_XML_CDATA_TYPE
    };

    struct ResXMLPosition
    {
        event_code_t                eventCode;
        const ResXMLTree_node*      curNode;
        const void*                 curExt;
    };

    void restart();

    const ResStringPool& getStrings() const;

    event_code_t getEventType() const;
    // Note, unlike XmlPullParser, the first call to next() will return
    // START_TAG of the first element.
    event_code_t next();

    // These are available for all nodes:
    int32_t getCommentID() const;
    const char16_t* getComment(size_t* outLen) const;
    uint32_t getLineNumber() const;
    
    // This is available for TEXT:
    int32_t getTextID() const;
    const char16_t* getText(size_t* outLen) const;
    ssize_t getTextValue(Res_value* outValue) const;
    
    // These are available for START_NAMESPACE and END_NAMESPACE:
    int32_t getNamespacePrefixID() const;
    const char16_t* getNamespacePrefix(size_t* outLen) const;
    int32_t getNamespaceUriID() const;
    const char16_t* getNamespaceUri(size_t* outLen) const;
    
    // These are available for START_TAG and END_TAG:
    int32_t getElementNamespaceID() const;
    const char16_t* getElementNamespace(size_t* outLen) const;
    int32_t getElementNameID() const;
    const char16_t* getElementName(size_t* outLen) const;
    
    // Remaining methods are for retrieving information about attributes
    // associated with a START_TAG:
    
    size_t getAttributeCount() const;
    
    // Returns -1 if no namespace, -2 if idx out of range.
    int32_t getAttributeNamespaceID(size_t idx) const;
    const char16_t* getAttributeNamespace(size_t idx, size_t* outLen) const;

    int32_t getAttributeNameID(size_t idx) const;
    const char16_t* getAttributeName(size_t idx, size_t* outLen) const;
    uint32_t getAttributeNameResID(size_t idx) const;

    // These will work only if the underlying string pool is UTF-8.
    const char* getAttributeNamespace8(size_t idx, size_t* outLen) const;
    const char* getAttributeName8(size_t idx, size_t* outLen) const;

    int32_t getAttributeValueStringID(size_t idx) const;
    const char16_t* getAttributeStringValue(size_t idx, size_t* outLen) const;
    
    int32_t getAttributeDataType(size_t idx) const;
    int32_t getAttributeData(size_t idx) const;
    ssize_t getAttributeValue(size_t idx, Res_value* outValue) const;

    ssize_t indexOfAttribute(const char* ns, const char* attr) const;
    ssize_t indexOfAttribute(const char16_t* ns, size_t nsLen,
                             const char16_t* attr, size_t attrLen) const;

    ssize_t indexOfID() const;
    ssize_t indexOfClass() const;
    ssize_t indexOfStyle() const;

    void getPosition(ResXMLPosition* pos) const;
    void setPosition(const ResXMLPosition& pos);

private:
    friend class ResXMLTree;
    
    event_code_t nextNode();

    const ResXMLTree&           mTree;
    event_code_t                mEventCode;
    const ResXMLTree_node*      mCurNode;
    const void*                 mCurExt;
};

/**
 * Convenience class for accessing data in a ResXMLTree resource.
 */
class ResXMLTree : public ResXMLParser
{
public:
    ResXMLTree();
    ~ResXMLTree();

    status_t setTo(const void* data, size_t size, bool copyData=false);

    status_t getError() const;

    void uninit();

private:
    friend class ResXMLParser;

    status_t validateNode(const ResXMLTree_node* node) const;

    status_t                    mError;
    void*                       mOwnedData;
    const ResXMLTree_header*    mHeader;
    size_t                      mSize;
    const uint8_t*              mDataEnd;
    ResStringPool               mStrings;
    const uint32_t*             mResIds;
    size_t                      mNumResIds;
    const ResXMLTree_node*      mRootNode;
    const void*                 mRootExt;
    event_code_t                mRootCode;
};

}   // namespace android

#endif // _LIBS_UTILS_RESOURCE_TYPES_H
