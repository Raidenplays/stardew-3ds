// The public-domain stb libraries we use (see the headers for their notice):
//  - stb_image: inflate (compressed .s3t textures) and PNG reading (mod art)
//  - stb_image_write: deflate, for writing compressed .s3t on the console
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

// zlib-wrapped deflate of a buffer; caller frees with free()
unsigned char* s3d_deflate(const unsigned char* data, int len, int* outLen) {
    return stbi_zlib_compress((unsigned char*)data, len, outLen, 6);
}

// zlib-wrapped inflate into a buffer of exactly outLen bytes; returns bytes written or -1
int s3d_inflate(char* out, int outLen, const char* in, int inLen) {
    return stbi_zlib_decode_buffer(out, outLen, in, inLen);
}

// PNG (or other) file in memory -> RGBA8; caller frees with s3d_image_free
unsigned char* s3d_load_png(const unsigned char* data, int len, int* w, int* h) {
    int n;
    return stbi_load_from_memory(data, len, w, h, &n, 4);
}
void s3d_image_free(unsigned char* p) { stbi_image_free(p); }
