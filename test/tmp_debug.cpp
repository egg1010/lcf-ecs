#include "include/serialization/serialization.hpp"
#include <cstdio>
#include <cstring>
#include <windows.h>

using namespace ecs;

int main() {
    SetConsoleOutputCP(CP_UTF8);

    // === 测试 Protobuf ===
    printf("=== Protobuf Debug ===\n");
    protobuf_codec pc;
    archive_writer* w = pc.create_writer();

    w->begin_object();
    w->key("version"); w->write_u32(42);
    w->key("name");    w->write_string("hello");
    w->key("x");       w->write_f32(3.14f);
    w->key("flag");    w->write_bool(true);
    w->end_object();

    std::string data = w->take();
    pc.destroy_writer(w);

    printf("data size = %zu\n", data.size());
    printf("magic: %02x %02x %02x %02x\n",
           (unsigned char)data[0], (unsigned char)data[1],
           (unsigned char)data[2], (unsigned char)data[3]);
    printf("hex dump (first 32 bytes):\n");
    for (size_t i = 0; i < data.size() && i < 32; ++i) {
        printf("%02x ", (unsigned char)data[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");

    archive_reader* r = pc.create_reader(data);
    printf("reader err = %d\n", r->has_error());

    bool ok = r->enter_object();
    printf("enter_object = %d\n", ok);

    printf("iterating keys:\n");
    std::string_view k;
    int fc = 0;
    while (!(k = r->next_key()).empty()) {
        printf("  key[%d] = '%.*s' (len=%zu)\n", fc, (int)k.size(), k.data(), k.size());
        ++fc;
        if (fc > 10) break;
    }
    printf("field_count = %d\n", fc);
    pc.destroy_reader(r);

    // === 测试 FlatBuffer ===
    printf("\n=== FlatBuffer Debug ===\n");
    flatbuffer_codec fc2;
    archive_writer* fw = fc2.create_writer();

    fw->begin_object();
    fw->key("version"); fw->write_u32(7);
    fw->key("name");    fw->write_string("world");
    fw->key("value");   fw->write_i32(-123);
    fw->end_object();

    std::string fb_data = fw->take();
    fc2.destroy_writer(fw);

    printf("data size = %zu\n", fb_data.size());
    printf("hex dump (first 64 bytes):\n");
    for (size_t i = 0; i < fb_data.size() && i < 64; ++i) {
        printf("%02x ", (unsigned char)fb_data[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");

    archive_reader* fr = fc2.create_reader(fb_data);
    printf("reader err = %d\n", fr->has_error());

    ok = fr->enter_object();
    printf("enter_object = %d\n", ok);

    printf("iterating keys:\n");
    int fc_n = 0;
    while (!(k = fr->next_key()).empty()) {
        printf("  key[%d] = '%.*s'\n", fc_n, (int)k.size(), k.data());
        ++fc_n;
        if (fc_n > 10) break;
    }
    printf("field_count = %d\n", fc_n);
    fc2.destroy_reader(fr);

    return 0;
}
