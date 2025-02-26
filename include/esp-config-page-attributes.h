#ifndef ESP_CONFIG_PAGE_ATTRIBUTES_H
#define ESP_CONFIG_PAGE_ATTRIBUTES_H

namespace ESP_CONFIG_PAGE
{
    enum AttributeType
    {
        TEXT,
        BOOL,
        INT,
        FLOAT,
    };

    union AttributeValue {
        char *str;
        int i;
        float f;
        bool b;
    };

    class Attribute
    {
    public:
        Attribute(const char *key, const char *nameStr, const AttributeType type) : key(key), name(nameStr), type(type), value(), onChange(nullptr)
        {
            this->isStrDinamic = false;
        }

        ~Attribute()
        {
            if (this->isStrDinamic)
            {
                free(this->value.str);
            }
        };

        void setValue(const AttributeValue value)
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
        }

        AttributeValue getValue()
        {
            return this->value;
        }

        unsigned int serializedValueSize()
        {
            switch (this->type)
            {
            case INT:
                {
                    return snprintf(NULL, 0, "%d", this->value.i);
                }
            case FLOAT:
                {
                    return snprintf(NULL, 0, "%f", this->value.f);
                }
            case BOOL:
                {
                    return 2;
                }
            case TEXT:
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
            case INT:
                {
                    sprintf(out, "%d", this->value.i);
                    return true;
                }
            case FLOAT:
                {
                    sprintf(out, "%f", this->value.f);
                    return true;
                }
            case BOOL:
                {
                    out[0] = this->value.b ? 't' : 'f';
                    out[1] = 0;
                    return true;
                }
            case TEXT:
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
            case INT:
                {
                    sscanf(in, "%d", &newVal.i);
                    changed = true;
                    break;
                }
            case FLOAT:
                {
                    sscanf(in, "%f", &newVal.f);
                    changed = true;
                    break;
                }
            case BOOL:
                {
                    newVal.b = in[0] == 't';
                    changed = true;
                    break;
                }
            case TEXT:
                {
                    newVal.str = (char*) malloc(strlen(in)+1);
                    strcpy(newVal.str, in);
                    changed = true;
                    break;
                }
            }

            if (changed)
            {
                setValue(newVal);
            }

            if (type == TEXT)
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
    KeyValueStorage *attributeStorage = nullptr;

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
    inline void setAndUpdateStorage(KeyValueStorage *storage)
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
        REGISTER_SERVER_METHOD("/config/attributes", HTTP_GET, getAttributes);
        REGISTER_SERVER_METHOD("/config/attributes", HTTP_POST, setAttribute);
    }
}

#endif //ESP_CONFIG_PAGE_ATTRIBUTES_H
