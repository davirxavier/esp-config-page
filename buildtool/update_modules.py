import minify_html
import gzip
import re
import argparse

modules = ['ota', 'wireless', 'ca', 'env', 'files', 'logging']

parser = argparse.ArgumentParser(prog="esp-config-page html build tool.",
                                 description="Python script that can be used to manage the modules that will be included in the config page html.")

for module in modules:
    parser.add_argument("--" + module, required=False, action='store_true')

args = parser.parse_args()

with open('../include/config_page.html', encoding="utf-8") as f:
    read_data = f.read()

    for e in args.__dict__:
        v = args.__dict__[e]
        if not v:
            read_data = re.sub(f'<!--MARKER-{e.upper()}-->?(.*?)<!--END-{e.upper()}-->', '', read_data, flags=re.DOTALL)

    minified = minify_html.minify(read_data, minify_css=True, minify_js=True, remove_bangs=True,
                                  remove_processing_instructions=True)


compressed = gzip.compress(minified.encode(), 9)

with open('../include/config-html.h', 'w') as f:
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

print('Updated enabled modules successfully.')
