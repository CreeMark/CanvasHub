#include "protocol_ws.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <stddef.h>

// Base64 encoding table
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// SHA1 constants
#define SHA1_BLOCK_SIZE 20

// Simple SHA1 implementation (or use OpenSSL)
// For simplicity, we'll assume a SHA1 function is available or implement a minimal one.
// To avoid external dependencies for this example, we'll implement a very basic one or use a placeholder.
// BUT, WebSocket requires SHA1. Let's include a minimal SHA1 here.

typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    unsigned char buffer[64];
} SHA1_CTX;

static void SHA1Transform(uint32_t state[5], const unsigned char buffer[64]);

static void SHA1Init(SHA1_CTX *context) {
    context->state[0] = 0x67452301;
    context->state[1] = 0xEFCDAB89;
    context->state[2] = 0x98BADCFE;
    context->state[3] = 0x10325476;
    context->state[4] = 0xC3D2E1F0;
    context->count[0] = context->count[1] = 0;
}

static void SHA1Update(SHA1_CTX *context, const unsigned char *data, uint32_t len) {
    uint32_t i, j;
    j = (context->count[0] >> 3) & 63;
    if ((context->count[0] += len << 3) < (len << 3)) context->count[1]++;
    context->count[1] += (len >> 29);
    if ((j + len) > 63) {
        memcpy(&context->buffer[j], data, (i = 64 - j));
        SHA1Transform(context->state, context->buffer);
        for (; i + 63 < len; i += 64) {
            SHA1Transform(context->state, &data[i]);
        }
        j = 0;
    }
    else i = 0;
    memcpy(&context->buffer[j], &data[i], len - i);
}

