#include "bcrypt_wrapper.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <crypt.h>

static const char bcrypt_base64[] = 
    "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

static void encode_base64(const uint8_t *data, int len, char *out) {
    int i;
    char *p = out;
    
    for (i = 0; i < len; i += 3) {
        uint32_t val = ((uint32_t)data[i]) << 16;
        if (i + 1 < len) val |= ((uint32_t)data[i + 1]) << 8;
        if (i + 2 < len) val |= data[i + 2];
        
        *p++ = bcrypt_base64[(val >> 18) & 0x3f];
        *p++ = bcrypt_base64[(val >> 12) & 0x3f];
        if (i + 1 < len) *p++ = bcrypt_base64[(val >> 6) & 0x3f];
        if (i + 2 < len) *p++ = bcrypt_base64[val & 0x3f];
    }
    *p = '\0';
}

void bcrypt_gensalt(int workfactor, char *salt) {
    static int seeded = 0;
    uint8_t random_bytes[16];
    
    if (workfactor < 4) workfactor = 4;
    if (workfactor > 31) workfactor = 31;
    
    if (!seeded) {
        srand((unsigned int)time(NULL) ^ ((unsigned int)clock() << 8));
        seeded = 1;
    }
    
    for (int i = 0; i < 16; i++) {
        random_bytes[i] = (uint8_t)(rand() & 0xff);
    }
    
    char encoded[32];
    encode_base64(random_bytes, 16, encoded);
    
    sprintf(salt, "$2a$%02d$%s", workfactor, encoded);
}

int bcrypt_hashpw(const char *passwd, const char *salt, char *hash) {
    struct crypt_data data;
    memset(&data, 0, sizeof(data));
    
    char *result = crypt_r(passwd, salt, &data);
    if (!result || result[0] == '*') {
        return -1;
    }
    
    strncpy(hash, result, BCRYPT_HASHSIZE - 1);
    hash[BCRYPT_HASHSIZE - 1] = '\0';
    
    return 0;
}

int bcrypt_newhash(const char *passwd, int workfactor, char *hash) {
    char salt[32];
    
    if (workfactor <= 0) workfactor = BCRYPT_COST;
    
    bcrypt_gensalt(workfactor, salt);
    
    return bcrypt_hashpw(passwd, salt, hash);
}

int bcrypt_checkpass(const char *passwd, const char *hash) {
    char new_hash[BCRYPT_HASHSIZE];
    
    if (bcrypt_hashpw(passwd, hash, new_hash) != 0) {
        return -1;
    }
    
    return strcmp(hash, new_hash) == 0 ? 0 : -1;
}
