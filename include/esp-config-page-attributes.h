#ifndef ESP_CONFIG_PAGE_ATTRIBUTES_H
#define ESP_CONFIG_PAGE_ATTRIBUTES_H

namespace ESP_CONFIG_PAGE
{
    KeyValueStorage *attributeStorage = nullptr;

    enum AttributeType
    {
        ATTR_TEXT,
        ATTR_BOOL,
        ATTR_INT,
        ATTR_FLOAT,
    };

    union AttributeValue {
        char *str;
        int i;
        float f;
        bool b;

        AttributeValue()
        {
            b = false;
        }
        explicit AttributeValue(char *str) : str(str) {}
        explicit AttributeValue(int i) : i(i) {}
        explicit AttributeValue(float f) : f(f) {}
        explicit AttributeValue(bool b) : b(b) {}
    };

    class Attribute
    {
    public:
        Attribute(const char *key, const char *nameStr, const AttributeType type) : key(key), name(nameStr), type(type), onChange(nullptr)
        {
            this->isStrDinamic = false;
        }

        ~Attribute()
        {
            if (this->isStrDinamic)
            {
                free(this->value.str);
            }
        }

        void set(const AttributeValue value)
        {
            this->set(value, true);
        }

        void set(const AttributeValue value, bool save)
        {
            if (this->isStrDinamic)
            {
                free(value.str);
            }

            this->value = value;
            this->isStrDinamic = false;

            if (onChange != nullptr)
            {
                onChange(value);
            }

            if (save && attributeStorage != nullptr)
            {
                char buf[this->serializedValueSize()];
                serializeValue(buf);
                attributeStorage->save(this->key, buf);
            }
        }

        AttributeValue getValue()
        {
            return this->value;
        }

        unsigned int serializedValueSize()
        {
            switch (this->type)
            {
            case ATTR_INT:
                {
                    return snprintf(NULL, 0, "%d", this->value.i);
                }
            case ATTR_FLOAT:
                {
                    return snprintf(NULL, 0, "%f", this->value.f);
                }
            case ATTR_BOOL:
                {
                    return 2;
                }
            case ATTR_TEXT:
                {
                    return strlen(this->value.str) + 1;
                }
            }

            return 0;
        }

        bool serializeValue(char *out)
        {
            switch (this->type)
            {
            case ATTR_INT:
                {
                    sprintf(out, "%d", this->value.i);
                    return true;
                }
            case ATTR_FLOAT:
                {
                    sprintf(out, "%f", this->value.f);
                    return true;
                }
            case ATTR_BOOL:
                {
                    out[0] = this->value.b ? 't' : 'f';
                    out[1] = 0;
                    return true;
                }
            case ATTR_TEXT:
                {
                    strcpy(out, this->value.str);
                    return true;
                }
            }

            return false;
        }

        bool deserializeValue(const char *in)
        {
            AttributeValue newVal;
            bool changed = false;

            switch (this->type)
            {
            case ATTR_INT:
                {
                    sscanf(in, "%d", &newVal.i);
                    changed = true;
                    break;
                }
            case ATTR_FLOAT:
                {
                    sscanf(in, "%f", &newVal.f);
                    changed = true;
                    break;
                }
            case ATTR_BOOL:
                {
                    newVal.b = in[0] == 't';
                    changed = true;
                    break;
                }
            case ATTR_TEXT:
                {
                    newVal.str = (char*) malloc(strlen(in)+1);
                    strcpy(newVal.str, in);
                    changed = true;
                    break;
                }
            }

            if (changed)
            {
                set(newVal, false);
            }

            if (type == ATTR_TEXT)
            {
                this->isStrDinamic = true;
            }

            return changed;
        }

        const char *key;

        /**
         * Function that will be called any time the value changes.
         */
        void (*onChange)(AttributeValue newValue);
    private:
        bool isStrDinamic;
        const char *name;
        const AttributeType type;
        AttributeValue value;
    };

    Attribute **attributes;
    uint8_t attributeCount = 0;
    uint16_t maxAttributes = 0;

    /**
     *  Adds an attribute to the webpage.
     */
    inline void addAttribute(Attribute* attribute)
    {
        LOGF("Adding new attribute: %s\n", attribute->key);

        if (attributeCount + 1 > maxAttributes)
        {
            maxAttributes = maxAttributes == 0 ? 1 : ceil(maxAttributes * 1.5);
            attributes = (Attribute**) realloc(attributes, sizeof(Attribute*) * maxAttributes);
        }

        attributes[attributeCount] = attribute;
        attributeCount++;
    }

    /**
     * Sets the storage for attributes and recovers saved attributes in the filesystem.
     * @param storage Storage to be used in the attributes module.
     */
    inline void setAndUpdateAttributeStorage(KeyValueStorage *storage)
    {
        LOGN("Setting up attribute storage.");
        attributeStorage = storage;
        if (attributeStorage == nullptr)
        {
            LOGN("No attribute storage found.");
            return;
        }

        for (uint8_t i = 0; i < attributeCount; i++)
        {
            Attribute *attribute = attributes[i];
            if (attribute == nullptr)
            {
                continue;
            }

            char *value = attributeStorage->recover(attribute->key);
            if (value == nullptr)
            {
                continue;
            }

            LOGF("Found value saved for attribute %s: %s\n", attribute->key, value);
            attribute->deserializeValue(value);
            free(value);
        }
    }

    inline void getAttributes()
    {
        unsigned int size = 16;
        for (uint8_t i = 0; i < attributeCount; i++)
        {
            size += attributes[i] != nullptr ? attributes[i]->serializedValueSize()+2 : 0;
        }

        char buf[size];
        for (uint8_t i = 0; i < attributeCount; i++)
        {
            if (attributes[i] == nullptr)
            {
                continue;
            }

            char serialized[attributes[i]->serializedValueSize()];
            attributes[i]->serializeValue(serialized);

            strcat(buf, serialized);
            if (i < attributeCount-1)
            {
                strcat(buf, "\n");
            }
        }

        server->send(200, "text/plain", buf);
    }

    inline void findAndSet(const char* key, const char* value)
    {
        for (uint8_t i = 0; i < attributeCount; i++)
        {
            if (attributes[i] != nullptr && strcmp(attributes[i]->key, key) == 0)
            {
                attributes[i]->deserializeValue(value);
                attributeStorage->save(key, value);
                break;
            }
        }
    }

    inline void setAttribute()
    {
        String text = server->arg("plain");

        unsigned int maxLineLength = getMaxLineLength(text.c_str());
        char buf[maxLineLength];

        bool keySet = false;
        char *key = nullptr;

        unsigned int currentChar = 0;
        for (unsigned int i = 0; i < text.length(); i++)
        {
            const char c = text[i];
            if (c == '\n')
            {
                buf[currentChar] = 0;

                if (keySet)
                {
                    findAndSet(key, buf);
                    break;
                }

                key = (char*) malloc(currentChar+1);
                strcpy(key, buf);
                keySet = true;

                currentChar = 0;
            }
            else
            {
                buf[currentChar] = c;
                currentChar++;
            }
        }

        if (key != nullptr)
        {
            free(key);
        }
    }

    inline void enableAttributesModule()
    {
        addServerHandler((char*) "/config/attributes", HTTP_GET, getAttributes);
        addServerHandler((char*) "/config/attributes", HTTP_POST, setAttribute);
    }
}

#endif //ESP_CONFIG_PAGE_ATTRIBUTES_H