static void SHA1Final(unsigned char digest[20], SHA1_CTX *context) {
    unsigned char finalcount[8];
    unsigned char c;
    uint32_t i;
    for (i = 0; i < 8; i++) {
        finalcount[i] = (unsigned char)((context->count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255);
    }
    c = 0200;
    SHA1Update(context, &c, 1);
    while ((context->count[0] & 504) != 448) {
        c = 0000;
        SHA1Update(context, &c, 1);
    }
    SHA1Update(context, finalcount, 8);
    for (i = 0; i < 20; i++) {
        digest[i] = (unsigned char)((context->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
    }
}

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

static void SHA1Transform(uint32_t state[5], const unsigned char buffer[64]) {
    uint32_t a, b, c, d, e, i;
    typedef union {
        unsigned char c[64];
        uint32_t l[16];
    } CHAR64LONG16;
    CHAR64LONG16 block[1];
    memcpy(block, buffer, 64);
    
    // Convert to big-endian
    for(i=0; i<16; i++) {
        block->l[i] = (block->l[i] << 24) | ((block->l[i] & 0xFF00) << 8) | 
                      ((block->l[i] >> 8) & 0xFF00) | (block->l[i] >> 24);
    }

    uint32_t *w = block->l;
    (void)w; // Suppress unused variable warning if macros don't use it directly (they use block->l)
    
    /* Copy context->state[] to working vars */
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];

    /* 4 rounds of 20 operations each. Loop unrolled. */
    // (Implementation omitted for brevity, using simplified loop)
    // Actually, let's implement a minimal correct transform to make it work.
    
    // ... Since implementing full SHA1 is lengthy, let's assume we link against OpenSSL or use a small library.
    // For this demonstration, I will include a very compact SHA1 body or just use a placeholder if testing environment allows.
    // However, since the user asked for pure C implementation without extra heavy libs if possible (or just standard ones),
    // let's complete the transform.
    
    // Helper macros
    #define blk0(i) (block->l[i])
    #define blk(i) (block->l[i&15] = rol(block->l[(i+13)&15]^block->l[(i+8)&15]^block->l[(i+2)&15]^block->l[i&15],1))
    #define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk0(i)+0x5A827999+rol(v,5);w=rol(w,30);
    #define R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk(i)+0x5A827999+rol(v,5);w=rol(w,30);
    #define R2(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0x6ED9EBA1+rol(v,5);w=rol(w,30);
    #define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+blk(i)+0x8F1BBCDC+rol(v,5);w=rol(w,30);
    #define R4(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0xCA62C1D6+rol(v,5);w=rol(w,30);

    R0(a,b,c,d,e, 0); R0(e,a,b,c,d, 1); R0(d,e,a,b,c, 2); R0(c,d,e,a,b, 3);
    R0(b,c,d,e,a, 4); R0(a,b,c,d,e, 5); R0(e,a,b,c,d, 6); R0(d,e,a,b,c, 7);
    R0(c,d,e,a,b, 8); R0(b,c,d,e,a, 9); R0(a,b,c,d,e,10); R0(e,a,b,c,d,11);
    R0(d,e,a,b,c,12); R0(c,d,e,a,b,13); R0(b,c,d,e,a,14); R0(a,b,c,d,e,15);
    R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
    R2(a,b,c,d,e,20); R2(e,a,b,c,d,21); R2(d,e,a,b,c,22); R2(c,d,e,a,b,23);
    R2(b,c,d,e,a,24); R2(a,b,c,d,e,25); R2(e,a,b,c,d,26); R2(d,e,a,b,c,27);
    R2(c,d,e,a,b,28); R2(b,c,d,e,a,29); R2(a,b,c,d,e,30); R2(e,a,b,c,d,31);
    R2(d,e,a,b,c,32); R2(c,d,e,a,b,33); R2(b,c,d,e,a,34); R2(a,b,c,d,e,35);
    R2(e,a,b,c,d,36); R2(d,e,a,b,c,37); R2(c,d,e,a,b,38); R2(b,c,d,e,a,39);
    R3(a,b,c,d,e,40); R3(e,a,b,c,d,41); R3(d,e,a,b,c,42); R3(c,d,e,a,b,43);
    R3(b,c,d,e,a,44); R3(a,b,c,d,e,45); R3(e,a,b,c,d,46); R3(d,e,a,b,c,47);
    R3(c,d,e,a,b,48); R3(b,c,d,e,a,49); R3(a,b,c,d,e,50); R3(e,a,b,c,d,51);
    R3(d,e,a,b,c,52); R3(c,d,e,a,b,53); R3(b,c,d,e,a,54); R3(a,b,c,d,e,55);
    R3(e,a,b,c,d,56); R3(d,e,a,b,c,57); R3(c,d,e,a,b,58); R3(b,c,d,e,a,59);
    R4(a,b,c,d,e,60); R4(e,a,b,c,d,61); R4(d,e,a,b,c,62); R4(c,d,e,a,b,63);
    R4(b,c,d,e,a,64); R4(a,b,c,d,e,65); R4(e,a,b,c,d,66); R4(d,e,a,b,c,67);
    R4(c,d,e,a,b,68); R4(b,c,d,e,a,69); R4(a,b,c,d,e,70); R4(e,a,b,c,d,71);
    R4(d,e,a,b,c,72); R4(c,d,e,a,b,73); R4(b,c,d,e,a,74); R4(a,b,c,d,e,75);
    R4(e,a,b,c,d,76); R4(d,e,a,b,c,77); R4(c,d,e,a,b,78); R4(b,c,d,e,a,79);

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

static void base64_encode(const unsigned char *in, size_t in_len, char *out) {
    size_t i = 0, j = 0;
    unsigned char a, b, c;

    while (i < in_len) {
        a = in[i++];
        b = (i < in_len) ? in[i++] : 0;
        c = (i < in_len) ? in[i++] : 0;

        out[j++] = b64_table[a >> 2];
        out[j++] = b64_table[((a & 3) << 4) | (b >> 4)];
        out[j++] = (i > in_len + 1) ? '=' : b64_table[((b & 15) << 2) | (c >> 6)];
        out[j++] = (i > in_len) ? '=' : b64_table[c & 63];
    }
    out[j] = '\0';
}

int ws_parse_handshake_key(const char *request, char *out_key, size_t max_len) {
    const char *key_header = "Sec-WebSocket-Key: ";
    char *start = strstr(request, key_header);
    if (!start) return -1;
    
    start += strlen(key_header);
    char *end = strstr(start, "\r\n");
    if (!end) return -1;
    
    size_t len = end - start;
    if (len >= max_len) return -1;
    
    strncpy(out_key, start, len);
    out_key[len] = '\0';
    return 0;
}

int ws_generate_handshake_response(const char *sec_websocket_key, char *out_response, size_t *out_len) {
    const char *guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char combined[256];
    unsigned char sha1_hash[20];
    char b64_hash[32];
    
    snprintf(combined, sizeof(combined), "%s%s", sec_websocket_key, guid);
    
    SHA1_CTX ctx;
    SHA1Init(&ctx);
    SHA1Update(&ctx, (unsigned char *)combined, strlen(combined));
    SHA1Final(sha1_hash, &ctx);
    
    base64_encode(sha1_hash, 20, b64_hash);
    
    int n = snprintf(out_response, *out_len,
             "HTTP/1.1 101 Switching Protocols\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Accept: %s\r\n"
             "\r\n",
             b64_hash);
             
    if (n > 0) {
        *out_len = n;
        return 0;
    }
    return -1;
}

int ws_is_handshake(const char *request) {
    return (strstr(request, "GET") && strstr(request, "Upgrade: websocket"));
}

int ws_build_frame(const char *payload, size_t payload_len, char *out_frame, size_t *out_len) {
    // Basic text frame, no masking (server -> client)
    // FIN=1, Opcode=1 (text)
    out_frame[0] = 0x81;
    
    size_t header_len = 2;
    if (payload_len < 126) {
        out_frame[1] = payload_len;
    } else if (payload_len < 65536) {
        out_frame[1] = 126;
        out_frame[2] = (payload_len >> 8) & 0xFF;
        out_frame[3] = payload_len & 0xFF;
        header_len = 4;
    } else {
        out_frame[1] = 127;
        // 64-bit length support omitted for brevity, assume < 4GB
        // ...
        return -1; // Too large for this simple implementation
    }
    
    if (*out_len < header_len + payload_len) return -1;
    
    memcpy(out_frame + header_len, payload, payload_len);
    *out_len = header_len + payload_len;
    return 0;
}

int ws_parse_frame(const char *in_frame, size_t in_len, char **out_payload, size_t *out_payload_len) {
    if (in_len < 2) return -1;
    
    unsigned char b0 = in_frame[0];
    (void)b0; // Unused for now
    unsigned char b1 = in_frame[1];
    
    int masked = (b1 & 0x80) != 0;
    uint64_t payload_len = b1 & 0x7F;
    
    size_t header_len = 2;
    if (payload_len == 126) {
        if (in_len < 4) return -2; // Incomplete
        payload_len = ((uint8_t)in_frame[2] << 8) | (uint8_t)in_frame[3];
        header_len = 4;
    } else if (payload_len == 127) {
        // 64-bit length
        if (in_len < 10) return -2; // Incomplete
        header_len = 10;
        // 64位长度
        payload_len = 0;
        for (int i = 2; i < 10; i++) {
            payload_len = (payload_len << 8) | (uint8_t)in_frame[i];
        }
    }
    
    // 添加掩码长度
    if (masked) {
        if (in_len < header_len + 4) return -2; // Incomplete
        header_len += 4;
    }
    
    // 检查是否有足够的完整数据
    uint64_t total_len_needed = header_len + payload_len;
    if (in_len < total_len_needed) {
        g_print("DEBUG: Incomplete frame: in_len=%zu, need=%lu, payload_len=%lu\n", 
               in_len, (unsigned long)total_len_needed, (unsigned long)payload_len);
        return -2; // Incomplete
    }
    
    // 分配内存
    char *payload = malloc(payload_len + 1);
    if (!payload) return -1;
    
    // 复制数据
    memcpy(payload, in_frame + header_len, payload_len);
    
    // 解掩码
    if (masked) {
        unsigned char mask[4];
        memcpy(mask, in_frame + header_len - 4, 4);
        for (size_t i = 0; i < payload_len; i++) {
            payload[i] ^= mask[i % 4];
        }
    }
    
    payload[payload_len] = '\0';
    *out_payload = payload;
    *out_payload_len = payload_len;
    
    g_print("Parsing frame: in_len=%zu, masked=%d, payload_len=%lu, consumed=%zu\n", 
           in_len, masked, (unsigned long)payload_len, header_len + payload_len);
    
    return header_len + payload_len;
}
