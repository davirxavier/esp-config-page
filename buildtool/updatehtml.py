import minify_html
import gzip

with open('../config_page.html', encoding="utf-8") as f:
    read_data = f.read()
    minified = minify_html.minify(read_data, minify_css=True, minify_js=True, remove_bangs=True,
                                  remove_processing_instructions=True)


compressed = gzip.compress(minified.encode(), 9)

with open('../config-html.h', 'w') as f:
    f.write('''#ifndef DX_ESP_CONFIG_PAGE_HTML_H
#define ESP_CONFIG_HTML_LEN %s
#define DX_ESP_CONFIG_PAGE_HTML_H
const uint8_t ESP_CONFIG_HTML[] PROGMEM = {''' % len(compressed))

    toWrite = ""

    for byte in compressed:
        toWrite = toWrite + str(int.from_bytes([byte], byteorder='big', signed=False))
        toWrite = toWrite + ", "

    f.write(toWrite)
    f.write("}")
    f.write(";")
    f.write("\n#endif")


print('Written new html')
