/*
 * Copyright (C) 2015 The Android Open Source Project
 * Copyright (C) 2015 Andrew Gunnerson <andrewgunnerson@gmail.com>
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

#include <iostream>
#include <vector>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <androidfw/ResourceTypes.h>

#include <utils/String8.h>

#include <pugixml.hpp>

using namespace android;

struct namespace_entry {
    String8 prefix;
    String8 uri;
};

static String8 build_namespace(const std::vector<namespace_entry> &namespaces,
                               const char16_t *ns)
{
    String8 str;
    if (ns) {
        str = String8(ns);
        for (const namespace_entry &ne : namespaces) {
            if (ne.uri == str) {
                str = ne.prefix;
                break;
            }
        }
        str.append(":");
    }
    return str;
}

static String8 complexToString(uint32_t complex, bool isFraction)
{
    const float MANTISSA_MULT =
            1.0f / (1 << Res_value::COMPLEX_MANTISSA_SHIFT);
    const float RADIX_MULTS[] = {
        1.0f * MANTISSA_MULT, 1.0f / (1 << 7) * MANTISSA_MULT,
        1.0f / (1 << 15) * MANTISSA_MULT, 1.0f / (1 << 23) * MANTISSA_MULT
    };

    float value = (complex & (Res_value::COMPLEX_MANTISSA_MASK
                    << Res_value::COMPLEX_MANTISSA_SHIFT))
            * RADIX_MULTS[(complex >> Res_value::COMPLEX_RADIX_SHIFT)
                    & Res_value::COMPLEX_RADIX_MASK];

    String8 result = String8::format("%f", value);

    if (!isFraction) {
        switch ((complex >> Res_value::COMPLEX_UNIT_SHIFT)
                & Res_value::COMPLEX_UNIT_MASK) {
        case Res_value::COMPLEX_UNIT_PX: result.append("px"); break;
        case Res_value::COMPLEX_UNIT_DIP: result.append("dp"); break;
        case Res_value::COMPLEX_UNIT_SP: result.append("sp"); break;
        case Res_value::COMPLEX_UNIT_PT: result.append("pt"); break;
        case Res_value::COMPLEX_UNIT_IN: result.append("in"); break;
        case Res_value::COMPLEX_UNIT_MM: result.append("mm"); break;
        default: result.append(" (unknown unit)"); break;
        }
    } else {
        switch ((complex >> Res_value::COMPLEX_UNIT_SHIFT)
                & Res_value::COMPLEX_UNIT_MASK) {
        case Res_value::COMPLEX_UNIT_FRACTION: result.append("%"); break;
        case Res_value::COMPLEX_UNIT_FRACTION_PARENT: result.append("%p"); break;
        default: result.append(" (unknown unit)"); break;
        }
    }

    return result;
}

void printXML(ResXMLTree *block)
{
    pugi::xml_document doc;

    std::vector<pugi::xml_node> stack;

    block->restart();

    std::vector<namespace_entry> namespaces;

    ResXMLTree::event_code_t code;
    while ((code = block->next()) != ResXMLTree::END_DOCUMENT
            && code != ResXMLTree::BAD_DOCUMENT) {
        if (code == ResXMLTree::START_TAG) {
            // Get parent node
            pugi::xml_node &parent = stack.empty() ? doc : stack.back();

            size_t len;

            // Get comment (if any)
            const char16_t *com16 = block->getComment(&len);
            if (com16) {
                parent.append_child(pugi::node_comment).set_value(
                        String8(com16));
            }

            // Get element name
            const char16_t *ns16 = block->getElementNamespace(&len);
            String8 name = build_namespace(namespaces, ns16);
            name.append(String8(block->getElementName(&len)));

            // Add to stack
            stack.push_back(parent.append_child(name.string()));

            pugi::xml_node &current = stack.back();

            // Add attributes
            int numattrs = block->getAttributeCount();
            for (int i = 0; i < numattrs; ++i) {
                // Attribute name
                ns16 = block->getAttributeNamespace(i, &len);
                name = build_namespace(namespaces, ns16);
                name.append(String8(block->getAttributeName(i, &len)));

                pugi::xml_attribute attr = current.append_attribute(name);

                // Attribute value
                Res_value value;
                block->getAttributeValue(i, &value);
                if (value.dataType == Res_value::TYPE_NULL) {
                    // Empty attribute
                } else if (value.dataType == Res_value::TYPE_REFERENCE
                        || value.dataType == Res_value::TYPE_DYNAMIC_REFERENCE) {
                    attr = String8::format("@0x%08x", value.data);
                } else if (value.dataType == Res_value::TYPE_ATTRIBUTE) {
                    attr = String8::format("?0x%08x", value.data);
                } else if (value.dataType == Res_value::TYPE_STRING) {
                    attr = String8(block->getAttributeStringValue(i, &len));
                } else if (value.dataType == Res_value::TYPE_FLOAT) {
                    attr = *(const float *) &value.data;
                } else if (value.dataType == Res_value::TYPE_DIMENSION) {
                    attr = complexToString(value.data, false);
                } else if (value.dataType == Res_value::TYPE_FRACTION) {
                    attr = complexToString(value.data, true);
                } else if (value.dataType >= Res_value::TYPE_FIRST_COLOR_INT
                        && value.dataType <= Res_value::TYPE_LAST_COLOR_INT) {
                    attr = String8::format("#%08x", value.data);
                } else if (value.dataType == Res_value::TYPE_INT_BOOLEAN) {
                    attr = value.data ? "true" : "false";
                } else if (value.dataType == Res_value::TYPE_INT_DEC) {
                    attr = value.data;
                } else if (value.dataType == Res_value::TYPE_INT_HEX) {
                    attr = String8::format("0x%x", value.data);
                } else if (value.dataType >= Res_value::TYPE_FIRST_INT
                        && value.dataType <= Res_value::TYPE_LAST_INT) {
                    attr = String8::format("0x%x", value.data);
                } else {
                    attr = String8::format("(unknown: type=0x%x, value=0x%x)",
                                           value.dataType, value.data);
                }
            }
        } else if (code == ResXMLTree::END_TAG) {
            stack.pop_back();
        } else if (code == ResXMLTree::START_NAMESPACE) {
            namespace_entry ns;
            size_t len;
            const char16_t *prefix16 = block->getNamespacePrefix(&len);
            if (prefix16) {
                ns.prefix = String8(prefix16);
            } else {
                ns.prefix = "<DEF>";
            }
            ns.uri = String8(block->getNamespaceUri(&len));

            namespaces.push_back(ns);
        } else if (code == ResXMLTree::END_NAMESPACE) {
            const namespace_entry &ns = namespaces.front();
            size_t len;
            const char16_t *prefix16 = block->getNamespacePrefix(&len);
            String8 pr;
            if (prefix16) {
                pr = String8(prefix16);
            } else {
                pr = "<DEF>";
            }
            if (ns.prefix != pr) {
                fprintf(stderr, "Error: Bad end namespace prefix: found=%s, expected=%s\n",
                        pr.string(), ns.prefix.string());
            }

            String8 uri = String8(block->getNamespaceUri(&len));
            if (ns.uri != uri) {
                fprintf(stderr, "Error: Bad end namespace URI: found=%s, expected=%s\n",
                        uri.string(), ns.uri.string());
            }

            // Hackish, but we don't need a full-blown XML library with
            // namespaces support
            pugi::xml_node child = doc.first_child();
            if (child) {
                String8 attrName("xmlns:");
                attrName.append(ns.prefix);
                child.append_attribute(attrName) = ns.uri;
            }

            namespaces.pop_back();
        } else if (code == ResXMLTree::TEXT) {
            size_t len;

            pugi::xml_node &current = stack.empty() ? doc : stack.back();
            current.append_child(pugi::node_pcdata).set_value(
                    String8(block->getText(&len)));
        }
    }

    block->restart();

    doc.print(std::cout);
}

int main(int argc, char * const argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: axml2xml [filename]\n");
        return EXIT_FAILURE;
    }

    ResXMLTree tree;

    FILE *fp = fopen(argv[1], "rb");
    if (!fp) {
        fprintf(stderr, "Error: Failed to open %s: %s",
                argv[1], strerror(errno));
        return EXIT_FAILURE;
    }

    fseek(fp, 0, SEEK_END);
    off_t size = ftello(fp);
    rewind(fp);
    std::vector<unsigned char> buf(size);
    fread(buf.data(), buf.size(), 1, fp);
    fclose(fp);

    bool ret = true;

    if (tree.setTo(buf.data(), buf.size()) != NO_ERROR) {
        fprintf(stderr, "Error: Resource %s is corrupt\n", argv[1]);
        ret = false;
    }

    if (ret) {
        tree.restart();
        printXML(&tree);
    }

    tree.uninit();

    return ret ? EXIT_SUCCESS : EXIT_FAILURE;
}
